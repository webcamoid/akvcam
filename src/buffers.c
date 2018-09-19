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
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "buffers.h"
#include "device.h"
#include "format.h"
#include "frame.h"
#include "list.h"
#include "node.h"
#include "object.h"
#include "rbuffer.h"

typedef struct
{
    struct v4l2_buffer buffer;
    char *data;
} akvcam_buffer, *akvcam_buffer_t;

struct akvcam_buffers
{
    akvcam_object_t self;
    akvcam_device_t device;
    enum v4l2_buf_type type;
    akvcam_list_tt(akvcam_buffer_t) buffers;
    akvcam_rbuffer_tt(char) rw_buffers;
    spinlock_t slock;
    akvcam_frame_ready_callback frame_ready;
    akvcam_node_t main_node;
    wait_queue_head_t frame_is_ready;
    size_t rw_buffer_size;
    AKVCAM_RW_MODE rw_mode;
    __u32 sequence;
};

bool akvcam_buffers_is_supported(akvcam_buffers_t self, enum v4l2_memory type);
bool akvcam_buffers_frame_available(akvcam_buffers_t self);
void akvcam_buffer_delete(akvcam_buffer_t *buffer);

akvcam_buffers_t akvcam_buffers_new(struct akvcam_device *device)
{
    akvcam_buffers_t self = kzalloc(sizeof(struct akvcam_buffers), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_buffers_delete);
    self->buffers = akvcam_list_new();
    self->rw_buffers = akvcam_rbuffer_new();
    spin_lock_init(&self->slock);
    self->device = device;
    self->type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;
    self->rw_mode = akvcam_device_rw_mode(device);
    self->rw_buffer_size = AKVCAM_BUFFERS_MIN;
    init_waitqueue_head(&self->frame_is_ready);

    return self;
}

