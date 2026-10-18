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

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>

#include "device.h"
#include "attributes.h"
#include "buffers.h"
#include "controls.h"
#include "converter.h"
#include "driver.h"
#include "format.h"
#include "frame.h"
#include "frame_filter.h"
#include "ioctl.h"
#include "list.h"
#include "log.h"

// Elastic devices: an output device creates capture devices on demand.
#define AKVCAM_ELASTIC_DEFAULT_MAX    8
#define AKVCAM_ELASTIC_MAX_LIMIT      64
#define AKVCAM_ELASTIC_STOP_DELAY_MS  3000

struct akvcam_device
{
    struct kref ref;
    char *name;
    char *description;
    akvcam_formats_list_t formats;
    akvcam_format_t format;
    bool dynamic_formats;
    akvcam_controls_t controls;
    akvcam_devices_list_t connected_devices;
    akvcam_buffers_t buffers;
    akvcam_frame_t current_frame;
    akvcam_frame_ct default_frame;
    akvcam_frame_filter_ct frame_filter;
    akvcam_converter_t in_video_converter;
    akvcam_converter_t out_video_converter;
    struct mutex device_mutex;
    struct mutex frame_mutex;
    struct mutex clock_mutex;
    struct v4l2_device v4l2_dev;
    struct video_device *vdev;
    struct task_struct *thread;
    AKVCAM_DEVICE_TYPE type;
    enum v4l2_buf_type buffer_type;
    AKVCAM_RW_MODE rw_mode;
    bool direct_mode;
    int32_t videonr;
    struct mutex connected_mutex;
    atomic_t open_count;

    // Elastic devices
    bool elastic;                      // output: creates capture devices
    int elastic_max;                   // output: maximum capture devices
    struct delayed_work elastic_work;  // output: reconciles capture devices
    unsigned long elastic_stop_time;   // output: jiffies when it stopped
    akvcam_device_t elastic_owner;     // capture: output that created it
    unsigned int elastic_index;        // capture: N in "<description> N"

    // Capture controls
    int brightness;
    int contrast;
    int gamma;
    int saturation;
    int hue;
    bool gray;
    bool horizontal_mirror;
    bool vertical_mirror;
    bool swap_rgb;

    // Output controls
    bool horizontal_flip;
    bool vertical_flip;
    AKVCAM_SCALING_MODE scaling;
    AKVCAM_ASPECT_RATIO_MODE aspect_ratio;
};

typedef int (*akvcam_thread_t)(void *data);
static const struct v4l2_file_operations akvcam_device_fops;
static void akvcam_device_vdev_release(struct video_device *vdev);
static void akvcam_device_elastic_work(struct work_struct *work);
static void akvcam_device_elastic_kick(akvcam_device_t self);

enum v4l2_buf_type akvcam_device_v4l2_from_device_type(AKVCAM_DEVICE_TYPE type);
int akvcam_device_controls_updated(akvcam_device_t self, __u32 id, __s32 value);
int akvcam_device_stop_streaming(akvcam_device_t self);
void akvcam_device_clock_run_once(akvcam_device_t self);
int akvcam_device_clock_start(akvcam_device_t self);
void akvcam_device_clock_stop(akvcam_device_t self);
int akvcam_device_clock_timeout(akvcam_device_t self);
akvcam_frame_t akvcam_device_frame_apply_adjusts(akvcam_device_ct self,
                                                 akvcam_frame_ct frame);

