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
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/videobuf2-vmalloc.h>

#include "queue.h"
#include "device.h"
#include "format.h"
#include "list.h"
#include "object.h"
#include "utils.h"
#include "mutex.h"

struct akvcam_queue
{
    akvcam_object_t self;
    akvcam_device_t device;
    akvcam_list_t buffers;
    struct vb2_queue queue;
    struct task_struct *thread;
    struct mutex mutex;
    akvcam_mutex_t qmutex;
    wait_queue_head_t buffers_not_empty;
    __u32 sequence;
    unsigned int num_buffers;
};

static struct vb2_ops akvcam_vb2_ops;
int akvcam_queue_send_frames(void *data);

akvcam_queue_t akvcam_queue_new(struct akvcam_device *device)
{
    int result;
    akvcam_queue_t self = kzalloc(sizeof(struct akvcam_queue), GFP_KERNEL);

    if (!self) {
        akvcam_set_last_error(-ENOMEM);

        goto akvcam_queue_new_failed;
    }

    self->self =
            akvcam_object_new(self, (akvcam_deleter_t) akvcam_queue_delete);

    if (!self->self)
        goto akvcam_queue_new_failed;

    self->device = device;
    self->thread = NULL;
    self->num_buffers = 4;
    self->sequence = 0;
    memset(&self->mutex, 0, sizeof(struct mutex));
    mutex_init(&self->mutex);
    self->qmutex = akvcam_mutex_new(AKVCAM_MUTEX_MODE_PERFORMANCE);
    init_waitqueue_head(&self->buffers_not_empty);
    self->buffers = akvcam_list_new();

    memset(&self->queue, 0, sizeof(struct vb2_queue));
    self->queue.type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;
    self->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
    self->queue.lock = &self->mutex;
    self->queue.ops = &akvcam_vb2_ops;
    self->queue.mem_ops = &vb2_vmalloc_memops;
    self->queue.drv_priv = self;
    self->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
                                | V4L2_BUF_FLAG_TSTAMP_SRC_SOE;
    result = vb2_queue_init(&self->queue);

    if (result) {
        akvcam_set_last_error(result);

        goto akvcam_queue_new_failed;
    }

    akvcam_set_last_error(0);

    return self;

akvcam_queue_new_failed:
    if (self) {
        vb2_queue_release(&self->queue);
        akvcam_list_delete(&self->buffers);
        mutex_destroy(&self->mutex);
        akvcam_object_free(&AKVCAM_TO_OBJECT(self));
        kfree(self);
    }

    return NULL;
}

