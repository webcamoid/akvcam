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

#include <linux/delay.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <media/v4l2-device.h>

#include "device.h"
#include "buffers.h"
#include "controls.h"
#include "driver.h"
#include "events.h"
#include "format.h"
#include "frame.h"
#include "list.h"
#include "node.h"
#include "object.h"

struct akvcam_device
{
    akvcam_object_t self;
    akvcam_list_tt(akvcam_format_t) formats;
    akvcam_format_t format;
    akvcam_controls_t controls;
    akvcam_list_tt(akvcam_node_t) nodes;
    akvcam_node_t priority_node;
    akvcam_buffers_t buffers;
    struct v4l2_device v4l2_dev;
    struct video_device *vdev;
    struct task_struct *thread;
    AKVCAM_DEVICE_TYPE type;
    AKVCAM_RW_MODE rw_mode;
    enum v4l2_priority priority;
    bool is_registered;
    bool streaming;
};

typedef int (*akvacam_thread_t)(void *data);

void akvcam_device_event_received(akvcam_device_t self,
                                  struct v4l2_event *event);
int akvcam_device_send_frames(akvcam_device_t self);

akvcam_device_t akvcam_device_new(const char *name,
                                  AKVCAM_DEVICE_TYPE type,
                                  AKVCAM_RW_MODE rw_mode)
{
    akvcam_controls_changed_callback controls_changed;
    akvcam_frame_ready_callback frame_ready;

    akvcam_device_t self = kzalloc(sizeof(struct akvcam_device), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_device_delete);
    self->formats = akvcam_list_new();
    self->format = akvcam_format_new(0, 0, 0, NULL);
    self->controls = akvcam_controls_new();
    controls_changed.user_data = self;
    controls_changed.callback =
            (akvcam_controls_changed_proc) akvcam_device_event_received;
    akvcam_controls_set_changed_callback(self->controls, controls_changed);
    self->nodes = akvcam_list_new();
    self->priority_node = NULL;
    self->type = type;
    self->rw_mode = rw_mode;
    self->priority = V4L2_PRIORITY_DEFAULT;

    memset(&self->v4l2_dev, 0, sizeof(struct v4l2_device));
    snprintf(self->v4l2_dev.name,
             V4L2_DEVICE_NAME_SIZE,
             "akvcam-device-%llu", akvcam_id());

    self->vdev = video_device_alloc();
    snprintf(self->vdev->name, 32, "%s", name);
    self->vdev->v4l2_dev = &self->v4l2_dev;
    self->vdev->vfl_type = VFL_TYPE_GRABBER;
    self->vdev->vfl_dir =
            type == AKVCAM_DEVICE_TYPE_OUTPUT? VFL_DIR_TX: VFL_DIR_RX;
    self->vdev->minor = -1;
    self->vdev->fops = akvcam_node_fops();
    self->vdev->tvnorms = V4L2_STD_ALL;
    self->vdev->release = video_device_release_empty;
    video_set_drvdata(self->vdev, self);
    self->is_registered = false;

    self->buffers = akvcam_buffers_new(self);
    frame_ready.user_data = self;
    frame_ready.callback =
            (akvcam_frame_ready_proc) akvcam_device_event_received;
    akvcam_buffers_set_frame_ready_callback(self->buffers, frame_ready);

    return self;
}

void akvcam_device_delete(akvcam_device_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_buffers_delete(&((*self)->buffers));
    akvcam_device_unregister(*self);
    video_device_release((*self)->vdev);
    akvcam_list_delete(&((*self)->nodes));
    akvcam_controls_delete(&((*self)->controls));
    akvcam_format_delete(&((*self)->format));
    akvcam_list_delete(&((*self)->formats));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

bool akvcam_device_register(akvcam_device_t self)
{
    int result;

    if (self->is_registered)
        return true;

    result = v4l2_device_register(NULL, &self->v4l2_dev);

    if (!result)
        result = video_register_device(self->vdev, VFL_TYPE_GRABBER, -1);

    akvcam_set_last_error(result);
    self->is_registered = result? false: true;

    return result? false: true;
}

void akvcam_device_unregister(akvcam_device_t self)
{
    if (!self->is_registered)
        return;

    video_unregister_device(self->vdev);
    v4l2_device_unregister(&self->v4l2_dev);
    self->is_registered = false;
}

u16 akvcam_device_num(const akvcam_device_t self)
{
    return self->vdev->num;
}

AKVCAM_DEVICE_TYPE akvcam_device_type(const akvcam_device_t self)
{
    return self->type;
}

AKVCAM_RW_MODE akvcam_device_rw_mode(const akvcam_device_t self)
{
    return self->rw_mode;
}

struct akvcam_list *akvcam_device_formats_nr(const akvcam_device_t self)
{
    return self->formats;
}

struct akvcam_list *akvcam_device_formats(const akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->formats));

    return self->formats;
}

