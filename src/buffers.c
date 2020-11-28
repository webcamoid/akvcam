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
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "buffers.h"
#include "buffer.h"
#include "device.h"
#include "format.h"
#include "frame.h"
#include "list.h"
#include "log.h"
#include "node.h"
#include "rbuffer.h"

typedef akvcam_list_tt(akvcam_buffer_t) akvcam_buffers_list_t;

struct akvcam_buffers
{
    struct kref ref;
    akvcam_buffers_list_t buffers;
    akvcam_rbuffer_tt(char) rw_buffers;
    enum v4l2_buf_type type;
    struct mutex mtx;
    akvcam_frame_ready_callback frame_ready;
    akvcam_frame_written_callback frame_written;
    akvcam_node_t main_node;
    akvcam_format_t format;
    wait_queue_head_t frame_is_ready;
    size_t rw_buffer_size;
    AKVCAM_RW_MODE rw_mode;
    __u32 sequence;
    bool multiplanar;
    bool horizontal_flip;   // Controlled by capture
    bool vertical_flip;
    bool horizontal_mirror; // Controlled by output
    bool vertical_mirror;
    AKVCAM_SCALING scaling;
    AKVCAM_ASPECT_RATIO aspect_ratio;
    bool swap_rgb;
    int brightness;
    int contrast;
    int gamma;
    int saturation;
    int hue;
    bool gray;
};

bool akvcam_buffers_is_supported(const akvcam_buffers_t self,
                                 enum v4l2_memory type);
bool akvcam_buffers_frame_available(const akvcam_buffers_t self);
akvcam_frame_t akvcam_buffers_frame_apply_adjusts(const akvcam_buffers_t self,
                                                  akvcam_frame_t frame);

akvcam_buffers_t akvcam_buffers_new(AKVCAM_RW_MODE rw_mode,
                                    enum v4l2_buf_type type,
                                    bool multiplanar)
{
    akvcam_buffers_t self = kzalloc(sizeof(struct akvcam_buffers), GFP_KERNEL);

    kref_init(&self->ref);
    self->buffers = akvcam_list_new();
    self->rw_buffers = akvcam_rbuffer_new();
    mutex_init(&self->mtx);
    self->rw_mode = rw_mode;
    self->rw_buffer_size = AKVCAM_BUFFERS_MIN;
    init_waitqueue_head(&self->frame_is_ready);
    self->multiplanar = multiplanar;
    self->format = akvcam_format_new(0, 0, 0, NULL);
    self->type = type;

    return self;
}

void akvcam_buffers_free(struct kref *ref)
{
    akvcam_buffers_t self = container_of(ref, struct akvcam_buffers, ref);
    akvcam_format_delete(self->format);
    akvcam_rbuffer_delete(self->rw_buffers);
    akvcam_list_delete(self->buffers);
    kfree(self);
}

void akvcam_buffers_delete(akvcam_buffers_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_buffers_free);
}