void akvcam_buffers_delete(akvcam_buffers_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_rbuffer_delete(&((*self)->rw_buffers));
    akvcam_list_delete(&((*self)->buffers));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

int akvcam_buffers_allocate(akvcam_buffers_t self,
                            struct akvcam_node *node,
                            struct v4l2_requestbuffers *params)
{
    size_t i;
    akvcam_format_t format;
    akvcam_buffer_t buffer;
    size_t buffer_length;
    __u32 buffer_size;

    memset(params->reserved, 0, 2 * sizeof(__u32));

    if (!akvcam_buffers_is_supported(self, params->memory))
        return -EINVAL;

    if (params->type != self->type)
        return -EINVAL;

    if (self->main_node && self->main_node != node)
        return -EBUSY;

    spin_lock(&self->slock);
    akvcam_list_clear(self->buffers);
    spin_unlock(&self->slock);

    if (params->count < 1) {
        self->main_node = NULL;
        akvcam_buffers_resize_rw(self, self->rw_buffer_size);
    } else {
        self->main_node = node;
        format = akvcam_device_format_nr(self->device);
        buffer_length = akvcam_format_size(format);
        buffer_size = (__u32) PAGE_ALIGN(buffer_length);

        for (i = 0; i < params->count; i++) {
            buffer = kzalloc(sizeof(akvcam_buffer), GFP_KERNEL);
            buffer->buffer.index = (__u32) i;
            buffer->buffer.type = params->type;
            buffer->buffer.memory = params->memory;
            buffer->buffer.bytesused = (__u32) buffer_length;
            buffer->buffer.length = buffer->buffer.bytesused;
            buffer->buffer.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            buffer->buffer.field = V4L2_FIELD_NONE;
            buffer->data = vzalloc(buffer->buffer.length);

            if (params->memory == V4L2_MEMORY_MMAP) {
                buffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED
                                     |  V4L2_BUF_FLAG_QUEUED;
                buffer->buffer.m.offset =
                        buffer->buffer.index * buffer_size;
            }

            spin_lock(&self->slock);
            akvcam_list_push_back(self->buffers,
                                  buffer,
                                  (akvcam_deleter_t) akvcam_buffer_delete);
            spin_unlock(&self->slock);
        }
    }

    return 0;
}

void akvcam_buffers_deallocate(akvcam_buffers_t self, struct akvcam_node *node)
{
    if (node && node == self->main_node) {
        self->main_node = NULL;
        spin_lock(&self->slock);
        akvcam_list_clear(self->buffers);
        spin_unlock(&self->slock);
        akvcam_buffers_resize_rw(self, self->rw_buffer_size);
    }
}

int akvcam_buffers_create(akvcam_buffers_t self,
                          struct akvcam_node *node,
                          struct v4l2_create_buffers *buffers)
{
    size_t i;
    akvcam_format_t format;
    akvcam_buffer_t buffer;
    akvcam_buffer_t last_buffer;
    akvcam_list_tt(akvcam_format_t) formats;
    size_t buffer_length;
    __u32 buffer_size;
    __u32 offset = 0;

    buffers->index = (__u32) akvcam_list_size(self->buffers);
    memset(buffers->reserved, 0, 8 * sizeof(__u32));

    if (!akvcam_buffers_is_supported(self, buffers->memory))
        return -EINVAL;

    if (buffers->format.type != self->type)
        return -EINVAL;

    formats = akvcam_device_formats_nr(self->device);
    format = akvcam_format_from_v4l2_nr(formats, &buffers->format);

    if (!format)
        return -EINVAL;

    if (self->main_node && self->main_node != node)
        return -EBUSY;

    if (buffers->count > 0) {
        if (!self->main_node)
            buffers->index = 0;

        self->main_node = node;
        buffer_length = akvcam_format_size(format);
        buffer_size = (__u32) PAGE_ALIGN(buffer_length);

        spin_lock(&self->slock);
        last_buffer = akvcam_list_back(self->buffers);

        if (last_buffer)
            offset = last_buffer->buffer.m.offset
                   + PAGE_ALIGN(last_buffer->buffer.length);

        spin_unlock(&self->slock);

        for (i = 0; i < buffers->count; i++) {
            buffer = kzalloc(sizeof(akvcam_buffer), GFP_KERNEL);
            buffer->buffer.index = buffers->index + (__u32) i;
            buffer->buffer.type = self->type;
            buffer->buffer.memory = buffers->memory;
            buffer->buffer.bytesused = (__u32) buffer_length;
            buffer->buffer.length = buffer->buffer.bytesused;
            buffer->buffer.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            buffer->buffer.field = V4L2_FIELD_NONE;
            buffer->data = vzalloc(buffer->buffer.length);

            if (buffers->memory == V4L2_MEMORY_MMAP) {
                buffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED
                                     |  V4L2_BUF_FLAG_QUEUED;
                buffer->buffer.m.offset = offset + (__u32) i * buffer_size;
            }

            spin_lock(&self->slock);
            akvcam_list_push_back(self->buffers,
                                  buffer,
                                  (akvcam_deleter_t) akvcam_buffer_delete);
            spin_unlock(&self->slock);
        }
    }

    return 0;
}

bool akvcam_buffers_fill(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    bool ok = false;

    if (!self->main_node)
        return false;

    spin_lock(&self->slock);
    akbuffer = akvcam_list_at(self->buffers, buffer->index);

    if (akbuffer && akbuffer->buffer.type == buffer->type) {
        memcpy(buffer, &akbuffer->buffer, sizeof(struct v4l2_buffer));
        buffer->flags &= (__u32) ~(V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE);
        ok = true;
    }

    spin_unlock(&self->slock);

    return ok;
}

int akvcam_buffers_queue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    int result = -EINVAL;
    akvcam_buffer_t akbuffer;

    spin_lock(&self->slock);
    akbuffer = akvcam_list_at(self->buffers, buffer->index);

    if (!akbuffer)
        goto akvcam_buffers_queue_failed;

    if (akbuffer->buffer.type != buffer->type)
        goto akvcam_buffers_queue_failed;

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        goto akvcam_buffers_queue_failed;

    switch (buffer->memory) {
    case V4L2_MEMORY_MMAP:
        akbuffer->buffer.flags = buffer->flags;
        akbuffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED;
        akbuffer->buffer.flags &= (__u32) ~V4L2_BUF_FLAG_DONE;

        break;

    case V4L2_MEMORY_USERPTR:
        if (buffer->m.userptr)
            akbuffer->buffer.m.userptr = buffer->m.userptr;

        akbuffer->buffer.flags = buffer->flags;
        akbuffer->buffer.flags |= V4L2_BUF_FLAG_QUEUED;
        akbuffer->buffer.flags &= (__u32) ~(V4L2_BUF_FLAG_MAPPED
                                            | V4L2_BUF_FLAG_DONE);

        break;

    default:
        break;
    }

    memcpy(buffer, &akbuffer->buffer, sizeof(struct v4l2_buffer));
    result = 0;

akvcam_buffers_queue_failed:
    spin_unlock(&self->slock);

    return result;
}