akvcam_device_t akvcam_device_new(const char *name,
                                  const char *description,
                                  AKVCAM_DEVICE_TYPE type,
                                  AKVCAM_RW_MODE rw_mode,
                                  akvcam_formats_list_t formats,
                                  akvcam_frame_ct default_frame,
                                  akvcam_frame_filter_ct frame_filter)
{
    akvcam_device_t self = kzalloc(sizeof(struct akvcam_device), GFP_KERNEL);
    kref_init(&self->ref);
    self->type = type;
    self->name = akvcam_strdup(name, AKVCAM_MEMORY_TYPE_KMALLOC);
    self->description = akvcam_strdup(description, AKVCAM_MEMORY_TYPE_KMALLOC);
    self->formats = akvcam_list_new_copy(formats);
    /* No configured formats: accept any supported format at runtime. */
    self->dynamic_formats = akvcam_list_empty(formats);
    self->format = self->dynamic_formats?
                     akvcam_format_dynamic_default():
                     akvcam_format_new_copy(akvcam_list_front(formats));
    self->controls = akvcam_controls_new(type);
    self->connected_devices = akvcam_list_new();
    self->buffer_type = akvcam_device_v4l2_from_device_type(type);
    self->buffers = akvcam_buffers_new(rw_mode, self->buffer_type);
    self->current_frame = NULL;
    self->default_frame = default_frame;
    self->frame_filter = frame_filter;
    self->rw_mode = rw_mode;
    self->videonr = -1;
    mutex_init(&self->device_mutex);
    mutex_init(&self->frame_mutex);
    mutex_init(&self->clock_mutex);
    mutex_init(&self->connected_mutex);
    atomic_set(&self->open_count, 0);
    self->elastic_max = AKVCAM_ELASTIC_DEFAULT_MAX;
    INIT_DELAYED_WORK(&self->elastic_work, akvcam_device_elastic_work);

    self->in_video_converter = akvcam_converter_new();
    self->out_video_converter = akvcam_converter_new();
    akvcam_buffers_set_format(self->buffers, self->format);
    memset(&self->v4l2_dev, 0, sizeof(struct v4l2_device));
    snprintf(self->v4l2_dev.name,
             sizeof(self->v4l2_dev.name),
             "akvcam-device-%u", (uint) akvcam_id());
    akvcam_connect(controls, self->controls, updated, self, akvcam_device_controls_updated);
    akvcam_connect(buffers, self->buffers, streaming_started, self, akvcam_device_clock_start);
    akvcam_connect(buffers, self->buffers, streaming_stopped, self, akvcam_device_stop_streaming);

    return self;
}

static void akvcam_device_free(struct kref *ref)
{
    akvcam_device_t self = container_of(ref, struct akvcam_device, ref);

    cancel_delayed_work_sync(&self->elastic_work);
    akvcam_converter_delete(self->in_video_converter);
    akvcam_converter_delete(self->out_video_converter);
    akvcam_frame_delete(self->current_frame);
    akvcam_buffers_delete(self->buffers);
    akvcam_device_unregister(self);
    akvcam_list_delete(self->connected_devices);
    akvcam_controls_delete(self->controls);
    akvcam_format_delete(self->format);
    akvcam_list_delete(self->formats);
    kfree(self->description);
    kfree(self->name);
    kfree(self);
}

void akvcam_device_delete(akvcam_device_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_device_free);
}