akvcam_buffers_t akvcam_buffers_ref(akvcam_buffers_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_buffers_copy_shared_properties(akvcam_buffers_t self,
                                           akvcam_buffers_t other)
{
    self->horizontal_mirror = other->horizontal_flip;
    self->vertical_mirror = other->vertical_flip;
    self->scaling = other->scaling;
    self->aspect_ratio = other->aspect_ratio;
    self->swap_rgb = other->swap_rgb;
}

akvcam_format_t akvcam_buffers_format_nr(akvcam_buffers_t self)
{
    return self->format;
}

akvcam_format_t akvcam_buffers_format(akvcam_buffers_t self)
{
    return akvcam_format_ref(self->format);
}

void akvcam_buffers_set_format(akvcam_buffers_t self, akvcam_format_t format)
{
    akvcam_format_copy(self->format, format);
}

int akvcam_buffers_allocate(akvcam_buffers_t self,
                            akvcam_node_t node,
                            struct v4l2_requestbuffers *params)
{
    size_t i;
    akvcam_buffer_t buffer;
    struct v4l2_buffer *v4l2_buff;
    size_t buffer_length;
    __u32 buffer_size;

    memset(params->reserved, 0, 2 * sizeof(__u32));

    if (!akvcam_buffers_is_supported(self, params->memory))
        return -EINVAL;

    if (params->type != self->type)
        return -EINVAL;

    if (self->main_node && self->main_node != node)
        return -EBUSY;

    if (mutex_lock_interruptible(&self->mtx))
        return -EINTR;

    akvcam_list_clear(self->buffers);
    mutex_unlock(&self->mtx);

    if (params->count < 1) {
        self->main_node = NULL;
        akvcam_buffers_resize_rw(self, self->rw_buffer_size);
    } else {
        self->main_node = node;
        buffer_length = akvcam_format_size(self->format);
        buffer_size = (__u32) PAGE_ALIGN(buffer_length);

        for (i = 0; i < params->count; i++) {
            buffer = akvcam_buffer_new(buffer_length);
            akvcam_buffer_set_offset(buffer, (__u32) i * buffer_size);
            v4l2_buff = akvcam_buffer_get(buffer);
            v4l2_buff->index = (__u32) i;
            v4l2_buff->type = params->type;
            v4l2_buff->memory = params->memory;
            v4l2_buff->bytesused = (__u32) buffer_length;
            v4l2_buff->flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            v4l2_buff->field = V4L2_FIELD_NONE;

            if (self->multiplanar)
                v4l2_buff->length = (__u32) akvcam_format_planes(self->format);
            else
                v4l2_buff->length = v4l2_buff->bytesused;

            if (params->memory == V4L2_MEMORY_MMAP && !self->multiplanar) {
                v4l2_buff->flags |= V4L2_BUF_FLAG_MAPPED
                                 |  V4L2_BUF_FLAG_QUEUED;
                v4l2_buff->m.offset = akvcam_buffer_offset(buffer);
            }

            if (!mutex_lock_interruptible(&self->mtx)) {
                akvcam_list_push_back(self->buffers,
                                      buffer,
                                      (akvcam_copy_t) akvcam_buffer_ref,
                                      (akvcam_delete_t) akvcam_buffer_delete);
                mutex_unlock(&self->mtx);
            }

            akvcam_buffer_delete(buffer);
        }
    }

    return 0;
}

void akvcam_buffers_deallocate(akvcam_buffers_t self, akvcam_node_t node)
{
    if (node && node == self->main_node) {
        self->main_node = NULL;

        if (!mutex_lock_interruptible(&self->mtx)) {
            akvcam_list_clear(self->buffers);
            mutex_unlock(&self->mtx);
        }

        akvcam_buffers_resize_rw(self, self->rw_buffer_size);
    }
}

int akvcam_buffers_create(akvcam_buffers_t self,
                          akvcam_node_t node,
                          struct v4l2_create_buffers *buffers,
                          akvcam_format_t format)
{
    size_t i;
    akvcam_buffer_t buffer;
    akvcam_buffer_t last_buffer;
    struct v4l2_buffer *v4l2_buff;
    size_t buffer_length;
    __u32 buffer_size;
    __u32 offset = 0;

    buffers->index = (__u32) akvcam_list_size(self->buffers);
    memset(buffers->reserved, 0, 8 * sizeof(__u32));

    if (!akvcam_buffers_is_supported(self, buffers->memory))
        return -EINVAL;

    if (buffers->format.type != self->type)
        return -EINVAL;

    if (!format)
        return -EINVAL;

    if (self->main_node && self->main_node != node)
        return -EBUSY;

    if (buffers->count > 0) {
        if (!self->main_node)
            buffers->index = 0;

        buffer_length = akvcam_format_size(format);
        buffer_size = (__u32) PAGE_ALIGN(buffer_length);

        if (mutex_lock_interruptible(&self->mtx))
            return -EINTR;

        last_buffer = akvcam_list_back(self->buffers);
        v4l2_buff = akvcam_buffer_get(last_buffer);

        if (last_buffer)
            offset = v4l2_buff->m.offset + PAGE_ALIGN(v4l2_buff->length);

        mutex_unlock(&self->mtx);
        self->main_node = node;

        for (i = 0; i < buffers->count; i++) {
            buffer = akvcam_buffer_new(buffer_length);
            akvcam_buffer_set_offset(buffer, offset + (__u32) i * buffer_size);
            v4l2_buff = akvcam_buffer_get(buffer);
            v4l2_buff->index = buffers->index + (__u32) i;
            v4l2_buff->type = self->type;
            v4l2_buff->memory = buffers->memory;
            v4l2_buff->bytesused = (__u32) buffer_length;
            v4l2_buff->flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            v4l2_buff->field = V4L2_FIELD_NONE;

            if (self->multiplanar)
                v4l2_buff->length = (__u32) akvcam_format_planes(format);
            else
                v4l2_buff->length = v4l2_buff->bytesused;

            if (buffers->memory == V4L2_MEMORY_MMAP && !self->multiplanar) {
                v4l2_buff->flags |= V4L2_BUF_FLAG_MAPPED
                                 |  V4L2_BUF_FLAG_QUEUED;
                v4l2_buff->m.offset = akvcam_buffer_offset(buffer);
            }

            if (!mutex_lock_interruptible(&self->mtx)) {
                akvcam_list_push_back(self->buffers,
                                      buffer,
                                      (akvcam_copy_t) akvcam_buffer_ref,
                                      (akvcam_delete_t) akvcam_buffer_delete);
                mutex_unlock(&self->mtx);
            }

            akvcam_buffer_delete(buffer);
        }
    }

    return 0;
}

bool akvcam_buffers_fill(const akvcam_buffers_t self,
                         struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_buffer *v4l2_buff;
    bool ok = false;

    if (!self->main_node)
        return false;

    if (mutex_lock_interruptible(&self->mtx))
        return false;

    akbuffer = akvcam_list_at(self->buffers, buffer->index);

    if (akbuffer) {
        v4l2_buff = akvcam_buffer_get(akbuffer);

        if (v4l2_buff->type == buffer->type) {
            if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                || buffer->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
                v4l2_buff->length = buffer->length;
                v4l2_buff->m.planes = buffer->m.planes;
            }

            memcpy(buffer, v4l2_buff, sizeof(struct v4l2_buffer));
            buffer->flags &= (__u32) ~(V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE);
            ok = true;
        }
    }

    mutex_unlock(&self->mtx);

    return ok;
}

