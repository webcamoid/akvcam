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

#include <linux/mm.h>
#include <linux/slab.h>
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
    akvcam_rbuffer_tt(akvcam_buffer_t | char) buffers;
    akvcam_frame_ready_callback frame_ready;
    akvcam_node_t main_node;
    AKVCAM_RW_MODE rw_mode;
    __u32 sequence;
};

bool akvcam_buffers_is_supported(akvcam_buffers_t self, enum v4l2_memory type);
void akvcam_buffer_delete(akvcam_buffer_t *buffer);

akvcam_buffers_t akvcam_buffers_new(struct akvcam_device *device)
{
    akvcam_buffers_t self = kzalloc(sizeof(struct akvcam_buffers), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_buffers_delete);
    self->buffers = akvcam_rbuffer_new();
    self->device = device;
    self->type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;
    self->rw_mode = akvcam_device_rw_mode(device);

    return self;
}

void akvcam_buffers_delete(akvcam_buffers_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_rbuffer_delete(&((*self)->buffers));
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

    akvcam_rbuffer_resize(self->buffers,
                          params->count,
                          sizeof(akvcam_buffer),
                          AKVCAM_RBUFFER_MEMORY_TYPE_KMALLOC);

    if (params->count < 1) {
        self->main_node = NULL;
        akvcam_buffers_resize_rw(self, 1);

        return 0;
    }

    self->main_node = node;
    format = akvcam_device_format_nr(self->device);
    buffer_length = akvcam_format_size(format);
    buffer_size = (__u32) PAGE_ALIGN(buffer_length);

    for (i = 0; i < params->count; i++) {
        buffer = akvcam_rbuffer_queue(self->buffers, NULL);
        buffer->buffer.index = (__u32) i;
        buffer->buffer.type = params->type;
        buffer->buffer.memory = params->memory;
        buffer->buffer.bytesused = (__u32) buffer_length;
        buffer->buffer.length = buffer->buffer.bytesused;
        buffer->buffer.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
        buffer->buffer.field = V4L2_FIELD_NONE;

        if (params->memory == V4L2_MEMORY_MMAP) {
            buffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED;
            buffer->buffer.m.offset =
                    buffer->buffer.index * buffer_size;
            buffer->data = vzalloc(buffer->buffer.length);
        }
    }

    return 0;
}

void akvcam_buffers_deallocate(akvcam_buffers_t self, struct akvcam_node *node)
{
    if (node && node == self->main_node) {
        self->main_node = NULL;
        akvcam_buffers_resize_rw(self, 1);
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

    buffers->index = (__u32) akvcam_rbuffer_n_elements(self->buffers);
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

    if (!self->main_node)
        buffers->index = 0;

    akvcam_rbuffer_resize(self->buffers,
                          buffers->count + buffers->index,
                          sizeof(akvcam_buffer),
                          AKVCAM_RBUFFER_MEMORY_TYPE_KMALLOC);

    if (buffers->count < 1)
        return 0;

    self->main_node = node;
    buffer_length = akvcam_format_size(format);
    buffer_size = (__u32) PAGE_ALIGN(buffer_length);
    last_buffer = akvcam_rbuffer_ptr_back(self->buffers);

    if (last_buffer)
        offset = last_buffer->buffer.m.offset
               + PAGE_ALIGN(last_buffer->buffer.length);

    for (i = 0; i < buffers->count; i++) {
        buffer = akvcam_rbuffer_queue(self->buffers, NULL);
        buffer->buffer.index = buffers->index + (__u32) i;
        buffer->buffer.type = self->type;
        buffer->buffer.memory = buffers->memory;
        buffer->buffer.bytesused = (__u32) buffer_length;
        buffer->buffer.length = buffer->buffer.bytesused;
        buffer->buffer.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
        buffer->buffer.field = V4L2_FIELD_NONE;

        if (buffers->memory == V4L2_MEMORY_MMAP) {
            buffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED;
            buffer->buffer.m.offset = offset + (__u32) i * buffer_size;
            buffer->data = vzalloc(buffer->buffer.length);
        }
    }

    return 0;
}

bool akvcam_buffers_fill(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;

    if (!self->main_node)
        return false;

    akbuffer = akvcam_rbuffer_ptr_at(self->buffers, buffer->index);

    if (!akbuffer)
        return false;

    if (akbuffer->buffer.type != buffer->type)
        return false;

    memcpy(buffer, &akbuffer->buffer, sizeof(struct v4l2_buffer));

    return true;
}

int akvcam_buffers_queue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer =
            akvcam_rbuffer_ptr_at(self->buffers, buffer->index);

    if (!akbuffer)
        return -EINVAL;

    if (akbuffer->buffer.type != buffer->type)
        return -EINVAL;

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        return -EINVAL;

    switch (buffer->memory) {
    case V4L2_MEMORY_MMAP:
        akbuffer->buffer.flags = buffer->flags;
        akbuffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED;
        akbuffer->buffer.flags &= (__u32) ~V4L2_BUF_FLAG_DONE;

        break;

    case V4L2_MEMORY_USERPTR:
        akbuffer->buffer.m.userptr = buffer->m.userptr;
        akbuffer->buffer.length = buffer->length;
        akbuffer->buffer.flags = buffer->flags;
        akbuffer->buffer.flags |= V4L2_BUF_FLAG_QUEUED;
        akbuffer->buffer.flags &= (__u32) ~(V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE);

        break;

    default:
        break;
    }

    memcpy(buffer, &akbuffer->buffer, sizeof(struct v4l2_buffer));

    return 0;
}