akvcam_device_t akvcam_device_ref(akvcam_device_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

bool akvcam_device_register(akvcam_device_t self)
{
    int result;

    if (self->vdev)
        return true;

    result = v4l2_device_register(NULL, &self->v4l2_dev);

    if (result == 0) {
        struct vb2_queue *queue = akvcam_buffers_vb2_queue(self->buffers);
        result = vb2_queue_init(queue);

        if (result == 0) {
            self->vdev = video_device_alloc();
            snprintf(self->vdev->name, 32, "%s", self->name);
            self->vdev->v4l2_dev = &self->v4l2_dev;
            self->vdev->vfl_type = VFL_TYPE_VIDEO;
            self->vdev->vfl_dir =
                    self->type == AKVCAM_DEVICE_TYPE_OUTPUT? VFL_DIR_TX: VFL_DIR_RX;
            self->vdev->minor = -1;
            self->vdev->fops = &akvcam_device_fops;
            self->vdev->ioctl_ops = akvcam_ioctl_ops();
            self->vdev->tvnorms = V4L2_STD_ALL;
            self->vdev->release = akvcam_device_vdev_release;
            self->vdev->queue = queue;
            self->vdev->lock = &self->device_mutex;
            self->vdev->ctrl_handler =
                    self->direct_mode?
                        NULL:
                        akvcam_controls_handler(self->controls);
            self->vdev->dev.groups = akvcam_attributes_groups(self->type);
            video_set_drvdata(self->vdev, self);
            self->vdev->device_caps = akvcam_device_caps(self);
            result = video_register_device(self->vdev,
                                           VFL_TYPE_VIDEO,
                                           self->videonr);

            if (result) {
                v4l2_device_unregister(&self->v4l2_dev);
                video_device_release(self->vdev);
                self->vdev = NULL;
            } else {
                /* The video device keeps the device alive until its last
                 * user closes it (see akvcam_device_vdev_release). */
                akvcam_device_ref(self);
            }
        } else {
            v4l2_device_unregister(&self->v4l2_dev);
        }
    }

    akvcam_set_last_error(result);

    return self->vdev;
}

/* Called by the V4L2 core when the last reference to the video device is
 * gone, that is, after unregistering it and closing every open file. */
static void akvcam_device_vdev_release(struct video_device *vdev)
{
    akvcam_device_t self = video_get_drvdata(vdev);

    video_device_release(vdev);
    v4l2_device_unregister(&self->v4l2_dev);
    akvcam_device_delete(self);
}

void akvcam_device_unregister(akvcam_device_t self)
{
    struct video_device *vdev = self->vdev;

    if (!vdev)
        return;

    self->vdev = NULL;
    video_unregister_device(vdev);
}

int32_t akvcam_device_num(akvcam_device_ct self)
{
    return self->vdev?
                self->vdev->num:
                self->videonr;
}

void akvcam_device_set_num(akvcam_device_t self, int32_t num)
{
    self->videonr = num;
}

bool akvcam_device_is_registered(akvcam_device_ct self)
{
    return self->vdev;
}

const char *akvcam_device_description(akvcam_device_t self)
{
    return self->description;
}

AKVCAM_DEVICE_TYPE akvcam_device_type(akvcam_device_ct self)
{
    return self->type;
}

enum v4l2_buf_type akvcam_device_v4l2_type(akvcam_device_ct self)
{
    return self->buffer_type;
}

AKVCAM_RW_MODE akvcam_device_rw_mode(akvcam_device_ct self)
{
    return self->rw_mode;
}

bool akvcam_device_direct_mode(akvcam_device_ct self)
{
    return self->direct_mode;
}

void akvcam_device_set_direct_mode(akvcam_device_t self, bool direct_mode)
{
    self->direct_mode = direct_mode;
}

bool akvcam_device_dynamic_formats(akvcam_device_ct self)
{
    return self->dynamic_formats;
}

akvcam_formats_list_t akvcam_device_formats(akvcam_device_ct self)
{
    return akvcam_list_new_copy(self->formats);
}

akvcam_format_t akvcam_device_format_nr(akvcam_device_ct self)
{
    return self->format;
}

akvcam_format_t akvcam_device_format(akvcam_device_ct self)
{
    return akvcam_format_ref(self->format);
}

void akvcam_device_set_format(akvcam_device_t self, akvcam_format_t format)
{
    akvcam_format_copy(self->format, format);
    akvcam_buffers_set_format(self->buffers, format);
}

akvcam_controls_t akvcam_device_controls_nr(akvcam_device_ct self)
{
    return self->controls;
}

akvcam_controls_t akvcam_device_controls(akvcam_device_ct self)
{
    return akvcam_controls_ref(self->controls);
}

akvcam_buffers_t akvcam_device_buffers_nr(akvcam_device_ct self)
{
    return self->buffers;
}

akvcam_buffers_t akvcam_device_buffers(akvcam_device_ct self)
{
    return akvcam_buffers_ref(self->buffers);
}

AKVCAM_DEVICE_TYPE akvcam_device_type_from_v4l2(enum v4l2_buf_type type)
{
    switch (type) {
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
        return AKVCAM_DEVICE_TYPE_OUTPUT;

    default:
        return AKVCAM_DEVICE_TYPE_CAPTURE;
    }
}

enum v4l2_buf_type akvcam_device_v4l2_from_device_type(AKVCAM_DEVICE_TYPE type)
{
    if (type == AKVCAM_DEVICE_TYPE_CAPTURE)
        return V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (type == AKVCAM_DEVICE_TYPE_OUTPUT)
        return V4L2_BUF_TYPE_VIDEO_OUTPUT;

    return (enum v4l2_buf_type) 0;
}

int akvcam_device_controls_updated(akvcam_device_t self, __u32 id, __s32 value)
{
    akvcam_list_element_t it = NULL;

    switch (id) {
    case V4L2_CID_BRIGHTNESS:
        self->brightness = value;
        break;

    case V4L2_CID_CONTRAST:
        self->contrast = value;
        break;

    case V4L2_CID_SATURATION:
        self->saturation = value;
        break;

    case V4L2_CID_HUE:
        self->hue = value;
        break;

    case V4L2_CID_GAMMA:
        self->gamma = value;
        break;

    case V4L2_CID_HFLIP:
        self->horizontal_flip = value;
        break;

    case V4L2_CID_VFLIP:
        self->vertical_flip = value;
        break;

    case V4L2_CID_COLORFX:
        self->gray = value == V4L2_COLORFX_BW;
        break;

    case AKVCAM_CID_SCALING:
        self->scaling = value;
        break;

    case AKVCAM_CID_ASPECT_RATIO:
        self->aspect_ratio = value;
        break;

    case AKVCAM_CID_SWAP_RGB:
        self->swap_rgb = value;
        break;

    default:
        break;
    }

    if (self->type != AKVCAM_DEVICE_TYPE_CAPTURE) {
        mutex_lock(&self->connected_mutex);

        for (;;) {
            akvcam_device_t capture_device
                    = akvcam_list_next(self->connected_devices, &it);

            if (!it)
                break;

            capture_device->horizontal_flip = self->horizontal_flip;
            capture_device->vertical_flip = self->vertical_flip;
            capture_device->scaling = self->scaling;
            capture_device->aspect_ratio = self->aspect_ratio;
            capture_device->swap_rgb = self->swap_rgb;
        }

        mutex_unlock(&self->connected_mutex);
    }

    return 0;
}

int akvcam_device_stop_streaming(akvcam_device_t self)
{
    akvcam_device_clock_stop(self);

    if (!mutex_lock_interruptible(&self->frame_mutex)) {
        akvcam_frame_delete(self->current_frame);
        self->current_frame = NULL;
        mutex_unlock(&self->frame_mutex);
    }

    if (self->elastic)
        self->elastic_stop_time = jiffies;

    akvcam_device_elastic_kick(self);

    return 0;
}

bool akvcam_device_streaming(akvcam_device_ct self)
{
    return self->thread != NULL;
}

akvcam_devices_list_t akvcam_device_connected_devices_nr(akvcam_device_ct self)
{
    return self->connected_devices;
}

akvcam_devices_list_t akvcam_device_connected_devices(akvcam_device_ct self)
{
    return akvcam_list_ref(self->connected_devices);
}

__u32 akvcam_device_caps(akvcam_device_ct self)
{
    __u32 caps = 0;

    switch (self->buffer_type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        caps = V4L2_CAP_VIDEO_CAPTURE;
        break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    default:
        caps = V4L2_CAP_VIDEO_OUTPUT;
        break;
    }

    if (self->rw_mode & AKVCAM_RW_MODE_READWRITE)
        caps |= V4L2_CAP_READWRITE;

    if (self->rw_mode & AKVCAM_RW_MODE_MMAP
        || self->rw_mode & AKVCAM_RW_MODE_USERPTR
        || self->rw_mode & AKVCAM_RW_MODE_DMABUF)
        caps |= V4L2_CAP_STREAMING;

    caps |= V4L2_CAP_EXT_PIX_FORMAT;

    return caps;
}

void akvcam_device_clock_run_once(akvcam_device_t self)
{
    bool using_default = false;

    akpr_function();

    if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE) {
        akvcam_frame_t frame = NULL;
        akvcam_frame_t adjusted_frame;
        akvcam_device_t output_device;
        int result;

        mutex_lock(&self->connected_mutex);
        output_device = akvcam_list_front(self->connected_devices);
        mutex_unlock(&self->connected_mutex);

        if (!mutex_lock_interruptible(&self->frame_mutex)) {
            if (output_device
                && output_device->thread != NULL
                && self->current_frame) {
                akpr_debug("Reading current frame.\n");
                frame = akvcam_frame_new_copy(self->current_frame);
            }

            mutex_unlock(&self->frame_mutex);
        }

        /* In direct mode the frame is copied as is, so it must match the
         * format of this device. An elastic device can outlive the stream
         * that created it; in that case show the default frame. */
        if (frame
            && self->direct_mode
            && !akvcam_format_is_same_format(akvcam_frame_format_nr(frame),
                                             self->format)) {
            akvcam_frame_delete(frame);
            frame = NULL;
        }

        if (!frame) {
            if (self->default_frame && akvcam_frame_size(self->default_frame) > 0) {
                akpr_debug("Reading default frame.\n");
                frame = akvcam_frame_new_copy(self->default_frame);
                using_default = true;
            } else {
                akpr_debug("Generating random frame.\n");
                frame = akvcam_frame_new(self->format);
                get_random_bytes(akvcam_frame_data(frame),
                                 (int) akvcam_frame_size(frame));
            }
        }

        if (self->direct_mode) {
            /* In direct mode: skip all adjustments and format conversion,
             * write the frame as-is directly to the capture buffer.
             * Exception: The default frames must still be converted to the
             * capture device format because they may have been created in a
             * different pixel format. */
            if (!using_default) {
                /* Frame came from the output device - formats are guaranteed
                 * to match (enforced at connect time), copy directly. */
                result = akvcam_buffers_write_frame(self->buffers, frame);
            } else {
                /* Fallback frame: convert to capture format first. */
                akvcam_frame_t converted;

                akvcam_converter_set_output_format(self->out_video_converter, self->format);
                akvcam_converter_set_scaling_mode(self->out_video_converter, self->scaling);
                akvcam_converter_set_aspect_ratio_mode(self->out_video_converter, self->aspect_ratio);

                akvcam_converter_begin(self->out_video_converter);
                converted =
                    akvcam_converter_convert(self->out_video_converter, frame);
                akvcam_converter_end(self->out_video_converter);
                result = akvcam_buffers_write_frame(self->buffers, converted);
                akvcam_frame_delete(converted);
            }

            adjusted_frame = NULL;
        } else {
            adjusted_frame = akvcam_device_frame_apply_adjusts(self, frame);
            result = akvcam_buffers_write_frame(self->buffers, adjusted_frame);
        }

        akvcam_frame_delete(frame);

        if (result < 0) {
            char *error_str = kzalloc(AKVCAM_MAX_STRING_SIZE, GFP_KERNEL);

            akvcam_string_from_error(result, error_str, AKVCAM_MAX_STRING_SIZE);
            akpr_err("Failed writing frame: %s.\n", error_str);
            kfree(error_str);
        }

        if (adjusted_frame)
            akvcam_frame_delete(adjusted_frame);
    } else {
        akvcam_list_element_t it = NULL;
        akvcam_frame_t frame = akvcam_buffers_read_frame(self->buffers);

        if (frame) {
            mutex_lock(&self->connected_mutex);

            for (;;) {
                akvcam_device_t capture_device =
                        akvcam_list_next(self->connected_devices, &it);

                if (!it)
                    break;

                if (!mutex_lock_interruptible(&capture_device->frame_mutex)) {
                    akvcam_frame_delete(capture_device->current_frame);
                    capture_device->current_frame = akvcam_frame_new_copy(frame);
                    mutex_unlock(&capture_device->frame_mutex);
                }
            }

            mutex_unlock(&self->connected_mutex);
            akvcam_frame_delete(frame);
        }
    }
}