int akvcam_buffers_queue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_buffer *v4l2_buff;
    int result = -EINVAL;

    if (mutex_lock_interruptible(&self->mtx))
        return -EINTR;

    akbuffer = akvcam_list_at(self->buffers, buffer->index);
    v4l2_buff = akvcam_buffer_get(akbuffer);

    if (!akbuffer)
        goto akvcam_buffers_queue_failed;

    if (v4l2_buff->type != buffer->type)
        goto akvcam_buffers_queue_failed;

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        goto akvcam_buffers_queue_failed;

    if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
        || buffer->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        v4l2_buff->length = buffer->length;
        v4l2_buff->m.planes = buffer->m.planes;
    }

    switch (buffer->memory) {
    case V4L2_MEMORY_MMAP:
        v4l2_buff->flags = buffer->flags;
        v4l2_buff->flags |= V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED;
        v4l2_buff->flags &= (__u32) ~V4L2_BUF_FLAG_DONE;

        break;

    case V4L2_MEMORY_USERPTR:
        if (buffer->m.userptr && !self->multiplanar)
            v4l2_buff->m.userptr = buffer->m.userptr;

        v4l2_buff->flags = buffer->flags;
        v4l2_buff->flags |= V4L2_BUF_FLAG_QUEUED;
        v4l2_buff->flags &= (__u32) ~(V4L2_BUF_FLAG_MAPPED
                                      | V4L2_BUF_FLAG_DONE);

        break;

    default:
        break;
    }

    memcpy(buffer, v4l2_buff, sizeof(struct v4l2_buffer));
    result = 0;

