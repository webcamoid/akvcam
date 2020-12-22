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
#include "rbuffer.h"

struct akvcam_buffers
{
    struct kref ref;
    akvcam_list_tt(akvcam_buffer_t) buffers;
    akvcam_rbuffer_tt(char) rw_buffers;
    struct mutex buffers_mutex;
    enum v4l2_buf_type type;
    akvcam_format_t format;
    wait_queue_head_t buffers_not_full;
    wait_queue_head_t buffers_not_empty;
    bool blocking;
    size_t rw_buffer_size;
    AKVCAM_RW_MODE rw_mode;
    __u32 sequence;
    bool multiplanar;
};

bool akvcam_buffers_is_supported(const akvcam_buffers_t self,
                                 enum v4l2_memory type);

akvcam_buffers_t akvcam_buffers_new(AKVCAM_RW_MODE rw_mode,
                                    enum v4l2_buf_type type,
                                    bool multiplanar)
{
    akvcam_buffers_t self = kzalloc(sizeof(struct akvcam_buffers), GFP_KERNEL);

    kref_init(&self->ref);
    self->buffers = akvcam_list_new();
    self->rw_buffers = akvcam_rbuffer_new();
    mutex_init(&self->buffers_mutex);
    self->rw_mode = rw_mode;
    self->type = type;
    self->multiplanar = multiplanar;
    self->rw_buffer_size = AKVCAM_BUFFERS_MIN;
    init_waitqueue_head(&self->buffers_not_full);
    init_waitqueue_head(&self->buffers_not_empty);
    self->format = akvcam_format_new(0, 0, 0, NULL);

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

bool akvcam_buffers_blocking(akvcam_buffers_t self)
{
    return self->blocking;
}

void akvcam_buffers_set_blocking(akvcam_buffers_t self, bool blocking)
{
    self->blocking = blocking;
}

akvcam_format_t akvcam_buffers_format(akvcam_buffers_t self)
{
    return akvcam_format_new_copy(self->format);
}

void akvcam_buffers_set_format(akvcam_buffers_t self, akvcam_format_t format)
{
    akvcam_format_copy(self->format, format);
}

int akvcam_buffers_allocate(akvcam_buffers_t self,
                            struct v4l2_requestbuffers *params)
{
    size_t i;
    akvcam_buffer_t buffer;
    struct v4l2_buffer v4l2_buff;
    size_t buffer_length;
    __u32 buffer_size;
    int result = 0;

    akpr_function();
    akvcam_init_reserved(params);

    if (!akvcam_buffers_is_supported(self, params->memory)) {
        akpr_err("Memory mode not supported.\n");

        return -EINVAL;
    }

    if (params->type != self->type) {
        akpr_err("Buffer types differs: %s != %s.\n",
                 akvcam_string_from_v4l2_buf_type(params->type),
                 akvcam_string_from_v4l2_buf_type(self->type));

        return -EINVAL;
    }

    if (mutex_lock_interruptible(&self->buffers_mutex))
        return -EIO;

    akvcam_list_clear(self->buffers);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
    params->capabilities = 0;

    if (self->rw_mode & AKVCAM_RW_MODE_MMAP)
        params->capabilities |= V4L2_BUF_CAP_SUPPORTS_MMAP;

    if (self->rw_mode & AKVCAM_RW_MODE_USERPTR)
        params->capabilities |= V4L2_BUF_CAP_SUPPORTS_USERPTR;
#endif

    if (params->count < 1) {
        if (self->rw_mode & AKVCAM_RW_MODE_READWRITE) {
            if (self->rw_buffer_size < 1)
                self->rw_buffer_size = 1;

            akvcam_rbuffer_clear(self->rw_buffers);
            akvcam_rbuffer_resize(self->rw_buffers,
                                  self->rw_buffer_size,
                                  akvcam_format_size(self->format),
                                  AKVCAM_MEMORY_TYPE_VMALLOC);
        }
    } else {
        buffer_length = akvcam_format_size(self->format);
        buffer_size = (__u32) PAGE_ALIGN(buffer_length);

        for (i = 0; i < params->count; i++) {
            buffer = akvcam_buffer_new(buffer_length);

            if (!akvcam_buffer_read(buffer, &v4l2_buff)) {
                result = -EIO;

                break;
            }

            v4l2_buff.index = (__u32) i;
            v4l2_buff.type = params->type;
            v4l2_buff.memory = params->memory;
            v4l2_buff.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            v4l2_buff.field = V4L2_FIELD_NONE;

            if (self->multiplanar)
                v4l2_buff.length = (__u32) akvcam_format_planes(self->format);
            else
                v4l2_buff.length = v4l2_buff.bytesused;

            if (params->memory == V4L2_MEMORY_MMAP && !self->multiplanar) {
                v4l2_buff.flags |= V4L2_BUF_FLAG_MAPPED;
                v4l2_buff.m.offset = (__u32) i * buffer_size;
            }

            if (!akvcam_buffer_write(buffer, &v4l2_buff)) {
                akvcam_buffer_delete(buffer);
                result = -EIO;

                break;
            }

            akvcam_list_push_back(self->buffers,
                                  buffer,
                                  (akvcam_copy_t) akvcam_buffer_ref,
                                  (akvcam_delete_t) akvcam_buffer_delete);
            akvcam_buffer_delete(buffer);
        }
    }

    mutex_unlock(&self->buffers_mutex);
    akpr_debug("%s\n", akvcam_string_from_v4l2_requestbuffers(params));

    return result;
}

void akvcam_buffers_deallocate(akvcam_buffers_t self)
{
    if (!mutex_lock_interruptible(&self->buffers_mutex)) {
        akvcam_list_clear(self->buffers);
        mutex_unlock(&self->buffers_mutex);
    }

    akvcam_buffers_resize_rw(self, self->rw_buffer_size);
}

int akvcam_buffers_create(akvcam_buffers_t self,
                          struct v4l2_create_buffers *buffers,
                          akvcam_format_t format)
{
    size_t i;
    akvcam_buffer_t buffer;
    akvcam_buffer_t last_buffer;
    struct v4l2_buffer v4l2_buff;
    size_t buffer_length;
    __u32 buffer_size;
    __u32 offset = 0;
    int result = 0;

    akpr_function();

    if (!akvcam_buffers_is_supported(self, buffers->memory)) {
        akpr_err("Memory mode not supported.\n");

        return -EINVAL;
    }

    if (buffers->format.type != self->type) {
        akpr_err("Buffer types differs: %s != %s.\n",
                 akvcam_string_from_v4l2_buf_type(buffers->format.type),
                 akvcam_string_from_v4l2_buf_type(self->type));

        return -EINVAL;
    }

    if (!format) {
        akpr_err("Format is NULL\n");

        return -EINVAL;
    }

    if (mutex_lock_interruptible(&self->buffers_mutex))
        return -EIO;

    buffers->index = (__u32) akvcam_list_size(self->buffers);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
    buffers->capabilities = 0;

    if (self->rw_mode & AKVCAM_RW_MODE_MMAP)
        buffers->capabilities |= V4L2_BUF_CAP_SUPPORTS_MMAP;

    if (self->rw_mode & AKVCAM_RW_MODE_USERPTR)
        buffers->capabilities |= V4L2_BUF_CAP_SUPPORTS_USERPTR;
#endif
    akvcam_init_reserved(buffers);

    if (buffers->count > 0) {
        buffer_length = akvcam_format_size(format);
        buffer_size = (__u32) PAGE_ALIGN(buffer_length);
        last_buffer = akvcam_list_back(self->buffers);

        if (last_buffer && akvcam_buffer_read(last_buffer, &v4l2_buff))
            offset = v4l2_buff.m.offset + PAGE_ALIGN(v4l2_buff.length);

        for (i = 0; i < buffers->count; i++) {
            buffer = akvcam_buffer_new(buffer_length);

            if (!akvcam_buffer_read(buffer, &v4l2_buff)) {
                result = -EIO;

                break;
            }

            v4l2_buff.index = buffers->index + (__u32) i;
            v4l2_buff.type = self->type;
            v4l2_buff.memory = buffers->memory;
            v4l2_buff.flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            v4l2_buff.field = V4L2_FIELD_NONE;

            if (self->multiplanar)
                v4l2_buff.length = (__u32) akvcam_format_planes(format);
            else
                v4l2_buff.length = v4l2_buff.bytesused;

            if (buffers->memory == V4L2_MEMORY_MMAP && !self->multiplanar) {
                v4l2_buff.flags |= V4L2_BUF_FLAG_MAPPED;
                v4l2_buff.m.offset = offset + (__u32) i * buffer_size;
            }

            if (!akvcam_buffer_write(buffer, &v4l2_buff)) {
                akvcam_buffer_delete(buffer);
                result = -EIO;

                break;
            }

            akvcam_list_push_back(self->buffers,
                                  buffer,
                                  (akvcam_copy_t) akvcam_buffer_ref,
                                  (akvcam_delete_t) akvcam_buffer_delete);
            akvcam_buffer_delete(buffer);
        }
    }

    mutex_unlock(&self->buffers_mutex);
    akpr_debug("%s\n", akvcam_string_from_v4l2_create_buffers(buffers));

    return result;
}

int akvcam_buffers_query(const akvcam_buffers_t self,
                         struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_buffer v4l2_buff;
    struct v4l2_plane *planes;
    size_t n_planes;
    size_t i;
    int result;

    akpr_function();
    akpr_debug("IN: %s\n", akvcam_string_from_v4l2_buffer(buffer));

    if (akvcam_list_empty(self->buffers))
        return -EINVAL;

    result = mutex_lock_interruptible(&self->buffers_mutex);

    if (result)
        return result;

    akbuffer = akvcam_list_at(self->buffers, buffer->index);

    if (akbuffer
        && akvcam_buffer_read(akbuffer, &v4l2_buff)
        && v4l2_buff.type == buffer->type) {
        if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
            || buffer->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
            v4l2_buff.length = buffer->length;
            v4l2_buff.m.planes = buffer->m.planes;

            if (akvcam_buffer_write(akbuffer, &v4l2_buff))
                result = -EIO;
        }

        if (!result && self->multiplanar) {
            planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);

            if (copy_from_user(planes,
                               (char __user *) buffer->m.planes,
                               buffer->length * sizeof(struct v4l2_plane))) {
                kfree(planes);

                return -EIO;
            }

            n_planes = akvcam_min(buffer->length,
                                  akvcam_format_planes(self->format));

            for (i = 0; i < n_planes; i++) {
                if (akvcam_device_type_from_v4l2(self->type) == AKVCAM_DEVICE_TYPE_CAPTURE
                    || planes[i].bytesused < 1) {
                    planes[i].bytesused =
                            (__u32) akvcam_format_plane_size(self->format, i);
                    planes[i].data_offset = 0;
                }

                planes[i].length =
                        (__u32) akvcam_format_plane_size(self->format, i);
                memset(&planes[i].m, 0, sizeof(struct v4l2_plane));

                if (buffer->memory == V4L2_MEMORY_MMAP) {
                    planes[i].m.mem_offset =
                            buffer->index
                            * (__u32) akvcam_format_size(self->format)
                            + (__u32) akvcam_format_offset(self->format, i);
                }

                akvcam_init_reserved(planes + i);
            }

            if (copy_to_user((char __user *) buffer->m.planes,
                             planes,
                             buffer->length * sizeof(struct v4l2_plane)))
                result = -EIO;

            kfree(planes);
        }

        memcpy(buffer, &v4l2_buff, sizeof(struct v4l2_buffer));
    } else {
        result = -EINVAL;
    }

    mutex_unlock(&self->buffers_mutex);
    akpr_debug("OUT: %s\n", akvcam_string_from_v4l2_buffer(buffer));

    return result;
}