static bool akvcam_buffers_is_done(const akvcam_buffer_t buffer,
                                   const __u32 *flags,
                                   size_t size)
{
    return buffer->buffer.flags & V4L2_BUF_FLAG_DONE;
}

int akvcam_buffers_dequeue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    int result = -EINVAL;
    akvcam_buffer_t akbuffer;
    akvcam_list_element_t it;

    while (!wait_event_interruptible_timeout(self->frame_is_ready,
                                             akvcam_buffers_frame_available(self),
                                             1 * HZ)) {
    }

    spin_lock(&self->slock);
    it = akvcam_list_find(self->buffers,
                          NULL,
                          0,
                          (akvcam_are_equals_t)
                          akvcam_buffers_is_done);

    akbuffer = akvcam_list_element_data(it);

    if (!akbuffer) {
        result = -EAGAIN;

        goto akvcam_buffers_dequeue_failed;
    }

    if (akbuffer->buffer.type != buffer->type)
        goto akvcam_buffers_dequeue_failed;

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        goto akvcam_buffers_dequeue_failed;

    switch (buffer->memory) {
    case V4L2_MEMORY_MMAP:
        akbuffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED;
        akbuffer->buffer.flags &= (__u32) ~(V4L2_BUF_FLAG_DONE
                                            | V4L2_BUF_FLAG_QUEUED);

        break;

    case V4L2_MEMORY_USERPTR:
        if (buffer->m.userptr)
            akbuffer->buffer.m.userptr = buffer->m.userptr;

        akbuffer->buffer.flags &= (__u32) ~(V4L2_BUF_FLAG_MAPPED
                                            | V4L2_BUF_FLAG_DONE
                                            | V4L2_BUF_FLAG_QUEUED);

        break;

    default:
        break;
    }

    memcpy(buffer, &akbuffer->buffer, sizeof(struct v4l2_buffer));
    result = 0;

akvcam_buffers_dequeue_failed:
    spin_unlock(&self->slock);

    return result;
}

void *akvcam_buffers_buffers_data(akvcam_buffers_t self,
                                  struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;

    spin_lock(&self->slock);
    akbuffer = akvcam_list_at(self->buffers, buffer->index);
    spin_unlock(&self->slock);

    return akbuffer? akbuffer->data: NULL;
}

static bool akvcam_buffers_equals_offset(const akvcam_buffer_t buffer,
                                         const __u32 *offset,
                                         size_t size)
{
    return buffer->buffer.m.offset == *offset;
}

void *akvcam_buffers_data(akvcam_buffers_t self, __u32 offset)
{
    void *data;
    akvcam_buffer_t buffer;
    akvcam_list_element_t it;

    spin_lock(&self->slock);
    it = akvcam_list_find(self->buffers,
                          &offset,
                          0,
                          (akvcam_are_equals_t)
                          akvcam_buffers_equals_offset);

    buffer = akvcam_list_element_data(it);
    data = buffer? buffer->data: NULL;
    spin_unlock(&self->slock);

    return data;
}

bool akvcam_buffers_allocated(akvcam_buffers_t self)
{
    return self->main_node != NULL;
}

size_t akvcam_buffers_size_rw(akvcam_buffers_t self)
{
    size_t size;

    spin_lock(&self->slock);
    size = akvcam_rbuffer_n_elements(self->rw_buffers);
    spin_unlock(&self->slock);

    return size;
}

bool akvcam_buffers_resize_rw(akvcam_buffers_t self, size_t size)
{
    akvcam_format_t format;

    if (self->main_node)
        return false;

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return false;

    if (size < 1)
        size = 1;

    spin_lock(&self->slock);
    akvcam_rbuffer_clear(self->rw_buffers);
    format = akvcam_device_format_nr(self->device);
    akvcam_rbuffer_resize(self->rw_buffers,
                          size,
                          akvcam_format_size(format),
                          AKVCAM_RBUFFER_MEMORY_TYPE_VMALLOC);
    self->rw_buffer_size = size;
    spin_unlock(&self->slock);

    return true;
}

