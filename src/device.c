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
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <media/v4l2-device.h>

#include "device.h"
#include "attributes.h"
#include "buffers.h"
#include "controls.h"
#include "driver.h"
#include "events.h"
#include "format.h"
#include "frame.h"
#include "global_deleter.h"
#include "list.h"
#include "log.h"
#include "node.h"
#include "settings.h"

#ifndef V4L2_CAP_EXT_PIX_FORMAT
#define V4L2_CAP_EXT_PIX_FORMAT 0x00200000
#endif

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
    akvcam_attributes_t attributes;
    akvcam_nodes_list_t nodes;
    akvcam_devices_list_t connected_devices;
    akvcam_node_t priority_node;
    akvcam_buffers_t buffers;
    akvcam_frame_t current_frame;
    struct mutex mtx;
    struct v4l2_device v4l2_dev;
    struct video_device *vdev;
    struct task_struct *thread;
    AKVCAM_DEVICE_TYPE type;
    enum v4l2_buf_type buffer_type;
    AKVCAM_RW_MODE rw_mode;
    enum v4l2_priority priority;
    int32_t videonr;
    int64_t broadcasting_node;
    bool streaming;
    bool streaming_rw;

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

enum v4l2_buf_type akvcam_device_v4l2_from_device_type(AKVCAM_DEVICE_TYPE type,
                                                       bool multiplanar);
void akvcam_device_event_received(akvcam_device_t self,
                                  struct v4l2_event *event);
void akvcam_device_controls_changed(akvcam_device_t self,
                                    struct v4l2_event *event);
int akvcam_device_clock_timeout(akvcam_device_t self);
akvcam_frame_t akvcam_device_frame_apply_adjusts(const akvcam_device_t self,
                                                 akvcam_frame_t frame);
void akvcam_device_notify_frame(akvcam_device_t self);
akvcam_frame_t akvcam_default_frame(void);

akvcam_device_t akvcam_device_new(const char *name,
                                  const char *description,
                                  AKVCAM_DEVICE_TYPE type,
                                  AKVCAM_RW_MODE rw_mode,
                                  akvcam_formats_list_t formats)
{
    akvcam_controls_changed_callback controls_changed;
    bool multiplanar;

    akvcam_device_t self = kzalloc(sizeof(struct akvcam_device), GFP_KERNEL);
    kref_init(&self->ref);
    self->type = type;
    self->name = akvcam_strdup(name, AKVCAM_MEMORY_TYPE_KMALLOC);
    self->description = akvcam_strdup(description, AKVCAM_MEMORY_TYPE_KMALLOC);
    self->formats = akvcam_list_new_copy(formats);
    self->format = akvcam_format_new_copy(akvcam_list_front(formats));
    self->controls = akvcam_controls_new(type);
    self->attributes = akvcam_attributes_new(type);
    self->connected_devices = akvcam_list_new();
    self->nodes = akvcam_list_new();
    multiplanar = akvcam_format_have_multiplanar(formats);
    self->buffer_type = akvcam_device_v4l2_from_device_type(type, multiplanar);
    self->buffers = akvcam_buffers_new(rw_mode, self->buffer_type, multiplanar);
    self->priority_node = NULL;
    self->rw_mode = rw_mode;
    self->videonr = -1;
    self->broadcasting_node = -1;
    self->priority = V4L2_PRIORITY_DEFAULT;
    mutex_init(&self->mtx);

    akvcam_buffers_set_format(self->buffers, self->format);
    memset(&self->v4l2_dev, 0, sizeof(struct v4l2_device));
    snprintf(self->v4l2_dev.name,
             V4L2_DEVICE_NAME_SIZE,
             "akvcam-device-%llu", akvcam_id());
    controls_changed.user_data = self;
    controls_changed.callback =
            (akvcam_controls_changed_proc) akvcam_device_controls_changed;
    akvcam_controls_set_changed_callback(self->controls, controls_changed);

    // Preload deault frame otherwise it will not get loaded in RW mode.
    akvcam_default_frame();

    return self;
}