int akvcam_buffers_queue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_buffer v4l2_buff;
    struct v4l2_plane *planes;
    size_t n_planes;
    size_t i;
    void *data;
    int result = 0;

    akpr_function();

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        return -EINVAL;

    result = mutex_lock_interruptible(&self->buffers_mutex);

    if (result)
        return result;

    akbuffer = akvcam_list_at(self->buffers, buffer->index);

    if (akbuffer) {
        if (akvcam_buffer_read(akbuffer, &v4l2_buff)) {
            if (v4l2_buff.type == buffer->type) {
                if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                    || buffer->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
                    v4l2_buff.length = buffer->length;
                    v4l2_buff.m.planes = buffer->m.planes;
                }

                switch (buffer->memory) {
                case V4L2_MEMORY_MMAP:
                    v4l2_buff.flags = buffer->flags;
                    v4l2_buff.flags |= V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_QUEUED;
                    v4l2_buff.flags &= (__u32) ~V4L2_BUF_FLAG_DONE;

                    break;

                case V4L2_MEMORY_USERPTR:
                    if (buffer->m.userptr && !self->multiplanar)
                        v4l2_buff.m.userptr = buffer->m.userptr;

                    v4l2_buff.flags = buffer->flags;
                    v4l2_buff.flags |= V4L2_BUF_FLAG_QUEUED;
                    v4l2_buff.flags &= (__u32) ~(V4L2_BUF_FLAG_MAPPED
                                                 | V4L2_BUF_FLAG_DONE);

                    if (buffer->length > 1
                        && buffer->bytesused > 1
                        && akvcam_device_type_from_v4l2(self->type) == AKVCAM_DEVICE_TYPE_OUTPUT) {
                        data = vzalloc(buffer->bytesused);

                        if (data) {
                            if (self->multiplanar) {
                                planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);

                                if (!copy_from_user(planes,
                                                    (char __user *) buffer->m.planes,
                                                    buffer->length * sizeof(struct v4l2_plane))) {
                                    n_planes = akvcam_min(buffer->length,
                                                          akvcam_format_planes(self->format));

                                    for (i = 0; i < n_planes; i++)
                                        if (copy_from_user((char *) data + akvcam_format_offset(self->format, i),
                                                           (char __user *) planes[i].m.userptr,
                                                           planes[i].length)) {
                                            akpr_err("Failed copying data from user space.\n");
                                            result = -EIO;

                                            break;
                                        }

                                    if (!akvcam_buffer_write_data(akbuffer, data, buffer->bytesused))
                                        result = -EIO;
                                } else {
                                    akpr_err("Failed copying data from user space.\n");
                                    result = -EIO;
                                }

                                kfree(planes);
                            } else {
                                if (copy_from_user(data,
                                                   (char __user *) buffer->m.userptr,
                                                   buffer->length)) {
                                    akpr_err("Failed copying data from user space.\n");
                                    result = -EIO;
                                }
                            }

                            vfree(data);
                        }
                    }

                    break;

                default:
                    break;
                }

                memcpy(buffer, &v4l2_buff, sizeof(struct v4l2_buffer));

                if (!akvcam_buffer_write(akbuffer, &v4l2_buff)) {
                    akpr_err("Failed writing buffer.\n");
                    result = -EIO;
                }
            } else {
                akpr_err("Buffers types differs.\n");
                result = -EINVAL;
            }
        } else {
            akpr_err("Can't read buffer.\n");
            result = -EIO;
        }
    } else {
        akpr_err("Buffer is empty.\n");
        result = -EINVAL;
    }

    mutex_unlock(&self->buffers_mutex);
    akpr_debug("%s\n", akvcam_string_from_v4l2_buffer(buffer));

    return result;
}