struct akvcam_format *akvcam_device_format_nr(const akvcam_device_t self)
{
    return self->format;
}

struct akvcam_format *akvcam_device_format(const akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->format));

    return self->format;
}

struct akvcam_controls *akvcam_device_controls_nr(const akvcam_device_t self)
{
    return self->controls;
}

struct akvcam_controls *akvcam_device_controls(const akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->controls));

    return self->controls;
}

struct akvcam_list *akvcam_device_nodes_nr(const akvcam_device_t self)
{
    return self->nodes;
}

struct akvcam_list *akvcam_device_nodes(const akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->nodes));

    return self->nodes;
}

struct akvcam_buffers *akvcam_device_buffers_nr(const akvcam_device_t self)
{
    return self->buffers;
}

struct akvcam_buffers *akvcam_device_buffers(const akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->buffers));

    return self->buffers;
}

struct akvcam_node *akvcam_device_priority_node(const akvcam_device_t self)
{
    return self->priority_node;
}

void akvcam_device_set_priority(akvcam_device_t self,
                                enum v4l2_priority priority,
                                struct akvcam_node *node)
{
    self->priority = priority;
    self->priority_node = node;
}

enum v4l2_priority akvcam_device_priority(const akvcam_device_t self)
{
    return self->priority;
}

bool akvcam_device_streaming(const akvcam_device_t self)
{
    return self->streaming;
}

void akvcam_device_set_streaming(akvcam_device_t self, bool streaming)
{
    if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE) {
        if (!self->streaming && streaming) {
            akvcam_buffers_reset_sequence(self->buffers);
            self->thread = kthread_run((akvacam_thread_t)
                                       akvcam_device_send_frames,
                                       self,
                                       "akvcam-thread-%llu",
                                       akvcam_id());
        } else if (self->streaming && !streaming) {
            kthread_stop(self->thread);
            self->thread = NULL;
        }
    } else if (!self->streaming && streaming) {
        akvcam_buffers_reset_sequence(self->buffers);
    }

    self->streaming = streaming;
}

size_t akvcam_device_sizeof(void)
{
    return sizeof(struct akvcam_device);
}

static bool akvcam_device_are_equals(const akvcam_device_t device,
                                     const struct file *filp,
                                     size_t size)
{
    bool equals;
    char *devname = kzalloc(1024, GFP_KERNEL);
    snprintf(devname, 1024, "video%d", device->vdev->num);
    equals = strcmp(devname, (char *) filp->f_path.dentry->d_iname) == 0;
    kfree(devname);

    return equals;
}

akvcam_device_t akvcam_device_from_file_nr(struct file *filp)
{
    akvcam_list_element_t it;

    if (filp->private_data)
        return akvcam_node_device_nr(filp->private_data);

    it = akvcam_list_find(akvcam_driver_devices_nr(),
                          filp,
                          0,
                          (akvcam_are_equals_t) akvcam_device_are_equals);

    return akvcam_list_element_data(it);
}

akvcam_device_t akvcam_device_from_file(struct file *filp)
{
    akvcam_device_t device = akvcam_device_from_file_nr(filp);

    if (!device)
        return NULL;

    akvcam_object_ref(AKVCAM_TO_OBJECT(device));

    return device;
}

void akvcam_device_event_received(akvcam_device_t self,
                                  struct v4l2_event *event)
{
    akvcam_node_t node;
    akvcam_list_element_t element = NULL;

    for (;;) {
        node = akvcam_list_next(self->nodes, &element);

        if (!element)
            break;

        akvcam_events_enqueue(akvcam_node_events_nr(node), event);
    }
}

bool akvcam_device_prepare_frame(akvcam_device_t self)
{
    bool result;

    akvcam_frame_t frame = akvcam_frame_new(self->format, NULL, 0);
    get_random_bytes_arch(akvcam_frame_data(frame),
                          (int) akvcam_frame_size(frame));
    result = akvcam_buffers_write_frame(self->buffers, frame);
    akvcam_frame_delete(&frame);

    return result;
}

int akvcam_device_send_frames(akvcam_device_t self)
{
    struct v4l2_fract *frame_rate = akvcam_format_frame_rate(self->format);
    __u32 tsleep = 1000 * frame_rate->denominator;

    if (frame_rate->numerator)
        tsleep /= frame_rate->numerator;

    while (!kthread_should_stop()) {
        if (akvcam_device_prepare_frame(self))
            akvcam_buffers_notify_frame(self->buffers);

        msleep_interruptible(tsleep);
    }

    return 0;
}
