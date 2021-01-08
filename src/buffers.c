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
#include <linux/mutex.h>
#include <media/videobuf2-vmalloc.h>

#include "buffers.h"
#include "device.h"
#include "format.h"
#include "frame.h"
#include "log.h"

#define AKVCAM_BUFFERS_MIN 4

typedef struct {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
} akvcam_buffers_buffer, *akvcam_buffers_buffer_t;

static const struct vb2_ops akvcam_akvcam_buffers_queue_ops;

struct akvcam_buffers
{
    struct kref ref;
    struct list_head buffers;
    struct vb2_queue queue;
    struct mutex buffers_mutex;
    struct mutex frames_mutex;
    akvcam_format_t format;
    akvcam_signal_callback(buffers, streaming_started);
    akvcam_signal_callback(buffers, streaming_stopped);
    enum v4l2_buf_type type;
    AKVCAM_RW_MODE rw_mode;
    __u32 sequence;
};

akvcam_signal_define(buffers, streaming_started)
akvcam_signal_define(buffers, streaming_stopped)

enum vb2_io_modes akvcam_buffers_io_modes_from_device_type(enum v4l2_buf_type type,
                                                           AKVCAM_RW_MODE rw_mode);
int akvcam_buffers_queue_setup(struct vb2_queue *queue,
                               unsigned int *num_buffers,
                               unsigned int *num_planes,
                               unsigned int sizes[],
                               struct device *alloc_devs[]);
int akvcam_buffers_buffer_prepare(struct vb2_buffer *buffer);
void akvcam_buffers_buffer_queue(struct vb2_buffer *buffer);
int akvcam_buffers_start_streaming(struct vb2_queue *queue, unsigned int count);
void akvcam_buffers_stop_streaming(struct vb2_queue *queue);

akvcam_buffers_t akvcam_buffers_new(AKVCAM_RW_MODE rw_mode,
                                    enum v4l2_buf_type type)
{
    akvcam_buffers_t self = kzalloc(sizeof(struct akvcam_buffers), GFP_KERNEL);

    kref_init(&self->ref);
    INIT_LIST_HEAD(&self->buffers);
    mutex_init(&self->buffers_mutex);
    mutex_init(&self->frames_mutex);
    self->rw_mode = rw_mode;
    self->type = type;
    self->format = akvcam_format_new(0, 0, 0, NULL);
    self->queue.type = type;
    self->queue.io_modes =
            akvcam_buffers_io_modes_from_device_type(self->type,
                                                     self->rw_mode);
    self->queue.drv_priv = self;
    self->queue.lock = &self->buffers_mutex;
    self->queue.buf_struct_size = sizeof(akvcam_buffers_buffer);
    self->queue.mem_ops = &vb2_vmalloc_memops;
    self->queue.ops = &akvcam_akvcam_buffers_queue_ops;
    self->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    self->queue.min_buffers_needed = AKVCAM_BUFFERS_MIN;

    return self;
}