void akvcam_device_free(struct kref *ref)
{
    akvcam_device_t self = container_of(ref, struct akvcam_device, ref);

    akvcam_frame_delete(self->current_frame);
    akvcam_buffers_delete(self->buffers);
    akvcam_device_unregister(self);
    akvcam_list_delete(self->nodes);
    akvcam_list_delete(self->connected_devices);
    akvcam_attributes_delete(self->attributes);
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

    if (!result) {
        self->vdev = video_device_alloc();
        snprintf(self->vdev->name, 32, "%s", self->name);
        self->vdev->v4l2_dev = &self->v4l2_dev;
        self->vdev->vfl_type = VFL_TYPE_VIDEO;
        self->vdev->vfl_dir =
                self->type == AKVCAM_DEVICE_TYPE_OUTPUT? VFL_DIR_TX: VFL_DIR_RX;
        self->vdev->minor = -1;
        self->vdev->fops = akvcam_node_fops();
        self->vdev->tvnorms = V4L2_STD_ALL;
        self->vdev->release = video_device_release_empty;
        akvcam_attributes_set(self->attributes, &self->vdev->dev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
        self->vdev->device_caps = akvcam_device_caps(self);
#endif

        video_set_drvdata(self->vdev, self);

        result = video_register_device(self->vdev,
                                       VFL_TYPE_VIDEO,
                                       self->videonr);

        if (result) {
            v4l2_device_unregister(&self->v4l2_dev);
            video_device_release(self->vdev);
            self->vdev = NULL;
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

int32_t akvcam_device_num(const akvcam_device_t self)
{
    return self->vdev?
                self->vdev->num:
                self->videonr;
}

void akvcam_device_set_num(const akvcam_device_t self, int32_t num)
{
    self->videonr = num;
}

int64_t akvcam_device_broadcasting_node(const akvcam_device_t self)
{
    return self->broadcasting_node;
}

void akvcam_device_set_broadcasting_node(const akvcam_device_t self,
                                         int64_t broadcasting_node)
{
    self->broadcasting_node = broadcasting_node;
}

bool akvcam_device_is_registered(const akvcam_device_t self)
{
    return self->vdev;
}

const char *akvcam_device_description(const akvcam_device_t self)
{
    return self->description;
}

AKVCAM_DEVICE_TYPE akvcam_device_type(const akvcam_device_t self)
{
    return self->type;
}

enum v4l2_buf_type akvcam_device_v4l2_type(const akvcam_device_t self)
{
    return self->buffer_type;
}

AKVCAM_RW_MODE akvcam_device_rw_mode(const akvcam_device_t self)
{
    return self->rw_mode;
}

akvcam_formats_list_t akvcam_device_formats(const akvcam_device_t self)
{
    return akvcam_list_new_copy(self->formats);
}

akvcam_format_t akvcam_device_format(const akvcam_device_t self)
{
    return akvcam_format_new_copy(self->format);
}

void akvcam_device_set_format(const akvcam_device_t self,
                              akvcam_format_t format)
{
    akvcam_format_copy(self->format, format);
    akvcam_buffers_set_format(self->buffers, format);
}

akvcam_controls_t akvcam_device_controls_nr(const akvcam_device_t self)
{
    return self->controls;
}

akvcam_controls_t akvcam_device_controls(const akvcam_device_t self)
{
    return akvcam_controls_ref(self->controls);
}

akvcam_nodes_list_t akvcam_device_nodes_nr(const akvcam_device_t self)
{
    return self->nodes;
}

akvcam_nodes_list_t akvcam_device_nodes(const akvcam_device_t self)
{
    return akvcam_list_ref(self->nodes);
}

akvcam_buffers_t akvcam_device_buffers_nr(const akvcam_device_t self)
{
    return self->buffers;
}

akvcam_buffers_t akvcam_device_buffers(const akvcam_device_t self)
{
    return akvcam_buffers_ref(self->buffers);
}

akvcam_node_t akvcam_device_priority_node(const akvcam_device_t self)
{
    return self->priority_node;
}

void akvcam_device_set_priority(akvcam_device_t self,
                                enum v4l2_priority priority,
                                akvcam_node_t node)
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

bool akvcam_device_streaming_rw(const akvcam_device_t self)
{
    return self->streaming_rw;
}

bool akvcam_device_start_streaming(akvcam_device_t self)
{
    uint64_t broadcasting_node;

    akpr_function();

    if (!self->streaming) {
        akvcam_buffers_reset_sequence(self->buffers);

        if (self->type == AKVCAM_DEVICE_TYPE_OUTPUT) {
            broadcasting_node = self->broadcasting_node;
            akvcam_device_set_streaming_rw(self, false);
            self->broadcasting_node = broadcasting_node;
        }

        self->thread = kthread_run((akvcam_thread_t)
                                   akvcam_device_clock_timeout,
                                   self,
                                   "akvcam-thread-%llu",
                                   akvcam_id());

        if (!self->thread)
            return false;

        self->streaming = true;
    }

    return true;
}

void akvcam_device_stop_streaming(akvcam_device_t self)
{
    akpr_function();

    if (self->streaming) {
        if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE) {
            kthread_stop(self->thread);
            self->thread = NULL;
        }

        self->streaming = false;
    }

    self->broadcasting_node = -1;
}

void akvcam_device_set_streaming_rw(akvcam_device_t self, bool streaming)
{
    akpr_function();
    akpr_debug("Streaming: %d\n", streaming);

    if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE) {
        if (self->streaming_rw != streaming) {
            if (!mutex_lock_interruptible(&self->mtx)) {
                akvcam_frame_delete(self->current_frame);
                self->current_frame = NULL;
                mutex_unlock(&self->mtx);
            }
        }
    }

    self->streaming_rw = streaming;

    if (!streaming)
        self->broadcasting_node = -1;
}

static bool akvcam_device_are_equals(const akvcam_device_t device,
                                     const struct file *filp)
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
    int32_t device_num;

    if (filp->private_data) {
        device_num = akvcam_node_device_num(filp->private_data);

        return akvcam_driver_device_from_num_nr(device_num);
    }

    it = akvcam_list_find(akvcam_driver_devices_nr(),
                          filp,
                          (akvcam_are_equals_t) akvcam_device_are_equals);

    return akvcam_list_element_data(it);
}

akvcam_device_t akvcam_device_from_file(struct file *filp)
{
    return akvcam_device_ref(akvcam_device_from_file_nr(filp));
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

void akvcam_device_controls_changed(akvcam_device_t self,
                                    struct v4l2_event *event)
{
    akvcam_list_element_t it = NULL;
    akvcam_device_t capture_device;

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

    akvcam_device_event_received(self, event);

    if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE)
        return;

    for (;;) {
        capture_device = akvcam_list_next(self->connected_devices, &it);

        if (!it)
            break;

        capture_device->horizontal_flip = self->horizontal_flip;
        capture_device->vertical_flip = self->vertical_flip;
        capture_device->scaling = self->scaling;
        capture_device->aspect_ratio = self->aspect_ratio;
    }
}

akvcam_devices_list_t akvcam_device_connected_devices_nr(const akvcam_device_t self)
{
    return self->connected_devices;
}

akvcam_devices_list_t akvcam_device_connected_devices(const akvcam_device_t self)
{
    return akvcam_list_ref(self->connected_devices);
}

__u32 akvcam_device_caps(const akvcam_device_t self)
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
        || self->rw_mode & AKVCAM_RW_MODE_USERPTR)
        caps |= V4L2_CAP_STREAMING;

    caps |= V4L2_CAP_EXT_PIX_FORMAT;

    return caps;
}