static akvcam_buffer_t akvcam_buffers_next_buffer(akvcam_buffers_t self)
{
    akvcam_list_element_t it = NULL;
    akvcam_buffer_t buffer;
    akvcam_buffer_t next_buffer = NULL;
    struct v4l2_buffer v4l2_buff;
    __u32 sequence = UINT_MAX;

    for (;;) {
        buffer = akvcam_list_next(self->buffers, &it);

        if (!it)
            break;

        if (akvcam_buffer_read(buffer, &v4l2_buff)
            && v4l2_buff.flags & V4L2_BUF_FLAG_DONE
            && v4l2_buff.sequence < sequence) {
            next_buffer = buffer;
            sequence = v4l2_buff.sequence;
        }
    }

    return next_buffer;
}

int akvcam_buffers_dequeue(akvcam_buffers_t self, struct v4l2_buffer *buffer)
{
    akvcam_buffer_t akbuffer;
    struct v4l2_buffer v4l2_buff;
    struct v4l2_plane *planes;
    size_t n_planes;
    size_t i;
    void *data;
    int result = 0;

    akpr_function();

    if (!akvcam_buffers_is_supported(self, buffer->memory))
        return -EINVAL;

    if (akvcam_list_empty(self->buffers))
        return -EIO;

    result = mutex_lock_interruptible(&self->buffers_mutex);

    if (result)
        return result;

    if (self->blocking) {
        result =
                akvcam_wait_condition(self->buffers_not_empty,
                                      akvcam_buffers_next_buffer(self) != NULL,
                                      &self->buffers_mutex,
                                      AKVCAM_WAIT_TIMEOUT_MSECS);

        if (result < 1) {
            if (result != -EINTR)
                mutex_unlock(&self->buffers_mutex);

            return result? result: -EAGAIN;
        }
    }

    akbuffer = akvcam_buffers_next_buffer(self);

    if (akbuffer) {
        if (akvcam_buffer_read(akbuffer, &v4l2_buff)) {
            if (v4l2_buff.type == buffer->type) {
                if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                    || buffer->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
                    v4l2_buff.length = buffer->length;
                    v4l2_buff.m.planes = buffer->m.planes;
                }

                switch (buffer->memory) {
                case V4L2_MEMORY_MMAP:
                    v4l2_buff.flags |= V4L2_BUF_FLAG_MAPPED;
                    v4l2_buff.flags &= (__u32) ~(V4L2_BUF_FLAG_DONE
                                                 | V4L2_BUF_FLAG_QUEUED);
                    if (self->multiplanar) {
                        n_planes = akvcam_min(buffer->length, akvcam_format_planes(self->format));

                        if (akvcam_device_type_from_v4l2(self->type) == AKVCAM_DEVICE_TYPE_CAPTURE) {
                            planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);

                            if (planes) {
                                if (!copy_from_user(planes,
                                                    (char __user *) buffer->m.planes,
                                                    buffer->length * sizeof(struct v4l2_plane))) {
                                    for (i = 0; i < n_planes; i++)
                                        planes[i].m.mem_offset =
                                                buffer->index
                                                * (__u32) akvcam_format_size(self->format)
                                                + (__u32) akvcam_format_offset(self->format, i);

                                    if (copy_to_user((char __user *) buffer->m.planes,
                                                     planes,
                                                     buffer->length * sizeof(struct v4l2_plane))) {
                                        akpr_err("Failed copying data to user space.\n");
                                        result = -EIO;
                                    }
                                } else {
                                    akpr_err("Failed copying data from user space.\n");
                                    result = -EIO;
                                }

                                kfree(planes);
                            } else {
                                akpr_err("Can't allocate memory for the planes.\n");
                                result = -EIO;
                            }
                        }
                    }

                    break;

                case V4L2_MEMORY_USERPTR:
                    if (buffer->m.userptr && !self->multiplanar)
                        v4l2_buff.m.userptr = buffer->m.userptr;

                    v4l2_buff.flags &= (__u32) ~(V4L2_BUF_FLAG_MAPPED
                                                 | V4L2_BUF_FLAG_DONE
                                                 | V4L2_BUF_FLAG_QUEUED);
                    if (buffer->length > 1
                        && buffer->bytesused > 1
                        && akvcam_device_type_from_v4l2(self->type) == AKVCAM_DEVICE_TYPE_CAPTURE) {
                        data = vzalloc(buffer->bytesused);

                        if (data) {
                            if (akvcam_buffer_read_data(akbuffer, data, buffer->bytesused)) {
                                if (self->multiplanar) {
                                    planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);

                                    if (!copy_from_user(planes,
                                                        (char __user *) buffer->m.planes,
                                                        buffer->length * sizeof(struct v4l2_plane))) {
                                        n_planes = akvcam_min(buffer->length,
                                                              akvcam_format_planes(self->format));

                                        for (i = 0; i < n_planes; i++)
                                            if (copy_to_user((char __user *) planes[i].m.userptr,
                                                             (char *) data + akvcam_format_offset(self->format, i),
                                                             planes[i].length)) {
                                                akpr_err("Failed copying data to user space.\n");
                                                result = -EIO;

                                                break;
                                            }
                                    } else {
                                        akpr_err("Failed copying data from user space.\n");
                                        result = -EIO;
                                    }

                                    kfree(planes);
                                } else if (copy_to_user((char __user *) buffer->m.userptr,
                                                        data,
                                                        buffer->length)) {
                                    akpr_err("Failed copying data to user space.\n");
                                    result = -EIO;
                                }
                            } else {
                                akpr_err("Can't read buffer data.\n");
                                result = -EIO;
                            }

                            vfree(data);
                        }
                    }

                    break;

                default:
                    break;
                }

                memcpy(buffer, &v4l2_buff, sizeof(struct v4l2_buffer));

                if (!akvcam_buffer_write(akbuffer, &v4l2_buff)) {
                    akpr_err("Failed writing buffer.\n");
                    result = -EIO;
                }
            } else {
                akpr_err("Buffers types differs.\n");
                result = -EINVAL;
            }
        } else {
            akpr_err("Can't read buffer.\n");
            result = -EIO;
        }
    } else {
        akpr_err("Buffer is empty.\n");
        result = -EAGAIN;
    }

    mutex_unlock(&self->buffers_mutex);
    akpr_debug("%s\n", akvcam_string_from_v4l2_buffer(buffer));

    return result;
}