int akvcam_device_clock_start(akvcam_device_t self)
{
    int result;

    akvcam_device_clock_stop(self);
    result = mutex_lock_interruptible(&self->clock_mutex);

    if (result)
        return result;

    self->thread = kthread_run((akvcam_thread_t)
                               akvcam_device_clock_timeout,
                               self,
                               "akvcam-thread-%llu",
                               akvcam_id());

    if (IS_ERR(self->thread)) {
        result = PTR_ERR(self->thread);
        self->thread = NULL;
    }

    mutex_unlock(&self->clock_mutex);

    if (result == 0)
        akvcam_device_elastic_kick(self);

    return result;
}

void akvcam_device_clock_stop(akvcam_device_t self)
{
    if (mutex_lock_interruptible(&self->clock_mutex))
        return;

    if (self->thread) {
        kthread_stop(self->thread);
        self->thread = NULL;
    }

    mutex_unlock(&self->clock_mutex);
}

int akvcam_device_clock_timeout(akvcam_device_t self)
{
    struct v4l2_fract frame_rate = akvcam_format_frame_rate(self->format);
    __u32 tsleep = 1000 * frame_rate.denominator;

    if (frame_rate.numerator)
        tsleep /= frame_rate.numerator;

    while (!kthread_should_stop()) {
        akvcam_device_clock_run_once(self);

        /* In direct_mode output devices, yield immediately to minimise
         * frame-delivery latency instead of sleeping a full frame period. */
        if (self->direct_mode && self->type == AKVCAM_DEVICE_TYPE_OUTPUT)
            schedule();
        else
            msleep_interruptible(tsleep);
    }

    return 0;
}