akvcam_buffers_queue_failed:
    mutex_unlock(&self->mtx);

    if (!result
        && akvcam_device_type_from_v4l2(self->type) == AKVCAM_DEVICE_TYPE_OUTPUT
        && buffer->memory == V4L2_MEMORY_MMAP) {
        akvcam_buffers_process_frame(self, buffer);
    }

    return result;
}

static bool akvcam_buffers_is_ready(const akvcam_buffer_t buffer,
                                    const __u32 *flags)
{
    return akvcam_buffer_get(buffer)->flags & *flags;
}

int akvcam_buffers_dequeue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_buffer *v4l2_buff;
    struct v4l2_fract *frame_rate;
    akvcam_list_element_t it;
    __u32 tsleep;
    __u32 flags;
    int result = -EINVAL;

    if (self->type == V4L2_BUF_TYPE_VIDEO_OUTPUT
        || self->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        flags = V4L2_BUF_FLAG_QUEUED;

        if (!akvcam_node_non_blocking(self->main_node)) {
            frame_rate = akvcam_format_frame_rate(self->format);
            tsleep = 1000 * frame_rate->denominator;

            if (frame_rate->numerator)
                tsleep /= frame_rate->numerator;

            msleep_interruptible(tsleep);
        }
    } else {
        flags = V4L2_BUF_FLAG_DONE;

        while (!wait_event_interruptible_timeout(self->frame_is_ready,
                                                 akvcam_buffers_frame_available(self),
                                                 1 * HZ)) {
        }
    }

    if (mutex_lock_interruptible(&self->mtx))
        return -EINTR;

    it = akvcam_list_find(self->buffers,
                          &flags,
                          (akvcam_are_equals_t) akvcam_buffers_is_ready);
    akbuffer = akvcam_list_element_data(it);
    v4l2_buff = akvcam_buffer_get(akbuffer);

    if (!akbuffer) {
        result = -EAGAIN;

        goto akvcam_buffers_dequeue_failed;
    }

    if (v4l2_buff->type != buffer->type)
        goto akvcam_buffers_dequeue_failed;

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        goto akvcam_buffers_dequeue_failed;

    if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
        || buffer->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        v4l2_buff->length = buffer->length;
        v4l2_buff->m.planes = buffer->m.planes;
    }

    switch (buffer->memory) {
    case V4L2_MEMORY_MMAP:
        v4l2_buff->flags |= V4L2_BUF_FLAG_MAPPED;
        v4l2_buff->flags &= (__u32) ~(V4L2_BUF_FLAG_DONE
                                      | V4L2_BUF_FLAG_QUEUED);

        break;

    case V4L2_MEMORY_USERPTR:
        if (buffer->m.userptr && !self->multiplanar)
            v4l2_buff->m.userptr = buffer->m.userptr;

        v4l2_buff->flags &= (__u32) ~(V4L2_BUF_FLAG_MAPPED
                                      | V4L2_BUF_FLAG_DONE
                                      | V4L2_BUF_FLAG_QUEUED);

        break;

    default:
        break;
    }

    if (self->type == V4L2_BUF_TYPE_VIDEO_OUTPUT
        || self->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        akvcam_get_timestamp(&v4l2_buff->timestamp);
        v4l2_buff->sequence = self->sequence;
        self->sequence++;
    }

    memcpy(buffer, v4l2_buff, sizeof(struct v4l2_buffer));
    result = 0;

akvcam_buffers_dequeue_failed:
    mutex_unlock(&self->mtx);

    return result;
}

void *akvcam_buffers_buffers_data(const akvcam_buffers_t self,
                                  const struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;

    if (mutex_lock_interruptible(&self->mtx))
        return NULL;

    akbuffer = akvcam_list_at(self->buffers, buffer->index);
    mutex_unlock(&self->mtx);

    return akvcam_buffer_data(akbuffer);
}

static bool akvcam_buffers_equals_offset(const akvcam_buffer_t buffer,
                                         const __u32 *offset)
{
    size_t buffer_size = akvcam_buffer_size(buffer);
    __u32 buffer_offset = akvcam_buffer_offset(buffer);

    return akvcam_between(buffer_offset,
                          *offset,
                          buffer_offset + buffer_size);
}