static bool akvcam_buffers_equals_offset(const akvcam_buffer_t buffer,
                                         const __u32 *offset)
{
    struct v4l2_buffer v4l2_buff;

    if (!akvcam_buffer_read(buffer, &v4l2_buff))
        return false;

    return akvcam_between(v4l2_buff.m.offset,
                          *offset,
                          v4l2_buff.m.offset + v4l2_buff.bytesused);
}

int akvcam_buffers_data_map(const akvcam_buffers_t self,
                            __u32 offset,
                            struct vm_area_struct *vma)
{
    akvcam_buffer_t buffer;
    akvcam_list_element_t it;
    int result;

    akpr_function();
    result = mutex_lock_interruptible(&self->buffers_mutex);

    if (result)
        return result;

    it = akvcam_list_find(self->buffers,
                          &offset,
                          (akvcam_are_equals_t) akvcam_buffers_equals_offset);
    buffer = akvcam_list_element_data(it);
    result = akvcam_buffer_map_data(buffer, vma);
    mutex_unlock(&self->buffers_mutex);

    return result;
}

bool akvcam_buffers_allocated(const akvcam_buffers_t self)
{
    return !akvcam_list_empty(self->buffers);
}

size_t akvcam_buffers_size_rw(const akvcam_buffers_t self)
{
    size_t size = 0;

    if (!mutex_lock_interruptible(&self->buffers_mutex)) {
        size = akvcam_rbuffer_n_elements(self->rw_buffers);
        mutex_unlock(&self->buffers_mutex);
    }

    return size;
}