void akvcam_queue_delete(akvcam_queue_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    vb2_queue_release(&((*self)->queue));
    akvcam_list_delete(&((*self)->buffers));
    akvcam_mutex_delete(&((*self)->qmutex));
    mutex_destroy(&((*self)->mutex));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

struct vb2_queue *akvcam_queue_vb2_queue(akvcam_queue_t self)
{
    return &self->queue;
}

struct mutex *akvcam_queue_mutex(akvcam_queue_t self)
{
    return &self->mutex;
}

int akvcam_queue_setup(struct vb2_queue *queue,
                       const void *parg,
                       unsigned int *num_buffers,
                       unsigned int *num_planes,
                       unsigned int sizes[],
                       void *alloc_ctxs[])
{
    akvcam_queue_t self;
    akvcam_format_t format;
    size_t frame_size;
    UNUSED(parg);
    UNUSED(alloc_ctxs);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    self = vb2_get_drv_priv(queue);
    format = akvcam_device_format_nr(self->device);
    frame_size = akvcam_format_size(format);

    if (*num_planes)
        return (*num_planes != 1 || sizes[0] < frame_size)? -EINVAL: 0;

    if (queue->num_buffers + *num_buffers < self->num_buffers)
        *num_buffers = self->num_buffers - queue->num_buffers;

    *num_planes = 1;
    sizes[0] = (unsigned int) frame_size;

    return 0;
}

int akvcam_buf_prepare(struct vb2_buffer *buffer)
{
    akvcam_queue_t self;
    akvcam_format_t format;
    size_t frame_size;

    printk(KERN_INFO "%s\n", __FUNCTION__);
    self = vb2_get_drv_priv(buffer->vb2_queue);
    format = akvcam_device_format_nr(self->device);
    frame_size = akvcam_format_size(format);

    if (vb2_plane_size(buffer, 0) < frame_size)
        return -EINVAL;

    vb2_set_plane_payload(buffer, 0, frame_size);

    return 0;
}

void akvcam_buf_queue(struct vb2_buffer *buffer)
{
    akvcam_queue_t self;

    printk(KERN_INFO "%s\n", __FUNCTION__);
    self = vb2_get_drv_priv(buffer->vb2_queue);

    akvcam_mutex_lock(self->qmutex);
    akvcam_list_push_back(self->buffers, buffer, NULL);
    akvcam_mutex_unlock(self->qmutex);

    wake_up_all(&self->buffers_not_empty);
}

int akvcam_start_streaming(struct vb2_queue *queue, unsigned int count)
{
    akvcam_queue_t self;
    UNUSED(count);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    self = vb2_get_drv_priv(queue);
    self->sequence = 0;
    self->thread = kthread_run(akvcam_queue_send_frames,
                               self,
                               "akvcam-thread-%llu",
                               akvcam_id());

    return 0;
}

void akvcam_stop_streaming(struct vb2_queue *queue)
{
    akvcam_queue_t self;
    akvcam_list_element_t it = NULL;
    struct vb2_buffer *buffer;

    printk(KERN_INFO "%s\n", __FUNCTION__);
    self = vb2_get_drv_priv(queue);
    kthread_stop(self->thread);
    self->thread = NULL;

    for (;;) {
        buffer = akvcam_list_next(self->buffers, &it);

        if (!it)
            break;

        vb2_buffer_done(buffer, VB2_BUF_STATE_ERROR);
    }

    akvcam_mutex_lock(self->qmutex);
    akvcam_list_clear(self->buffers);
    akvcam_mutex_unlock(self->qmutex);
}

int akvcam_queue_send_frames(void *data)
{
    akvcam_queue_t self = data;
    akvcam_format_t format = akvcam_device_format_nr(self->device);
    struct v4l2_fract *frame_rate = akvcam_format_frame_rate(format);
    size_t frame_size = akvcam_format_size(format);
    unsigned int tsleep = 1000 * frame_rate->denominator;
    struct vb2_buffer *buffer;
    struct vb2_v4l2_buffer *v4l2_buffer;
    char *pixels;
    bool ok;

    if (frame_rate->numerator)
        tsleep /= frame_rate->numerator;

    while (!kthread_should_stop()) {
        if (!wait_event_interruptible_timeout(self->buffers_not_empty,
                                              !akvcam_list_empty(self->buffers),
                                              1 * HZ))
            continue;

        akvcam_mutex_lock(self->qmutex);
        ok = akvcam_list_pop(self->buffers, 0, (void **) &buffer, NULL);
        akvcam_mutex_unlock(self->qmutex);

        v4l2_buffer = to_vb2_v4l2_buffer(buffer);
        v4l2_buffer->field = V4L2_FIELD_NONE;
        v4l2_buffer->sequence = self->sequence;
        self->sequence++;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
        v4l2_get_timestamp(&v4l2_buffer->timestamp);
#else
        struct timeval timestamp;
        v4l2_get_timestamp(&timestamp);
        buffer->timestamp = (u64) ktime_to_ns(timeval_to_ktime(timestamp));
#endif

        pixels = vb2_plane_vaddr(buffer, 0);

        if (pixels) {
            get_random_bytes_arch(pixels, (int) frame_size);

            vb2_set_plane_payload(buffer, 0, frame_size);
            vb2_buffer_done(buffer, VB2_BUF_STATE_DONE);
        }

//        msleep_interruptible(tsleep);
    }

    return 0;
}

static struct vb2_ops akvcam_vb2_ops = {
    .queue_setup     = akvcam_queue_setup    ,
    .buf_prepare     = akvcam_buf_prepare    ,
    .buf_queue       = akvcam_buf_queue      ,
    .start_streaming = akvcam_start_streaming,
    .stop_streaming  = akvcam_stop_streaming ,
    .wait_prepare    = vb2_ops_wait_prepare  ,
    .wait_finish     = vb2_ops_wait_finish   ,
};