void *akvcam_buffers_data(const akvcam_buffers_t self, __u32 offset)
{
    void *data;
    akvcam_buffer_t buffer;
    akvcam_list_element_t it;

    if (mutex_lock_interruptible(&self->mtx))
        return NULL;

    it = akvcam_list_find(self->buffers,
                          &offset,
                          (akvcam_are_equals_t) akvcam_buffers_equals_offset);

    buffer = akvcam_list_element_data(it);
    data = akvcam_buffer_data(buffer);
    mutex_unlock(&self->mtx);

    return data;
}

bool akvcam_buffers_allocated(const akvcam_buffers_t self)
{
    return self->main_node != NULL;
}

size_t akvcam_buffers_size_rw(const akvcam_buffers_t self)
{
    size_t size = 0;

    if (!mutex_lock_interruptible(&self->mtx)) {
        size = akvcam_rbuffer_n_elements(self->rw_buffers);
        mutex_unlock(&self->mtx);
    }

    return size;
}

bool akvcam_buffers_resize_rw(akvcam_buffers_t self, size_t size)
{
    if (self->main_node)
        return false;

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return false;

    if (size < 1)
        size = 1;

    if (mutex_lock_interruptible(&self->mtx))
        return false;

    akvcam_rbuffer_clear(self->rw_buffers);
    akvcam_rbuffer_resize(self->rw_buffers,
                          size,
                          akvcam_format_size(self->format),
                          AKVCAM_MEMORY_TYPE_VMALLOC);
    self->rw_buffer_size = size;
    mutex_unlock(&self->mtx);

    return true;
}

ssize_t akvcam_buffers_read_rw(akvcam_buffers_t self,
                               akvcam_node_t node,
                               void *data,
                               size_t size)
{
    struct v4l2_fract *frame_rate;
    size_t data_size;
    size_t format_size;
    __u32 tsleep;

    UNUSED(node);

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return 0;

    data_size = akvcam_rbuffer_data_size(self->rw_buffers);
    format_size = akvcam_format_size(self->format);

    if (data_size < 1 || data_size % format_size == 0) {
        frame_rate = akvcam_format_frame_rate(self->format);
        tsleep = 1000 * frame_rate->denominator;

        if (frame_rate->numerator)
            tsleep /= frame_rate->numerator;

        msleep_interruptible(tsleep);
    }

    if (mutex_lock_interruptible(&self->mtx))
        return 0;

    if (!akvcam_rbuffer_dequeue_bytes(self->rw_buffers, data, &size, false))
        size = 0;

    mutex_unlock(&self->mtx);

    return (ssize_t) size;
}

ssize_t akvcam_buffers_write_rw(akvcam_buffers_t self,
                                akvcam_node_t node,
                                const void *data,
                                size_t size)
{
    akvcam_frame_t frame;
    struct v4l2_fract *frame_rate;
    size_t data_size;
    size_t format_size;
    size_t read_size;
    __u32 tsleep;

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return 0;

    if (mutex_lock_interruptible(&self->mtx))
        return 0;

    akvcam_rbuffer_queue_bytes(self->rw_buffers, data, size);
    mutex_unlock(&self->mtx);

    format_size = akvcam_format_size(self->format);

    if (self->frame_written.callback) {
        frame = akvcam_frame_new(self->format, NULL, 0);

        while (akvcam_rbuffer_data_size(self->rw_buffers) >= format_size) {
            read_size = format_size;
            akvcam_rbuffer_dequeue_bytes(self->rw_buffers,
                                         akvcam_frame_data(frame),
                                         &read_size,
                                         false);
            self->frame_written.callback(self->frame_written.user_data, frame);
        }

        akvcam_frame_delete(frame);
    }

    if (!akvcam_node_non_blocking(node)) {
        data_size = akvcam_rbuffer_data_size(self->rw_buffers);

        if (data_size < 1 || data_size % format_size == 0) {
            frame_rate = akvcam_format_frame_rate(self->format);
            tsleep = 1000 * frame_rate->denominator;

            if (frame_rate->numerator)
                tsleep /= frame_rate->numerator;

            msleep_interruptible(tsleep);
        }
    }

    return (ssize_t) size;
}