akvcam_frame_t akvcam_device_frame_apply_adjusts(akvcam_device_ct self,
                                                 akvcam_frame_ct frame)
{
    bool horizontal_flip = self->horizontal_flip != self->horizontal_mirror;
    bool vertical_flip = self->vertical_flip != self->vertical_mirror;
    akvcam_format_t frame_fmt;
    akvcam_format_t iformat;
    akvcam_frame_t iframe;
    akvcam_frame_t oframe;
    struct v4l2_fract frame_rate;

    akpr_function();

    akpr_debug("brightness: %d\n", self->brightness);
    akpr_debug("contrast: %d\n", self->contrast);
    akpr_debug("gamma: %d\n", self->gamma);
    akpr_debug("saturation: %d\n", self->saturation);
    akpr_debug("hue: %d\n", self->hue);
    akpr_debug("gray: %s\n", self->gray? "true": "false");
    akpr_debug("horizontal_mirror: %s\n", self->horizontal_mirror? "true": "false");
    akpr_debug("vertical_mirror: %s\n", self->vertical_mirror? "true": "false");
    akpr_debug("swap_rgb: %s\n", self->swap_rgb? "true": "false");
    akpr_debug("horizontal_flip: %s\n", self->horizontal_flip? "true": "false");
    akpr_debug("vertical_flip: %s\n", self->vertical_flip? "true": "false");
    akpr_debug("scaling: %s\n", akvcam_converter_scaling_mode_to_string(self->scaling));
    akpr_debug("aspect_ratio: %s\n", akvcam_converter_aspect_ratio_mode_to_string(self->aspect_ratio));

    frame_fmt = akvcam_frame_format_nr(frame);
    frame_rate = akvcam_format_frame_rate(frame_fmt);
    iformat = akvcam_format_new(V4L2_PIX_FMT_ARGB32,
                                akvcam_format_width(frame_fmt),
                                akvcam_format_height(frame_fmt),
                                &frame_rate);
    akvcam_converter_set_output_format(self->in_video_converter, iformat);
    akvcam_converter_set_scaling_mode(self->in_video_converter, self->scaling);
    akvcam_converter_set_aspect_ratio_mode(self->in_video_converter, self->aspect_ratio);
    akvcam_format_delete(iformat);

    akvcam_converter_begin(self->in_video_converter);
    iframe = akvcam_converter_convert(self->in_video_converter, frame);
    akvcam_converter_end(self->in_video_converter);

    akvcam_frame_filter_mirror(iframe,
                               horizontal_flip,
                               vertical_flip);
    akvcam_frame_filter_apply(self->frame_filter,
                              iframe,
                              self->hue,
                              self->saturation,
                              self->brightness,
                              self->contrast,
                              self->gamma,
                              self->gray,
                              self->swap_rgb);

    akvcam_converter_set_output_format(self->out_video_converter, self->format);
    akvcam_converter_set_scaling_mode(self->out_video_converter, self->scaling);
    akvcam_converter_set_aspect_ratio_mode(self->out_video_converter, self->aspect_ratio);

    akvcam_converter_begin(self->out_video_converter);
    oframe = akvcam_converter_convert(self->out_video_converter, iframe);
    akvcam_converter_end(self->out_video_converter);
    akvcam_frame_delete(iframe);

    return oframe;
}