bool akvcam_buffers_resize_rw(akvcam_buffers_t self, size_t size)
{
    if (!akvcam_list_empty(self->buffers))
        return false;

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return false;

    if (size < 1)
        size = 1;

    if (mutex_lock_interruptible(&self->buffers_mutex))
        return false;

    akvcam_rbuffer_clear(self->rw_buffers);
    akvcam_rbuffer_resize(self->rw_buffers,
                          size,
                          akvcam_format_size(self->format),
                          AKVCAM_MEMORY_TYPE_VMALLOC);
    self->rw_buffer_size = size;
    mutex_unlock(&self->buffers_mutex);

    return true;
}

ssize_t akvcam_buffers_read(akvcam_buffers_t self,
                            void __user *data,
                            size_t size)
{
    void *vdata;
    ssize_t data_size;

    akpr_function();

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return 0;

    data_size = mutex_lock_interruptible(&self->buffers_mutex);

    if (data_size)
        return data_size;

    if (self->blocking) {
        data_size =
                akvcam_wait_condition(self->buffers_not_empty,
                                      akvcam_rbuffer_data_size(self->rw_buffers) >= size,
                                      &self->buffers_mutex,
                                      AKVCAM_WAIT_TIMEOUT_MSECS);

        if (data_size > 0) {
            vdata = vmalloc(size);

            if (vdata) {
                akvcam_rbuffer_dequeue_bytes(self->rw_buffers,
                                             vdata,
                                             &size,
                                             false);

                if (!copy_to_user(data, vdata, size))
                    data_size = size;
                else
                    data_size = -EIO;

                vfree(vdata);
                wake_up_interruptible_all(&self->buffers_not_full);
            } else {
                data_size = -EIO;
            }
        } else if (data_size == 0) {
            data_size = -EAGAIN;
        }
    } else {
        size = akvcam_min(akvcam_rbuffer_data_size(self->rw_buffers), size);

        if (size > 0) {
            vdata = vmalloc(size);

            if (vdata) {
                akvcam_rbuffer_dequeue_bytes(self->rw_buffers,
                                             vdata,
                                             &size,
                                             false);

                if (!copy_to_user(data, vdata, size))
                    data_size = size;
                else
                    data_size = -EIO;

                vfree(vdata);
                wake_up_interruptible_all(&self->buffers_not_full);
            } else {
                data_size = -EIO;
            }
        } else {
            data_size = -EAGAIN;
        }
    }

    if (data_size != -EINTR)
        mutex_unlock(&self->buffers_mutex);

    return data_size;
}

