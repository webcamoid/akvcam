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
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>

#include "device.h"
#include "attributes.h"
#include "buffers.h"
#include "controls.h"
#include "driver.h"
#include "format.h"
#include "frame.h"
#include "global_deleter.h"
#include "ioctl.h"
#include "list.h"
#include "log.h"
#include "settings.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
#define VFL_TYPE_VIDEO VFL_TYPE_GRABBER
#endif

struct akvcam_device
{
    struct kref ref;
    char *name;
    char *description;
    akvcam_formats_list_t formats;
    akvcam_format_t format;
    akvcam_controls_t controls;
    akvcam_devices_list_t connected_devices;
    akvcam_buffers_t buffers;
    akvcam_frame_t current_frame;
    struct mutex device_mutex;
    struct mutex frame_mutex;
    struct mutex clock_mutex;
    struct v4l2_device v4l2_dev;
    struct video_device *vdev;
    struct task_struct *thread;
    AKVCAM_DEVICE_TYPE type;
    enum v4l2_buf_type buffer_type;
    AKVCAM_RW_MODE rw_mode;
    int32_t videonr;

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
    AKVCAM_SCALING scaling;
    AKVCAM_ASPECT_RATIO aspect_ratio;
};

typedef int (*akvcam_thread_t)(void *data);
static const struct v4l2_file_operations akvcam_device_fops;

enum v4l2_buf_type akvcam_device_v4l2_from_device_type(AKVCAM_DEVICE_TYPE type,
                                                       bool multiplanar);
void akvcam_device_controls_updated(akvcam_device_t self, __u32 id, __s32 value);
void akvcam_device_stop_streaming(akvcam_device_t self);
int akvcam_device_clock_timeout(akvcam_device_t self);
void akvcam_device_clock_run_once(akvcam_device_t self);
int akvcam_device_clock_start(akvcam_device_t self);
void akvcam_device_clock_stop(akvcam_device_t self);
int akvcam_device_clock_timeout(akvcam_device_t self);
akvcam_frame_t akvcam_device_frame_apply_adjusts(akvcam_device_ct self,
                                                 akvcam_frame_ct frame);
akvcam_frame_t akvcam_default_frame(void);

akvcam_device_t akvcam_device_new(const char *name,
                                  const char *description,
                                  AKVCAM_DEVICE_TYPE type,
                                  AKVCAM_RW_MODE rw_mode,
                                  akvcam_formats_list_t formats)
{
    bool multiplanar;

    akvcam_device_t self = kzalloc(sizeof(struct akvcam_device), GFP_KERNEL);
    kref_init(&self->ref);
    self->type = type;
    self->name = akvcam_strdup(name, AKVCAM_MEMORY_TYPE_KMALLOC);
    self->description = akvcam_strdup(description, AKVCAM_MEMORY_TYPE_KMALLOC);
    self->formats = akvcam_list_new_copy(formats);
    self->format = akvcam_format_new_copy(akvcam_list_front(formats));
    self->controls = akvcam_controls_new(type);
    self->connected_devices = akvcam_list_new();
    multiplanar = akvcam_format_have_multiplanar(formats);
    self->buffer_type = akvcam_device_v4l2_from_device_type(type, multiplanar);
    self->buffers = akvcam_buffers_new(rw_mode, self->buffer_type);
    self->rw_mode = rw_mode;
    self->videonr = -1;
    mutex_init(&self->device_mutex);
    mutex_init(&self->frame_mutex);
    mutex_init(&self->clock_mutex);

    akvcam_buffers_set_format(self->buffers, self->format);
    memset(&self->v4l2_dev, 0, sizeof(struct v4l2_device));
    snprintf(self->v4l2_dev.name,
             V4L2_DEVICE_NAME_SIZE,
             "akvcam-device-%u", (uint) akvcam_id());
    akvcam_connect(controls, self->controls, updated, self, akvcam_device_controls_updated);
    akvcam_connect(buffers, self->buffers, streaming_started, self, akvcam_device_clock_start);
    akvcam_connect(buffers, self->buffers, streaming_stopped, self, akvcam_device_stop_streaming);

    // Preload deault frame otherwise it will not get loaded in RW mode.
    akvcam_default_frame();

    return self;
}

static void akvcam_device_free(struct kref *ref)
{
    akvcam_device_t self = container_of(ref, struct akvcam_device, ref);

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
            self->vdev->release = video_device_release_empty;
            self->vdev->queue = queue;
            self->vdev->lock = &self->device_mutex;
            self->vdev->ctrl_handler = akvcam_controls_handler(self->controls);
            self->vdev->dev.groups = akvcam_attributes_groups(self->type);
            video_set_drvdata(self->vdev, self);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
            self->vdev->device_caps = akvcam_device_caps(self);
#endif

            result = video_register_device(self->vdev,
                                           VFL_TYPE_VIDEO,
                                           self->videonr);

            if (result) {
                v4l2_device_unregister(&self->v4l2_dev);
                video_device_release(self->vdev);
                self->vdev = NULL;
            }
        } else {
            v4l2_device_unregister(&self->v4l2_dev);
        }
    }

    akvcam_set_last_error(result);

    return self->vdev;
}

void akvcam_device_unregister(akvcam_device_t self)
{
    if (!self->vdev)
        return;

    video_unregister_device(self->vdev);
    video_device_release(self->vdev);
    self->vdev = NULL;

    v4l2_device_unregister(&self->v4l2_dev);
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

akvcam_formats_list_t akvcam_device_formats(akvcam_device_ct self)
{
    return akvcam_list_new_copy(self->formats);
}

akvcam_format_t akvcam_device_format(akvcam_device_ct self)
{
    return akvcam_format_new_copy(self->format);
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
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
        return AKVCAM_DEVICE_TYPE_OUTPUT;

    default:
        return AKVCAM_DEVICE_TYPE_CAPTURE;
    }
}

