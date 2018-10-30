/* akvcam, virtual camera for Linux.
 * Copyright (C) 2018  Gonzalo Exequiel Pedone
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/videodev2.h>

#include "events.h"
#include "list.h"
#include "object.h"
#include "rbuffer.h"

#define AKVCAM_EVENTS_QUEUE_MAX 32

struct akvcam_events
{
    akvcam_object_t self;
    akvcam_list_tt(struct v4l2_event_subscription) subscriptions;
    akvcam_rbuffer_tt(struct v4l2_event) events;
    wait_queue_head_t event_signaled;
    __u32 sequence;
};

void akvcam_events_remove_unsub(akvcam_events_t self,
                                const struct v4l2_event_subscription *sub);

akvcam_events_t akvcam_events_new(void)
{
    akvcam_events_t self = kzalloc(sizeof(struct akvcam_events), GFP_KERNEL);
    self->self = akvcam_object_new("events",
                                   self,
                                   (akvcam_deleter_t) akvcam_events_delete);
    self->subscriptions = akvcam_list_new();
    init_waitqueue_head(&self->event_signaled);
    self->events = akvcam_rbuffer_new();
    akvcam_rbuffer_resize(self->events,
                          AKVCAM_EVENTS_QUEUE_MAX,
                          sizeof(struct v4l2_event),
                          AKVCAM_MEMORY_TYPE_KMALLOC);
    self->sequence = 0;

    return self;
}

void akvcam_events_delete(akvcam_events_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_rbuffer_delete(&((*self)->events));
    akvcam_list_delete(&((*self)->subscriptions));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

void akvcam_events_subscribe(akvcam_events_t self,
                             struct v4l2_event_subscription *subscription)
{
    akvcam_list_element_t it;
    it = akvcam_list_find(self->subscriptions,
                          subscription,
                          sizeof(struct v4l2_event_subscription),
                          NULL);

    if (it)
        return;

    akvcam_list_push_back(self->subscriptions,
                          subscription,
                          sizeof(struct v4l2_event_subscription),
                          NULL,
                          false);
}

void akvcam_events_unsubscribe(akvcam_events_t self,
                               const struct v4l2_event_subscription *subscription)
{
    akvcam_list_element_t it;
    it = akvcam_list_find(self->subscriptions,
                          subscription,
                          sizeof(struct v4l2_event_subscription),
                          NULL);

    if (!it)
        return;

    akvcam_events_remove_unsub(self, akvcam_list_element_data(it));
    akvcam_list_erase(self->subscriptions, it);
}

void akvcam_events_unsubscribe_all(akvcam_events_t self)
{
    akvcam_list_clear(self->subscriptions);
    akvcam_rbuffer_clear(self->events);
    self->sequence = 0;
}

__poll_t akvcam_events_poll(akvcam_events_t self,
                            struct file *filp,
                            struct poll_table_struct *wait)
{
    if (akvcam_rbuffer_data_size(self->events) > 0)
        return AK_EPOLLIN | AK_EPOLLPRI | AK_EPOLLRDNORM;

    poll_wait(filp, &self->event_signaled, wait);

    return 0;
}

static bool akvcam_events_check(const struct v4l2_event_subscription *sub,
                                const struct v4l2_event *event,
                                size_t size)
{
    UNUSED(size);

    return sub->type == event->type && sub->id == event->id;
}

bool akvcam_events_enqueue(akvcam_events_t self,
                           const struct v4l2_event *event)
{
    struct v4l2_event *qevent;

    if (event->type != V4L2_EVENT_FRAME_SYNC) {
        // Check if someone is subscribed to this event.
        if (!akvcam_list_find(self->subscriptions,
                              event,
                              0,
                              (akvcam_are_equals_t) akvcam_events_check)) {
            return false;
        }
    }

    qevent = akvcam_rbuffer_queue(self->events, event);
    qevent->sequence = self->sequence++;
    ktime_get_ts(&qevent->timestamp);
    memset(&qevent->reserved, 0, 8 * sizeof(__u32));

    // Inform about the new event.
    wake_up_all(&self->event_signaled);

    return true;
}

bool akvcam_events_dequeue(akvcam_events_t self, struct v4l2_event *event)
{
    if (akvcam_rbuffer_data_size(self->events) < 1)
        return false;

    akvcam_rbuffer_dequeue(self->events, event, false);
    event->pending = (__u32) akvcam_rbuffer_n_data(self->events);

    return true;
}

bool akvcam_events_available(const akvcam_events_t self)
{
    return akvcam_rbuffer_data_size(self->events) > 0;
}

void akvcam_events_remove_unsub(akvcam_events_t self,
                                const struct v4l2_event_subscription *sub)
{
    struct v4l2_event event;
    akvcam_rbuffer_tt(struct v4l2_event) subscribed_events =
            akvcam_rbuffer_new();

    akvcam_rbuffer_resize(subscribed_events,
                          akvcam_rbuffer_n_elements(self->events),
                          akvcam_rbuffer_element_size(self->events),
                          AKVCAM_MEMORY_TYPE_KMALLOC);

    while (akvcam_rbuffer_data_size(self->events) > 0) {
        akvcam_rbuffer_dequeue(self->events, &event, false);

        if (sub->type != event.type || sub->id !=  event.id)
            akvcam_rbuffer_queue(subscribed_events, &event);
    }

    akvcam_rbuffer_copy(self->events, subscribed_events);
    akvcam_rbuffer_delete(&subscribed_events);
}