ssize_t akvcam_buffers_write(akvcam_buffers_t self,
                             const void __user *data,
                             size_t size)
{
    void *vdata;
    ssize_t data_size;

    akpr_function();

    if (!(self->rw_mode & AKVCAM_RW_MODE_READWRITE))
        return 0;

    data_size = mutex_lock_interruptible(&self->buffers_mutex);

    if (data_size)
        return data_size;

    if (self->blocking) {
        data_size =
                akvcam_wait_condition(self->buffers_not_full,
                                      akvcam_rbuffer_available_data_size(self->rw_buffers) >= (ssize_t) size,
                                      &self->buffers_mutex,
                                      AKVCAM_WAIT_TIMEOUT_MSECS);

        if (data_size > 0) {
            vdata = vmalloc(size);

            if (vdata) {
                if (!copy_from_user(vdata, data, size)) {
                    akvcam_rbuffer_queue_bytes(self->rw_buffers, vdata, size);
                    wake_up_interruptible_all(&self->buffers_not_empty);
                    data_size = size;
                } else {
                    data_size = -EIO;
                }

                vfree(vdata);
            } else {
                data_size = -EIO;
            }
        } else if (data_size == 0) {
            data_size = -EAGAIN;
        }
    } else {
        data_size =
                akvcam_min(akvcam_rbuffer_available_data_size(self->rw_buffers),
                           (ssize_t) size);

        if (data_size > 0) {
            size = data_size;
            vdata = vmalloc(size);

            if (vdata) {
                if (!copy_from_user(vdata, data, size)) {
                    akvcam_rbuffer_queue_bytes(self->rw_buffers, vdata, size);
                    wake_up_interruptible_all(&self->buffers_not_empty);
                    data_size = size;
                } else {
                    data_size = -EIO;
                }

                vfree(vdata);
            } else {
                data_size = -EIO;
            }
        } else {
            data_size = -EAGAIN;
        }
    }

    if (data_size != -EINTR)
        mutex_unlock(&self->buffers_mutex);

    return data_size;
}