enum v4l2_buf_type akvcam_device_v4l2_from_device_type(AKVCAM_DEVICE_TYPE type,
                                                       bool multiplanar)
{
    if (type == AKVCAM_DEVICE_TYPE_CAPTURE && multiplanar)
        return V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    else if (type == AKVCAM_DEVICE_TYPE_CAPTURE && !multiplanar)
        return V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else if (type == AKVCAM_DEVICE_TYPE_OUTPUT && multiplanar)
        return V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    else if (type == AKVCAM_DEVICE_TYPE_OUTPUT && !multiplanar)
        return V4L2_BUF_TYPE_VIDEO_OUTPUT;

    return (enum v4l2_buf_type) 0;
}

void akvcam_device_controls_updated(akvcam_device_t self, __u32 id, __s32 value)
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

    if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE)
        return;

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
}

void akvcam_device_stop_streaming(akvcam_device_t self)
{
    akvcam_device_clock_stop(self);

    if (!mutex_lock_interruptible(&self->frame_mutex)) {
        akvcam_frame_delete(self->current_frame);
        self->current_frame = NULL;
        mutex_unlock(&self->frame_mutex);
    }
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

    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
        caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE;
        break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
        caps = V4L2_CAP_VIDEO_OUTPUT;
        break;

    default:
        caps = V4L2_CAP_VIDEO_OUTPUT_MPLANE;
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
    akvcam_frame_t default_frame = akvcam_default_frame();

    akpr_function();

    if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE) {
        akvcam_frame_t frame = NULL;
        akvcam_frame_t adjusted_frame;
        akvcam_device_t output_device =
                akvcam_list_front(self->connected_devices);
        int result;

        if (!mutex_lock_interruptible(&self->frame_mutex)) {
            if (output_device
                && output_device->thread != NULL
                && self->current_frame) {
                akpr_debug("Reading current frame.\n");
                frame = akvcam_frame_new_copy(self->current_frame);
            }

            mutex_unlock(&self->frame_mutex);
        }

        if (!frame) {
            if (default_frame && akvcam_frame_size(default_frame) > 0) {
                akpr_debug("Reading default frame.\n");
                frame = akvcam_frame_new_copy(default_frame);
            } else {
                akpr_debug("Generating random frame.\n");
                frame = akvcam_frame_new(self->format, NULL, 0);
                get_random_bytes(akvcam_frame_data(frame),
                                 (int) akvcam_frame_size(frame));
            }
        }

        adjusted_frame = akvcam_device_frame_apply_adjusts(self, frame);
        akvcam_frame_delete(frame);
        result = akvcam_buffers_write_frame(self->buffers, adjusted_frame);

        if (result < 0)
            akpr_err("Failed writing frame: %s.\n", akvcam_string_from_error(result));

        akvcam_frame_delete(adjusted_frame);
    } else {
        akvcam_list_element_t it = NULL;
        akvcam_frame_t frame = akvcam_buffers_read_frame(self->buffers);

        if (frame) {
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

    if (!self->thread)
        result = -EIO;

    mutex_unlock(&self->clock_mutex);

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
        msleep_interruptible(tsleep);
    }

    return 0;
}

akvcam_frame_t akvcam_device_frame_apply_adjusts(akvcam_device_ct self,
                                                 akvcam_frame_ct frame)
{
    bool horizontal_flip = self->horizontal_flip != self->horizontal_mirror;
    bool vertical_flip = self->vertical_flip != self->vertical_mirror;
    akvcam_frame_t new_frame = akvcam_frame_new_copy(frame);
    akvcam_format_t frame_format = akvcam_frame_format(frame);
    __u32 fourcc = akvcam_format_fourcc(self->format);
    size_t iwidth = akvcam_format_width(frame_format);
    size_t iheight = akvcam_format_height(frame_format);
    size_t owidth = akvcam_format_width(self->format);
    size_t oheight = akvcam_format_height(self->format);

    akpr_function();
    akvcam_format_delete(frame_format);

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
    akpr_debug("scaling: %s\n", akvcam_frame_scaling_to_string(self->scaling));
    akpr_debug("aspect_ratio: %s\n", akvcam_frame_aspect_ratio_to_string(self->aspect_ratio));

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

akvcam_frame_t akvcam_default_frame(void)
{
    static akvcam_frame_t frame = NULL;
    akvcam_settings_t settings;
    bool loaded = false;

    if (frame)
        return frame;

    settings = akvcam_settings_new();

    if (akvcam_settings_load(settings, akvcam_settings_file())) {
        char *file_name;

        akvcam_settings_begin_group(settings, "General");
        file_name = akvcam_settings_value(settings, "default_frame");
        frame = akvcam_frame_new(NULL, NULL, 0);
        loaded = akvcam_frame_load(frame, file_name);
        akvcam_settings_end_group(settings);
    }

    akvcam_settings_delete(settings);

    if (loaded) {
        akvcam_global_deleter_add(frame,
                                  (akvcam_delete_t) akvcam_frame_delete);
    } else {
        akvcam_frame_delete(frame);
        frame = NULL;
    }

    return frame;
}

static const struct v4l2_file_operations akvcam_device_fops = {
    .owner          = THIS_MODULE    ,
    .open           = v4l2_fh_open   ,
    .release        = vb2_fop_release,
    .unlocked_ioctl = video_ioctl2   ,
    .read           = vb2_fop_read   ,
    .write          = vb2_fop_write  ,
    .mmap           = vb2_fop_mmap   ,
    .poll           = vb2_fop_poll   ,
};