static int akvcam_device_open(struct file *file)
{
    akvcam_device_t self = video_drvdata(file);
    int result = v4l2_fh_open(file);

    if (result == 0)
        atomic_inc(&self->open_count);

    return result;
}

static int akvcam_device_release(struct file *file)
{
    akvcam_device_t self = video_drvdata(file);
    int result = vb2_fop_release(file);

    atomic_dec(&self->open_count);
    akvcam_device_elastic_kick(self);

    return result;
}

/* ---------------------------------------------------------------------------
 * Elastic devices
 *
 * An output device with the elastic property creates capture devices on
 * demand: while the output is streaming there is always exactly one capture
 * device without a streaming client (the spare one). As soon as a client
 * starts streaming from the spare device a new spare is created, and when a
 * client leaves the surplus spare devices are removed. A client is attached
 * when it starts streaming, not when it opens the device, because many
 * applications open every video device just to query its capabilities.
 *
 * When the output stops, devices without open files are removed after a
 * short delay. Devices still in use keep showing the default frame and are
 * removed when their client closes them. If the output starts again they
 * are fed again, as long as their format is still compatible.
 *
 * All the changes are done by a single ordered work queue, because the
 * devices can't be registered or removed from the ioctls that trigger them.
 * ------------------------------------------------------------------------ */

static bool akvcam_device_same_device(const void *element_data,
                                      const void *data)
{
    return element_data == data;
}

static void akvcam_device_elastic_kick(akvcam_device_t self)
{
    akvcam_device_t output = self->elastic? self: self->elastic_owner;
    struct workqueue_struct *wq;

    if (!output || !output->elastic)
        return;

    wq = akvcam_driver_elastic_wq();

    if (wq)
        mod_delayed_work(wq, &output->elastic_work, 0);
}

static bool akvcam_device_elastic_is_idle(akvcam_device_t capture)
{
    return atomic_read(&capture->open_count) < 1
           && !akvcam_device_streaming(capture);
}

static akvcam_devices_list_t akvcam_device_connected_snapshot(akvcam_device_t self)
{
    akvcam_devices_list_t snapshot;

    mutex_lock(&self->connected_mutex);
    snapshot = akvcam_list_new_copy(self->connected_devices);
    mutex_unlock(&self->connected_mutex);

    return snapshot;
}