static akvcam_buffer_t akvcam_buffers_next_read_buffer(akvcam_buffers_t self)
{
    akvcam_list_element_t it = NULL;
    akvcam_buffer_t buffer;
    akvcam_buffer_t next_buffer = NULL;
    struct v4l2_buffer v4l2_buff;
    __u32 sequence = UINT_MAX;

    for (;;) {
        buffer = akvcam_list_next(self->buffers, &it);

        if (!it)
            break;

        if (akvcam_buffer_read(buffer, &v4l2_buff)
            && v4l2_buff.flags & V4L2_BUF_FLAG_QUEUED
            && !(v4l2_buff.flags & V4L2_BUF_FLAG_DONE)
            && v4l2_buff.sequence < sequence) {
            next_buffer = buffer;
            sequence = v4l2_buff.sequence;
        }
    }

    return next_buffer;
}

akvcam_frame_t akvcam_buffers_read_frame(akvcam_buffers_t self)
{
    akvcam_buffer_t buffer;
    struct v4l2_buffer v4l2_buff;
    size_t length;
    akvcam_frame_t frame = NULL;
    size_t frame_size;
    int condition_result;

    akpr_function();

    if (mutex_lock_interruptible(&self->buffers_mutex) == 0) {
        if (!akvcam_list_empty(self->buffers)) {
            condition_result =
                    akvcam_wait_condition(self->buffers_not_empty,
                                          akvcam_buffers_next_read_buffer(self),
                                          &self->buffers_mutex,
                                          AKVCAM_WAIT_TIMEOUT_MSECS);
        } else {
            condition_result =
                    akvcam_wait_condition(self->buffers_not_empty,
                                          !akvcam_rbuffer_elements_empty(self->rw_buffers),
                                          &self->buffers_mutex,
                                          AKVCAM_WAIT_TIMEOUT_MSECS);
        }

        if (condition_result < 1) {
            if (condition_result != -EINTR)
                mutex_unlock(&self->buffers_mutex);

            return frame;
        }

        if (!akvcam_list_empty(self->buffers)) {
            buffer = akvcam_buffers_next_read_buffer(self);

            if (buffer && akvcam_buffer_read(buffer, &v4l2_buff)) {
                if (v4l2_buff.memory == V4L2_MEMORY_MMAP
                    || v4l2_buff.memory == V4L2_MEMORY_USERPTR) {
                    frame = akvcam_frame_new(self->format, NULL, 0);
                    length = akvcam_min((size_t) v4l2_buff.length,
                                        akvcam_frame_size(frame));
                    akvcam_buffer_read_data(buffer,
                                            akvcam_frame_data(frame),
                                            length);
                }
            }
        } else if (self->rw_mode & AKVCAM_RW_MODE_READWRITE) {
            frame = akvcam_frame_new(self->format, NULL, 0);
            frame_size = akvcam_frame_size(frame);
            akvcam_rbuffer_dequeue_bytes(self->rw_buffers,
                                         akvcam_frame_data(frame),
                                         &frame_size,
                                         false);
        }

        if (frame)
            wake_up_interruptible_all(&self->buffers_not_full);

        mutex_unlock(&self->buffers_mutex);
    }

    return frame;
}

static akvcam_buffer_t akvcam_buffers_next_write_buffer(akvcam_buffers_t self)
{
    akvcam_list_element_t it = NULL;
    akvcam_buffer_t buffer;
    akvcam_buffer_t next_buffer = NULL;
    struct v4l2_buffer v4l2_buff;
    __u32 sequence = UINT_MAX;

    for (;;) {
        buffer = akvcam_list_next(self->buffers, &it);

        if (!it)
            break;

        if (akvcam_buffer_read(buffer, &v4l2_buff)
            && v4l2_buff.flags & (V4L2_BUF_FLAG_DONE | V4L2_BUF_FLAG_QUEUED)
            && v4l2_buff.sequence < sequence) {
            next_buffer = buffer;
            sequence = v4l2_buff.sequence;
        }
    }

    return next_buffer;
}