static bool akvcam_buffers_is_queued(const akvcam_buffer_t buffer,
                                     const __u32 *flags)
{
    struct v4l2_buffer *v4l2_buff = akvcam_buffer_get(buffer);
    UNUSED(flags);

    return !(v4l2_buff->flags & V4L2_BUF_FLAG_DONE)
            && v4l2_buff->flags & V4L2_BUF_FLAG_QUEUED;
}

bool akvcam_buffers_write_frame(akvcam_buffers_t self,
                                akvcam_frame_t frame)
{
    akvcam_list_element_t it;
    akvcam_buffer_t buffer;
    struct v4l2_buffer *v4l2_buff;
    size_t length;
    void *data;
    bool ok = false;
    akvcam_frame_t adjusted_frame =
            akvcam_buffers_frame_apply_adjusts(self, frame);

    if (mutex_lock_interruptible(&self->mtx))
        return false;

    if (self->main_node) {
        it = akvcam_list_find(self->buffers,
                              NULL,
                              (akvcam_are_equals_t) akvcam_buffers_is_queued);
        buffer = akvcam_list_element_data(it);
        v4l2_buff = akvcam_buffer_get(buffer);

        if (buffer
            && (v4l2_buff->memory == V4L2_MEMORY_MMAP
                || v4l2_buff->memory == V4L2_MEMORY_USERPTR)) {
            data = akvcam_frame_data(adjusted_frame);
            length = akvcam_min((size_t) v4l2_buff->length,
                                akvcam_frame_size(adjusted_frame));

            if (data && length > 0)
                memcpy(akvcam_buffer_data(buffer), data, length);

            akvcam_get_timestamp(&v4l2_buff->timestamp);
            v4l2_buff->sequence = self->sequence;
            v4l2_buff->flags |= V4L2_BUF_FLAG_DONE;
            ok = true;
        }
    } else if (self->rw_mode & AKVCAM_RW_MODE_READWRITE) {
        akvcam_rbuffer_queue_bytes(self->rw_buffers,
                                   akvcam_frame_data(adjusted_frame),
                                   akvcam_frame_size(adjusted_frame));
        ok = true;
    }

    mutex_unlock(&self->mtx);
    akvcam_frame_delete(adjusted_frame);

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

void akvcam_buffers_process_frame(const akvcam_buffers_t self,
                                  struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_buffer *v4l2_buff;
    akvcam_frame_t frame;

    akpr_function();

    if (self->frame_written.callback) {
        akbuffer = akvcam_list_at(self->buffers, buffer->index);

        if (!akbuffer)
            return;

        v4l2_buff = akvcam_buffer_get(akbuffer);
        frame = akvcam_frame_new(self->format,
                                 akvcam_buffer_data(akbuffer),
                                 v4l2_buff->length);
        self->frame_written.callback(self->frame_written.user_data, frame);
        akvcam_frame_delete(frame);
    }
}

void akvcam_buffers_reset_sequence(akvcam_buffers_t self)
{
    self->sequence = 0;
}

bool akvcam_buffers_is_supported(const akvcam_buffers_t self,
                                 enum v4l2_memory type)
{
    return (self->rw_mode & AKVCAM_RW_MODE_MMAP
            && type == V4L2_MEMORY_MMAP)
            || (self->rw_mode & AKVCAM_RW_MODE_USERPTR
                && type == V4L2_MEMORY_USERPTR);
}

bool akvcam_buffers_frame_available(const akvcam_buffers_t self)
{
    akvcam_list_element_t it;
    __u32 flags = V4L2_BUF_FLAG_DONE;

    if (mutex_lock_interruptible(&self->mtx))
        return false;

    it = akvcam_list_find(self->buffers,
                          &flags,
                          (akvcam_are_equals_t) akvcam_buffers_is_ready);
    mutex_unlock(&self->mtx);

    return it != NULL;
}

akvcam_frame_t akvcam_buffers_frame_apply_adjusts(const akvcam_buffers_t self,
                                                  akvcam_frame_t frame)
{
    bool horizontal_flip = self->horizontal_flip != self->horizontal_mirror;
    bool vertical_flip = self->vertical_flip != self->vertical_mirror;

    akvcam_format_t frame_format = akvcam_frame_format_nr(frame);

    __u32 fourcc = akvcam_format_fourcc(self->format);
    size_t iwidth = akvcam_format_width(frame_format);
    size_t iheight = akvcam_format_height(frame_format);
    size_t owidth = akvcam_format_width(self->format);
    size_t oheight = akvcam_format_height(self->format);

    akvcam_frame_t new_frame = akvcam_frame_new(NULL, NULL, 0);
    akvcam_frame_copy(new_frame, frame);

    if (owidth * oheight > iwidth * iheight) {
        akvcam_frame_mirror(new_frame,
                            horizontal_flip,
                            vertical_flip);

        if (self->swap_rgb)
            akvcam_frame_swap_rgb(new_frame);

        akvcam_frame_adjust(new_frame,
                            self->hue,
                            self->saturation,
                            self->brightness,
                            self->contrast,
                            self->gamma,
                            self->gray);
        akvcam_frame_scaled(new_frame,
                            owidth,
                            oheight,
                            self->scaling,
                            self->aspect_ratio);
        akvcam_frame_convert(new_frame, fourcc);
    } else {
        akvcam_frame_scaled(new_frame,
                            owidth,
                            oheight,
                            self->scaling,
                            self->aspect_ratio);
        akvcam_frame_mirror(new_frame,
                            horizontal_flip,
                            vertical_flip);

        if (self->swap_rgb)
            akvcam_frame_swap_rgb(new_frame);

        akvcam_frame_adjust(new_frame,
                            self->hue,
                            self->saturation,
                            self->brightness,
                            self->contrast,
                            self->gamma,
                            self->gray);
        akvcam_frame_convert(new_frame, fourcc);
    }

    return new_frame;
}

void akvcam_buffers_set_properties(akvcam_buffers_t self,
                                     const struct v4l2_event *event)
{
    switch (event->id) {
    case V4L2_CID_BRIGHTNESS:
        self->brightness = event->u.ctrl.value;
        break;

    case V4L2_CID_CONTRAST:
        self->contrast = event->u.ctrl.value;
        break;

    case V4L2_CID_SATURATION:
        self->saturation = event->u.ctrl.value;
        break;

    case V4L2_CID_HUE:
        self->hue = event->u.ctrl.value;
        break;

    case V4L2_CID_GAMMA:
        self->gamma = event->u.ctrl.value;
        break;

    case V4L2_CID_HFLIP:
        self->horizontal_flip = event->u.ctrl.value;
        break;

    case V4L2_CID_VFLIP:
        self->vertical_flip = event->u.ctrl.value;
        break;

    case V4L2_CID_COLORFX:
        self->gray = event->u.ctrl.value == V4L2_COLORFX_BW;
        break;

    case AKVCAM_CID_SCALING:
        self->scaling = (AKVCAM_SCALING) event->u.ctrl.value;
        break;

    case AKVCAM_CID_ASPECT_RATIO:
        self->aspect_ratio = (AKVCAM_ASPECT_RATIO) event->u.ctrl.value;
        break;

    case AKVCAM_CID_SWAP_RGB:
        self->swap_rgb = event->u.ctrl.value;
        break;

    default:
        break;
    }
}

void akvcam_buffers_set_frame_ready_callback(akvcam_buffers_t self,
                                             const akvcam_frame_ready_callback callback)
{
    self->frame_ready = callback;
}

void akvcam_buffers_set_frame_written_callback(akvcam_buffers_t self,
                                               const akvcam_frame_written_callback callback)
{
    self->frame_written = callback;
}