ssize_t akvcam_buffers_read_rw(akvcam_buffers_t self,
                               struct akvcam_node *node,
                               void *data,
                               size_t size)
{
    akvcam_format_t format;
    struct v4l2_fract *frame_rate;
    size_t data_size;
    __u32 tsleep;

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return 0;

    format = akvcam_device_format_nr(self->device);
    data_size = akvcam_rbuffer_data_size(self->rw_buffers);

    if (data_size < 1 || data_size % akvcam_format_size(format) == 0) {
        frame_rate = akvcam_format_frame_rate(format);
        tsleep = 1000 * frame_rate->denominator;

        if (frame_rate->numerator)
            tsleep /= frame_rate->numerator;

        msleep_interruptible(tsleep);
    }

    spin_lock(&self->slock);

    if (!akvcam_rbuffer_dequeue_bytes(self->rw_buffers, data, &size, false))
        size = 0;

    spin_unlock(&self->slock);

    return (ssize_t) size;
}

static bool akvcam_buffers_is_queued(const akvcam_buffer_t buffer,
                                     const __u32 *flags,
                                     size_t size)
{
    return !(buffer->buffer.flags & V4L2_BUF_FLAG_DONE)
            && buffer->buffer.flags & V4L2_BUF_FLAG_QUEUED;
}

bool akvcam_buffers_write_frame(akvcam_buffers_t self,
                                struct akvcam_frame *frame)
{
    akvcam_list_element_t it;
    akvcam_buffer_t akbuffer;
    size_t length;
    void *data;
    bool ok = false;

    spin_lock(&self->slock);

    if (self->main_node) {
        it = akvcam_list_find(self->buffers,
                              NULL,
                              0,
                              (akvcam_are_equals_t)
                              akvcam_buffers_is_queued);
        akbuffer = akvcam_list_element_data(it);

        if (akbuffer
            && (akbuffer->buffer.memory == V4L2_MEMORY_MMAP
                || akbuffer->buffer.memory == V4L2_MEMORY_USERPTR)) {
            data = akvcam_frame_data(frame);
            length = akvcam_min((size_t) akbuffer->buffer.length,
                                akvcam_frame_size(frame));

            if (akbuffer->data && data && length > 0)
                memcpy(akbuffer->data, data, length);

            do_gettimeofday(&akbuffer->buffer.timestamp);
            akbuffer->buffer.sequence = self->sequence;
            akbuffer->buffer.flags |= V4L2_BUF_FLAG_DONE;
            ok = true;
        }
    } else if (self->rw_mode & AKVCAM_RW_MODE_READWRITE) {
        akvcam_rbuffer_queue_bytes(self->rw_buffers,
                                   akvcam_frame_data(frame),
                                   akvcam_frame_size(frame));
        ok = true;
    }

    spin_unlock(&self->slock);

    if (ok)
        wake_up_all(&self->frame_is_ready);

    return ok;
}

void akvcam_buffers_notify_frame(akvcam_buffers_t self)
{
    struct v4l2_event event;

    if (self->frame_ready.callback) {
        memset(&event, 0, sizeof(struct v4l2_event));
        event.type = V4L2_EVENT_FRAME_SYNC;
        event.u.frame_sync.frame_sequence = self->sequence;
        self->frame_ready.callback(self->frame_ready.user_data, &event);
    }

    self->sequence++;
}

void akvcam_buffers_reset_sequence(akvcam_buffers_t self)
{
    self->sequence = 0;
}

bool akvcam_buffers_is_supported(akvcam_buffers_t self, enum v4l2_memory type)
{
    return (self->rw_mode & AKVCAM_RW_MODE_MMAP
            && type == V4L2_MEMORY_MMAP)
            || (self->rw_mode & AKVCAM_RW_MODE_USERPTR
                && type == V4L2_MEMORY_USERPTR);
}

bool akvcam_buffers_frame_available(akvcam_buffers_t self)
{
    akvcam_list_element_t it;

    spin_lock(&self->slock);
    it = akvcam_list_find(self->buffers,
                          NULL,
                          0,
                          (akvcam_are_equals_t)
                          akvcam_buffers_is_done);
    spin_unlock(&self->slock);

    return it;
}

void akvcam_buffer_delete(akvcam_buffer_t *buffer)
{
    if (!buffer || !*buffer)
        return;

    if ((*buffer)->data)
        vfree((*buffer)->data);

    kfree(*buffer);
    *buffer = NULL;
}

void akvcam_buffers_set_frame_ready_callback(akvcam_buffers_t self,
                                             akvcam_frame_ready_callback callback)
{
    self->frame_ready = callback;
}