int akvcam_buffers_write_frame(akvcam_buffers_t self, akvcam_frame_t frame)
{
    akvcam_buffer_t buffer;
    struct v4l2_buffer v4l2_buff;
    char *data;
    size_t length;
    int condition_result = -ENOTTY;
    int result = 0;

    akpr_function();

    if (mutex_lock_interruptible(&self->buffers_mutex) == 0) {
        if (self->rw_mode & (AKVCAM_RW_MODE_MMAP | AKVCAM_RW_MODE_USERPTR)
            && !akvcam_list_empty(self->buffers)) {
            akpr_debug("Writting streaming buffers\n");
            condition_result =
                    akvcam_wait_condition(self->buffers_not_full,
                                          akvcam_buffers_next_write_buffer(self),
                                          &self->buffers_mutex,
                                          AKVCAM_WAIT_TIMEOUT_MSECS);

            if (condition_result < 1) {
                if (condition_result != -EINTR)
                    mutex_unlock(&self->buffers_mutex);

                return condition_result;
            }

            buffer = akvcam_buffers_next_write_buffer(self);

            if (buffer) {
                if (akvcam_buffer_read(buffer, &v4l2_buff)) {
                    if (v4l2_buff.memory == V4L2_MEMORY_MMAP
                        || v4l2_buff.memory == V4L2_MEMORY_USERPTR) {
                        if (frame) {
                            length = akvcam_frame_size(frame);
                            result = akvcam_buffer_write_data(buffer,
                                                              akvcam_frame_data(frame),
                                                              length)? 0: -EIO;
                        } else {
                            data = vzalloc(v4l2_buff.length);
                            result = akvcam_buffer_write_data(buffer,
                                                              data,
                                                              v4l2_buff.length)? 0: -EIO;
                            vfree(data);
                        }

                        if (result == 0) {
                            akvcam_get_timestamp(&v4l2_buff.timestamp);
                            v4l2_buff.sequence = self->sequence;
                            v4l2_buff.flags |= V4L2_BUF_FLAG_DONE;

                            if (akvcam_buffer_write(buffer, &v4l2_buff))
                                self->sequence++;
                            else
                                result = -EIO;
                        }
                    }
                } else {
                    result = -EIO;
                }
            } else {
                result = -EAGAIN;
            }
        } else if (frame
                   && self->rw_mode & AKVCAM_RW_MODE_READWRITE
                   && akvcam_list_empty(self->buffers)) {
            akpr_debug("Writting RW buffers\n");
            condition_result =
                    akvcam_wait_condition(self->buffers_not_full,
                                          !akvcam_rbuffer_elements_full(self->rw_buffers),
                                          &self->buffers_mutex,
                                          AKVCAM_WAIT_TIMEOUT_MSECS);

            if (condition_result < 1) {
                if (condition_result != -EINTR)
                    mutex_unlock(&self->buffers_mutex);

                return condition_result;
            }

            length = akvcam_frame_size(frame);
            akpr_debug("Queueing %lu bytes\n", length);
            akvcam_rbuffer_queue_bytes(self->rw_buffers,
                                       akvcam_frame_data(frame),
                                       length);
            akpr_debug("Total bytes in queue: %lu\n", akvcam_rbuffer_data_size(self->rw_buffers));
            akpr_debug("Total frames in queue: %lu\n", akvcam_rbuffer_n_data(self->rw_buffers));
        } else {
            akpr_debug("Invalid device mode.\n");
            result = -ENOTTY;
        }

        if (result == 0)
            wake_up_interruptible_all(&self->buffers_not_empty);

        mutex_unlock(&self->buffers_mutex);
    }

    return result;
}

__u32 akvcam_buffers_sequence(akvcam_buffers_t self)
{
    return self->sequence;
}

void akvcam_buffers_reset_sequence(akvcam_buffers_t self)
{
    self->sequence = 0;
}

bool akvcam_buffers_is_supported(const akvcam_buffers_t self,
                                 enum v4l2_memory type)
{
    akpr_function();
    akpr_debug("rw_mode: %s, memory_type: %s\n",
               akvcam_string_from_rw_mode(self->rw_mode),
               akvcam_string_from_v4l2_memory(type));

    return (self->rw_mode & AKVCAM_RW_MODE_MMAP
            && type == V4L2_MEMORY_MMAP)
            || (self->rw_mode & AKVCAM_RW_MODE_USERPTR
                && type == V4L2_MEMORY_USERPTR);
}