static void akvcam_device_elastic_remove(akvcam_device_t self,
                                         akvcam_device_t capture)
{
    akvcam_list_element_t it;

    akpr_info("Removing elastic device '%s %u'\n",
              self->description,
              capture->elastic_index);

    mutex_lock(&self->connected_mutex);
    it = akvcam_list_find(self->connected_devices,
                          capture,
                          akvcam_device_same_device);

    if (it)
        akvcam_list_erase(self->connected_devices, it);

    mutex_unlock(&self->connected_mutex);

    akvcam_driver_remove_device(capture);

    /* Break the reference cycle with the output device. */
    mutex_lock(&capture->connected_mutex);
    akvcam_list_clear(capture->connected_devices);
    mutex_unlock(&capture->connected_mutex);

    akvcam_device_unregister(capture);
}

static akvcam_device_t akvcam_device_elastic_create(akvcam_device_t self,
                                                    unsigned int index)
{
    akvcam_formats_list_t formats;
    akvcam_format_t output_format;
    akvcam_device_t capture;
    char *description;

    description = kasprintf(GFP_KERNEL, "%s %u", self->description, index);

    if (!description)
        return NULL;

    output_format = akvcam_format_new_copy(self->format);

    if (self->direct_mode) {
        /* The only format offered is the one the output is streaming. */
        formats = akvcam_list_new();
        akvcam_list_push_back(formats,
                              output_format,
                              (akvcam_copy_t) akvcam_format_ref,
                              (akvcam_delete_t) akvcam_format_delete);
    } else {
        /* Same formats as the output; no formats means dynamic formats. */
        formats = akvcam_list_new_copy(self->formats);
    }

    capture = akvcam_device_new(self->name,
                                description,
                                AKVCAM_DEVICE_TYPE_CAPTURE,
                                self->rw_mode,
                                formats,
                                self->default_frame,
                                self->frame_filter);
    akvcam_list_delete(formats);
    kfree(description);

    if (!capture) {
        akvcam_format_delete(output_format);

        return NULL;
    }

    capture->direct_mode = self->direct_mode;
    capture->elastic_owner = self;
    capture->elastic_index = index;
    capture->horizontal_flip = self->horizontal_flip;
    capture->vertical_flip = self->vertical_flip;
    capture->scaling = self->scaling;
    capture->aspect_ratio = self->aspect_ratio;
    capture->swap_rgb = self->swap_rgb;
    akvcam_device_set_format(capture, output_format);
    akvcam_format_delete(output_format);

    /* Connect both devices; every list owns a reference. */
    mutex_lock(&capture->connected_mutex);
    akvcam_list_push_back(capture->connected_devices,
                          self,
                          (akvcam_copy_t) akvcam_device_ref,
                          (akvcam_delete_t) akvcam_device_delete);
    mutex_unlock(&capture->connected_mutex);
    mutex_lock(&self->connected_mutex);
    akvcam_list_push_back(self->connected_devices,
                          capture,
                          (akvcam_copy_t) akvcam_device_ref,
                          (akvcam_delete_t) akvcam_device_delete);
    mutex_unlock(&self->connected_mutex);
    akvcam_driver_add_device(capture);

    if (!akvcam_device_register(capture)) {
        akpr_err("Failed to register elastic device '%s %u'\n",
                 self->description,
                 index);
        akvcam_device_elastic_remove(self, capture);
        akvcam_device_delete(capture);

        return NULL;
    }

    akpr_info("Created elastic device '%s %u' as /dev/video%d\n",
              self->description,
              index,
              akvcam_device_num(capture));

    /* The lists keep the device alive. */
    akvcam_device_delete(capture);

    return capture;
}

