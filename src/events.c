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

#define AKVCAM_EVENTS_QUEUE_MAX 32

struct akvcam_events
{
    akvcam_object_t self;
    akvcam_list_tt(struct v4l2_event_subscription) subscriptions;
    wait_queue_head_t event_signaled;
    struct v4l2_event *events_buffer;
    size_t read;
    size_t write;
    size_t n_events;
    __u32 sequence;
};

void akvcam_events_remove_unsub(akvcam_events_t self,
                                struct v4l2_event_subscription *sub);

akvcam_events_t akvcam_events_new(void)
{
    akvcam_events_t self = kzalloc(sizeof(struct akvcam_events), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_events_delete);
    self->subscriptions = akvcam_list_new();
    init_waitqueue_head(&self->event_signaled);
    self->events_buffer = kzalloc(AKVCAM_EVENTS_QUEUE_MAX
                                  * sizeof(struct v4l2_event),
                                  GFP_KERNEL);

    self->read = 0;
    self->write = 0;
    self->n_events = 0;
    self->sequence = 0;

    return self;
}

void akvcam_events_delete(akvcam_events_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    kfree((*self)->events_buffer);
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

    akvcam_list_push_back_copy(self->subscriptions,
                               subscription,
                               sizeof(struct v4l2_event_subscription),
                               akvcam_delete_data);
}

void akvcam_events_unsubscribe(akvcam_events_t self,
                               struct v4l2_event_subscription *subscription)
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
    self->read = 0;
    self->write = 0;
    self->n_events = 0;
    self->sequence = 0;
}

unsigned int akvcam_events_poll(akvcam_events_t self,
                                struct file *filp,
                                struct poll_table_struct *wait)
{
    if (self->n_events > 0)
        return POLLPRI;

    poll_wait(filp, &self->event_signaled, wait);

    return 0;
}

bool akvcam_events_check(const struct v4l2_event_subscription *sub,
                         const struct v4l2_event *event,
                         size_t size)
{
    return sub->type == event->type && sub->id == event->id;
}

bool akvcam_events_enqueue(akvcam_events_t self, struct v4l2_event *event)
{
    struct v4l2_event *qevent;

    // Check if someone is subscribed to this event.
    if (!akvcam_list_find(self->subscriptions,
                          event,
                          0,
                          (akvcam_are_equals_t) akvcam_events_check)) {
        return false;
    }

    // Buffer is full, advance the read pointer.
    if (self->n_events == AKVCAM_EVENTS_QUEUE_MAX)
        self->read = (self->read + 1) % AKVCAM_EVENTS_QUEUE_MAX;

    // Put a new event in the queue, discard old events if necessary.
    qevent = self->events_buffer + self->write;
    memcpy(qevent, event, sizeof(struct v4l2_event));
    qevent->sequence = self->sequence++;
    ktime_get_ts(&qevent->timestamp);
    memset(&qevent->reserved, 0, 8 * sizeof(__u32));
    self->write = (self->write + 1) % AKVCAM_EVENTS_QUEUE_MAX;

    if (self->n_events < AKVCAM_EVENTS_QUEUE_MAX)
        self->n_events++;

    // Inform about the new event.
    wake_up_all(&self->event_signaled);

    return true;
}

bool akvcam_events_dequeue(akvcam_events_t self, struct v4l2_event *event)
{
    struct v4l2_event *qevent;

    if (self->n_events < 1)
        return false;

    qevent = self->events_buffer + self->read;
    memcpy(event, qevent, sizeof(struct v4l2_event));
    self->read = (self->read + 1) % AKVCAM_EVENTS_QUEUE_MAX;
    event->pending = (__u32) --self->n_events;

    return true;
}

bool akvcam_events_available(akvcam_events_t self)
{
    return self->n_events > 0;
}

void akvcam_events_remove_unsub(akvcam_events_t self,
                                struct v4l2_event_subscription *sub)
{
    size_t i;
    size_t read;
    akvcam_list_element_t it = NULL;
    struct v4l2_event *event;
    struct v4l2_event *subscribed_event;
    akvcam_list_t subscribed_events = akvcam_list_new();

    for (i = 0; i < self->n_events; i++) {
        read = (i + self->read) % AKVCAM_EVENTS_QUEUE_MAX;
        event = self->events_buffer + read;

        if (sub->type != event->type || sub->id !=  event->id)
            akvcam_list_push_back(subscribed_events, event, NULL);
    }

    if (akvcam_list_size(subscribed_events) != self->n_events) {
        for (i = 0;; i++) {
            event = akvcam_list_next(subscribed_events, &it);

            if (!it)
                break;

            read = (i + self->read) % AKVCAM_EVENTS_QUEUE_MAX;
            subscribed_event = self->events_buffer + read;

            if (subscribed_event != event)
                memcpy(subscribed_event,
                       event,
                       sizeof(struct v4l2_event));
        }

        self->n_events = akvcam_list_size(subscribed_events);
        self->write = (self->read + self->n_events) % AKVCAM_EVENTS_QUEUE_MAX;
    }

    akvcam_list_delete(&subscribed_events);
}