static void akvcam_buffers_free(struct kref *ref)
{
    akvcam_buffers_t self = container_of(ref, struct akvcam_buffers, ref);
    akvcam_format_delete(self->format);
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

akvcam_format_t akvcam_buffers_format(akvcam_buffers_ct self)
{
    return akvcam_format_new_copy(self->format);
}

void akvcam_buffers_set_format(akvcam_buffers_t self, akvcam_format_ct format)
{
    akvcam_format_copy(self->format, format);
}

size_t akvcam_buffers_count(akvcam_buffers_ct self)
{
    return self->queue.min_buffers_needed;
}

void akvcam_buffers_set_count(akvcam_buffers_t self, size_t nbuffers)
{
    self->queue.min_buffers_needed = nbuffers;
}

akvcam_frame_t akvcam_buffers_read_frame(akvcam_buffers_t self)
{
    akvcam_frame_t frame;
    akvcam_buffers_buffer_t buf;
    size_t i;

    akpr_function();

    if (mutex_lock_interruptible(&self->frames_mutex))
        return NULL;

    if (list_empty(&self->buffers)) {
        mutex_unlock(&self->frames_mutex);

        return NULL;
    }

    buf = list_entry(self->buffers.next, akvcam_buffers_buffer, list);
    list_del(&buf->list);
    buf->vb.vb2_buf.timestamp = ktime_get_ns();
    buf->vb.field = V4L2_FIELD_NONE;
    buf->vb.sequence = self->sequence++;
    mutex_unlock(&self->frames_mutex);

    frame = akvcam_frame_new(self->format, NULL, 0);

    for (i = 0; i < buf->vb.vb2_buf.num_planes; i++) {
        memcpy(akvcam_frame_plane_data(frame, i),
               vb2_plane_vaddr(&buf->vb.vb2_buf, i),
               akvcam_format_plane_size(self->format, i));
    }

    vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

    return frame;
}

int akvcam_buffers_write_frame(akvcam_buffers_t self, akvcam_frame_t frame)
{
    akvcam_buffers_buffer_t buf;
    size_t i;
    int result;

    akpr_function();
    result = mutex_lock_interruptible(&self->frames_mutex);

    if (result)
        return result;

    if (list_empty(&self->buffers)) {
        mutex_unlock(&self->frames_mutex);

        return -EAGAIN;
    }

    buf = list_entry(self->buffers.next, akvcam_buffers_buffer, list);
    list_del(&buf->list);
    buf->vb.vb2_buf.timestamp = ktime_get_ns();
    buf->vb.field = V4L2_FIELD_NONE;
    buf->vb.sequence = self->sequence++;
    mutex_unlock(&self->frames_mutex);

    for (i = 0; i < buf->vb.vb2_buf.num_planes; i++) {
        memcpy(vb2_plane_vaddr(&buf->vb.vb2_buf, i),
               akvcam_frame_plane_data(frame, i),
               vb2_plane_size(&buf->vb.vb2_buf, i));
    }

    vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

    return 0;
}

struct vb2_queue *akvcam_buffers_vb2_queue(akvcam_buffers_t self)
{
    return &self->queue;
}

enum vb2_io_modes akvcam_buffers_io_modes_from_device_type(enum v4l2_buf_type type,
                                                           AKVCAM_RW_MODE rw_mode)
{
    enum vb2_io_modes io_modes = 0;

    if (rw_mode & AKVCAM_RW_MODE_READWRITE) {
        if (akvcam_device_type_from_v4l2(type) == AKVCAM_DEVICE_TYPE_CAPTURE)
            io_modes |= VB2_READ;
        else
            io_modes |= VB2_WRITE;
    }

    if (rw_mode & AKVCAM_RW_MODE_MMAP)
        io_modes |= VB2_MMAP;

    if (rw_mode & AKVCAM_RW_MODE_USERPTR)
        io_modes |= VB2_USERPTR;

    if (rw_mode & AKVCAM_RW_MODE_DMABUF)
        io_modes |= VB2_DMABUF;

    return io_modes;
}

int akvcam_buffers_queue_setup(struct vb2_queue *queue,
                               unsigned int *num_buffers,
                               unsigned int *num_planes,
                               unsigned int sizes[],
                               struct device *alloc_devs[])
{
    akvcam_buffers_t self = vb2_get_drv_priv(queue);
    size_t i;
    UNUSED(alloc_devs);

    akpr_function();

    if (*num_buffers < 1)
        *num_buffers = 1;

    if (*num_planes > 0) {
        if (*num_planes < akvcam_format_planes(self->format))
            return -EINVAL;

        for (i = 0; i < *num_planes; i++)
            if (sizes[i] < akvcam_format_plane_size(self->format, i))
                return -EINVAL;

        return 0;
    }

    *num_planes = akvcam_format_planes(self->format);

    for (i = 0; i < *num_planes; i++)
        sizes[i] = akvcam_format_plane_size(self->format, i);

    return 0;
}

int akvcam_buffers_buffer_prepare(struct vb2_buffer *buffer)
{
    akvcam_buffers_t self = vb2_get_drv_priv(buffer->vb2_queue);
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(buffer);
    size_t plane_size;
    size_t i;

    akpr_function();

    for (i = 0; i < buffer->num_planes; i++) {
        plane_size = akvcam_format_plane_size(self->format, i);

        if (vb2_plane_size(buffer, i) < plane_size)
            return -EINVAL;
        else
            vb2_set_plane_payload(buffer, i, plane_size);
    }

    if (vbuf->field == V4L2_FIELD_ANY)
        vbuf->field = V4L2_FIELD_NONE;

    return 0;
}

void akvcam_buffers_buffer_queue(struct vb2_buffer *buffer)
{
    akvcam_buffers_t self = vb2_get_drv_priv(buffer->vb2_queue);
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(buffer);
    akvcam_buffers_buffer_t buf = container_of(vbuf, akvcam_buffers_buffer, vb);

    akpr_function();

    if (!mutex_lock_interruptible(&self->frames_mutex)) {
        list_add_tail(&buf->list, &self->buffers);
        mutex_unlock(&self->frames_mutex);
    }
}

int akvcam_buffers_start_streaming(struct vb2_queue *queue, unsigned int count)
{
    akvcam_buffers_t self = vb2_get_drv_priv(queue);
    UNUSED(count);

    akpr_function();
    self->sequence = 0;

    return akvcam_call_no_args(self, streaming_started);
}

void akvcam_buffers_stop_streaming(struct vb2_queue *queue)
{
    akvcam_buffers_t self = vb2_get_drv_priv(queue);
    akvcam_buffers_buffer_t buf;
    akvcam_buffers_buffer_t node;

    akpr_function();

    akvcam_emit_no_args(self, streaming_stopped);

    if (!mutex_lock_interruptible(&self->frames_mutex)) {
        list_for_each_entry_safe(buf, node, &self->buffers, list) {
            vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
            list_del(&buf->list);
        }

        mutex_unlock(&self->frames_mutex);
    }
}

static const struct vb2_ops akvcam_akvcam_buffers_queue_ops = {
    .queue_setup     = akvcam_buffers_queue_setup,
    .buf_prepare     = akvcam_buffers_buffer_prepare,
    .buf_queue       = akvcam_buffers_buffer_queue,
    .start_streaming = akvcam_buffers_start_streaming,
    .stop_streaming  = akvcam_buffers_stop_streaming,
    .wait_prepare    = vb2_ops_wait_prepare,
    .wait_finish     = vb2_ops_wait_finish,
};