int akvcam_buffers_dequeue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer =
            akvcam_rbuffer_dequeue(self->buffers, NULL, true, false);

    if (!akbuffer)
        return -EAGAIN;

    if (akbuffer->buffer.type != buffer->type)
        return -EINVAL;

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        return -EINVAL;

    switch (buffer->memory) {
    case V4L2_MEMORY_MMAP:
        akbuffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED
                               |  V4L2_BUF_FLAG_QUEUED
                               |  V4L2_BUF_FLAG_DONE
                               |  V4L2_BUF_FLAG_KEYFRAME;

        break;

    case V4L2_MEMORY_USERPTR:
        akbuffer->buffer.flags |= V4L2_BUF_FLAG_QUEUED
                               |  V4L2_BUF_FLAG_DONE
                               |  V4L2_BUF_FLAG_KEYFRAME;

        break;

    default:
        break;
    }

    memcpy(buffer, &akbuffer->buffer, sizeof(struct v4l2_buffer));

    return 0;
}

bool akvcam_buffers_equals_offset(const akvcam_buffer_t buffer,
                                  const __u32 *offset,
                                  size_t size)
{
    return buffer->buffer.m.offset == *offset;
}

void *akvcam_buffers_data(akvcam_buffers_t self, __u32 offset)
{
    akvcam_buffer_t buffer =
            akvcam_rbuffer_find(self->buffers,
                                &offset,
                                0,
                                (akvcam_are_equals_t)
                                akvcam_buffers_equals_offset);

    if (!buffer)
        return NULL;

    return buffer->data;
}

bool akvcam_buffers_allocated(akvcam_buffers_t self)
{
    return self->main_node != NULL;
}

size_t akvcam_buffers_size(akvcam_buffers_t self)
{
    return akvcam_rbuffer_n_elements(self->buffers);
}

bool akvcam_buffers_resize_rw(akvcam_buffers_t self, size_t size)
{
    akvcam_format_t format;

    if (self->main_node)
        return false;

    if (size < 1)
        size = 1;

    akvcam_rbuffer_clear(self->buffers);
    format = akvcam_device_format_nr(self->device);
    akvcam_rbuffer_resize(self->buffers,
                          size,
                          akvcam_format_size(format),
                          AKVCAM_RBUFFER_MEMORY_TYPE_VMALLOC);

    return true;
}

ssize_t akvcam_buffers_read_rw(akvcam_buffers_t self, void *data, size_t size)
{
    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return 0;

    if (!akvcam_rbuffer_dequeue_bytes(self->buffers, data, &size, false, true))
        return 0;

    return (ssize_t) size;
}

void akvcam_buffers_write_frame(akvcam_buffers_t self,
                                struct akvcam_frame *frame)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_event event;
    size_t length;
    void *data;
    bool queued = false;
    __u32 sequence = self->sequence;

    if (self->main_node) {
        akbuffer = akvcam_rbuffer_queue(self->buffers, NULL);

        switch (akbuffer->buffer.memory) {
        case V4L2_MEMORY_MMAP:
            data = akvcam_frame_data(frame);
            length = min((size_t) akbuffer->buffer.length,
                         akvcam_frame_size(frame));

            if (akbuffer->data && data && length > 0)
                memcpy(akbuffer->data, data, length);

            do_gettimeofday(&akbuffer->buffer.timestamp);
            akbuffer->buffer.sequence = sequence;
            queued = true;

            break;

        case V4L2_MEMORY_USERPTR:
            data = akvcam_frame_data(frame);
            length = min((size_t) akbuffer->buffer.length,
                         akvcam_frame_size(frame));

            if (akbuffer->buffer.m.userptr && data && length > 0)
                copy_to_user((void *) akbuffer->buffer.m.userptr, data, length);

            do_gettimeofday(&akbuffer->buffer.timestamp);
            akbuffer->buffer.sequence = sequence;
            queued = true;

            break;

        default:
            break;
        }
    } else if (self->rw_mode & AKVCAM_RW_MODE_READWRITE) {
        akvcam_rbuffer_queue_bytes(self->buffers,
                                   akvcam_frame_data(frame),
                                   akvcam_frame_size(frame));
        queued = true;
    }

    if (queued) {
        if (self->frame_ready.callback) {
            memset(&event, 0, sizeof(struct v4l2_event));
            event.type = V4L2_EVENT_FRAME_SYNC;
            event.u.frame_sync.frame_sequence = sequence;
            self->frame_ready.callback(self->frame_ready.user_data, &event);
        }

        self->sequence++;
    }
}

bool akvcam_buffers_is_supported(akvcam_buffers_t self, enum v4l2_memory type)
{
    return (self->rw_mode & AKVCAM_RW_MODE_MMAP
            && type == V4L2_MEMORY_MMAP)
            || (self->rw_mode & AKVCAM_RW_MODE_USERPTR
                && type == V4L2_MEMORY_USERPTR);
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