static void akvcam_device_elastic_reconcile(akvcam_device_t self)
{
    akvcam_device_t captures[AKVCAM_ELASTIC_MAX_LIMIT];
    bool spare[AKVCAM_ELASTIC_MAX_LIMIT];
    akvcam_devices_list_t snapshot;
    akvcam_list_element_t it = NULL;
    akvcam_format_t output_format = NULL;
    struct workqueue_struct *wq = akvcam_driver_elastic_wq();
    size_t ncaptures = 0;
    size_t nspares = 0;
    size_t total;
    size_t i;
    size_t j;
    bool streaming = akvcam_device_streaming(self);

    if (!self->elastic || !wq)
        return;

    if (!streaming) {
        /* Give the output a chance to start again before removing
         * anything, so a quick restart doesn't make devices flicker. */
        unsigned long deadline =
                self->elastic_stop_time
                + msecs_to_jiffies(AKVCAM_ELASTIC_STOP_DELAY_MS);

        if (time_before(jiffies, deadline)) {
            queue_delayed_work(wq, &self->elastic_work, deadline - jiffies);

            return;
        }
    }

    snapshot = akvcam_device_connected_snapshot(self);

    /* Elastic capture devices, sorted by index. */
    for (;;) {
        akvcam_device_t capture = akvcam_list_next(snapshot, &it);

        if (!it)
            break;

        if (capture->elastic_owner != self
            || ncaptures >= AKVCAM_ELASTIC_MAX_LIMIT)
            continue;

        for (i = ncaptures; i > 0
             && captures[i - 1]->elastic_index > capture->elastic_index; i--)
            captures[i] = captures[i - 1];

        captures[i] = capture;
        ncaptures++;
    }

    total = ncaptures;

    if (!streaming) {
        /* Remove the devices nobody is using. */
        for (i = 0; i < ncaptures; i++)
            if (akvcam_device_elastic_is_idle(captures[i])) {
                akvcam_device_elastic_remove(self, captures[i]);
                captures[i] = NULL;
            }

        goto elastic_reconcile_done;
    }

    if (self->direct_mode)
        output_format = akvcam_format_new_copy(self->format);

    for (i = 0; i < ncaptures; i++) {
        spare[i] = false;

        if (akvcam_device_streaming(captures[i]))
            continue;

        if (output_format) {
            akvcam_format_t capture_format =
                    akvcam_format_new_copy(captures[i]->format);
            bool compatible =
                    akvcam_format_is_same_format(capture_format, output_format);

            akvcam_format_delete(capture_format);

            if (!compatible) {
                /* Stale format from a previous stream. */
                if (akvcam_device_elastic_is_idle(captures[i])) {
                    akvcam_device_elastic_remove(self, captures[i]);
                    captures[i] = NULL;
                    total--;
                }

                continue;
            }
        }

        spare[i] = true;
        nspares++;
    }

    akvcam_format_delete(output_format);

    /* Keep a single spare device: the surplus ones are removed, starting
     * from the highest index. */
    for (i = ncaptures; i > 0 && nspares > 1; i--) {
        j = i - 1;

        if (!captures[j] || !spare[j])
            continue;

        if (akvcam_device_elastic_is_idle(captures[j])) {
            akvcam_device_elastic_remove(self, captures[j]);
            captures[j] = NULL;
            spare[j] = false;
            nspares--;
            total--;
        }
    }

    if (nspares < 1 && total < (size_t) self->elastic_max) {
        unsigned int index = 1;

        /* Lowest index not in use. */
        for (i = 0; i < ncaptures; i++)
            if (captures[i] && captures[i]->elastic_index == index)
                index++;

        akvcam_device_elastic_create(self, index);
    }

elastic_reconcile_done:
    akvcam_list_delete(snapshot);
}

static void akvcam_device_elastic_work(struct work_struct *work)
{
    akvcam_device_t self = container_of(to_delayed_work(work),
                                        struct akvcam_device,
                                        elastic_work);

    akvcam_device_elastic_reconcile(self);
}

bool akvcam_device_elastic(akvcam_device_ct self)
{
    return self->elastic;
}

int akvcam_device_elastic_max(akvcam_device_ct self)
{
    return self->elastic_max;
}

void akvcam_device_set_elastic(akvcam_device_t self,
                               bool elastic,
                               int max_devices)
{
    if (self->type != AKVCAM_DEVICE_TYPE_OUTPUT)
        return;

    self->elastic = elastic;
    self->elastic_max = clamp(max_devices, 1, AKVCAM_ELASTIC_MAX_LIMIT);
}

void akvcam_device_elastic_shutdown(akvcam_device_t self)
{
    akvcam_devices_list_t snapshot;
    akvcam_list_element_t it = NULL;

    if (!self->elastic)
        return;

    self->elastic = false;
    cancel_delayed_work_sync(&self->elastic_work);
    snapshot = akvcam_device_connected_snapshot(self);

    for (;;) {
        akvcam_device_t capture = akvcam_list_next(snapshot, &it);

        if (!it)
            break;

        if (capture->elastic_owner == self)
            akvcam_device_elastic_remove(self, capture);
    }

    akvcam_list_delete(snapshot);
}

void akvcam_device_connected_lock(akvcam_device_t self)
{
    mutex_lock(&self->connected_mutex);
}

void akvcam_device_connected_unlock(akvcam_device_t self)
{
    mutex_unlock(&self->connected_mutex);
}

static const struct v4l2_file_operations akvcam_device_fops = {
    .owner          = THIS_MODULE    ,
    .open           = akvcam_device_open   ,
    .release        = akvcam_device_release,
    .unlocked_ioctl = video_ioctl2   ,
    .read           = vb2_fop_read   ,
    .write          = vb2_fop_write  ,
    .mmap           = vb2_fop_mmap   ,
    .poll           = vb2_fop_poll   ,
};