int akvcam_device_clock_timeout(akvcam_device_t self)
{
    akvcam_list_element_t it = NULL;
    akvcam_device_t capture_device;
    akvcam_device_t output_device;
    akvcam_frame_t frame;
    akvcam_frame_t adjusted_frame;
    akvcam_frame_t default_frame = akvcam_default_frame();
    struct v4l2_fract *frame_rate = akvcam_format_frame_rate(self->format);
    __u32 tsleep = 1000 * frame_rate->denominator;

    if (frame_rate->numerator)
        tsleep /= frame_rate->numerator;

    while (!kthread_should_stop()) {
        if (self->type == AKVCAM_DEVICE_TYPE_CAPTURE) {
            frame = NULL;
            output_device = akvcam_list_front(self->connected_devices);

            if (!mutex_lock_interruptible(&self->mtx)) {
                if (output_device
                    && (output_device->streaming || output_device->streaming_rw)
                    && self->current_frame) {
                    akpr_debug("Reading current frame.\n");
                    frame = akvcam_frame_new_copy(self->current_frame);
                }

                mutex_unlock(&self->mtx);
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

            if (!akvcam_buffers_write_frame(self->buffers, adjusted_frame))
                akpr_err("Failed writing frame.\n");

            akvcam_frame_delete(adjusted_frame);
            akvcam_device_notify_frame(self);
        } else {
            it = NULL;

            for (;;) {
                capture_device = akvcam_list_next(self->connected_devices, &it);

                if (!it)
                    break;

                if (!mutex_lock_interruptible(&capture_device->mtx)) {
                    akvcam_frame_delete(capture_device->current_frame);
                    capture_device->current_frame =
                            akvcam_buffers_read_frame(self->buffers);
                    mutex_unlock(&capture_device->mtx);
                }
            }
        }

        msleep_interruptible(tsleep);
    }

    return 0;
}

akvcam_frame_t akvcam_device_frame_apply_adjusts(const akvcam_device_t self,
                                                 akvcam_frame_t frame)
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

void akvcam_device_notify_frame(akvcam_device_t self)
{
    struct v4l2_event event;

    akpr_function();
    memset(&event, 0, sizeof(struct v4l2_event));
    event.type = V4L2_EVENT_FRAME_SYNC;
    event.u.frame_sync.frame_sequence =
            akvcam_buffers_sequence(self->buffers) - 1;
    akvcam_device_event_received(self, &event);
}

akvcam_frame_t akvcam_default_frame(void)
{
    static akvcam_frame_t frame = NULL;
    akvcam_settings_t settings;
    char *file_name;
    bool loaded = false;

    if (frame)
        return frame;

    settings = akvcam_settings_new();

    if (akvcam_settings_load(settings, akvcam_settings_file())) {
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
