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

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/videodev2.h>

#include "ioctl.h"
#include "buffers.h"
#include "controls.h"
#include "device.h"
#include "driver.h"
#include "events.h"
#include "format.h"
#include "list.h"
#include "node.h"
#include "object.h"
#include "utils.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#define DEFAULT_COLORSPACE V4L2_COLORSPACE_SRGB
#else
#define DEFAULT_COLORSPACE V4L2_COLORSPACE_RAW
#endif

#define AKVCAM_HANDLER(cmd, proc, arg_type) \
    {cmd, (akvcam_proc_t) proc, sizeof(arg_type)}

#define AKVCAM_HANDLER_IGNORE(cmd) \
    {cmd, NULL, 0}

#define AKVCAM_HANDLER_END \
    {0, NULL, 0}

typedef int (*akvcam_proc_t)(struct akvcam_node *node, void *arg);

typedef struct
{
    uint cmd;
    akvcam_proc_t proc;
    size_t data_size;
} akvcam_ioctl_handler, *akvcam_ioctl_handler_t;

struct akvcam_ioctl
{
    akvcam_object_t self;
    size_t n_ioctls;
};

int akvcam_ioctls_querycap(akvcam_node_t node, struct v4l2_capability *arg);
int akvcam_ioctls_query_ext_ctrl(akvcam_node_t node,
                                 struct v4l2_query_ext_ctrl *control);
int akvcam_ioctls_g_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls);
int akvcam_ioctls_s_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls);
int akvcam_ioctls_try_ext_ctrls(akvcam_node_t node,
                                struct v4l2_ext_controls *controls);
int akvcam_ioctls_queryctrl(akvcam_node_t node, struct v4l2_queryctrl *control);
int akvcam_ioctls_g_ctrl(akvcam_node_t node, struct v4l2_control *control);
int akvcam_ioctls_s_ctrl(akvcam_node_t node, struct v4l2_control *control);
int akvcam_ioctls_enuminput(akvcam_node_t node, struct v4l2_input *input);
int akvcam_ioctls_g_input(akvcam_node_t node, int *input);
int akvcam_ioctls_s_input(akvcam_node_t node, int *input);
int akvcam_ioctls_enum_fmt(akvcam_node_t node, struct v4l2_fmtdesc *format);
int akvcam_ioctls_g_fmt(akvcam_node_t node, struct v4l2_format *format);
int akvcam_ioctls_s_fmt(akvcam_node_t node, struct v4l2_format *format);
int akvcam_ioctls_try_fmt(akvcam_node_t node, struct v4l2_format *format);
int akvcam_ioctls_g_parm(akvcam_node_t node, struct v4l2_streamparm *param);
int akvcam_ioctls_s_parm(akvcam_node_t node, struct v4l2_streamparm *param);
int akvcam_ioctls_enum_framesizes(akvcam_node_t node,
                                  struct v4l2_frmsizeenum *frame_sizes);
int akvcam_ioctls_enum_frameintervals(akvcam_node_t node,
                                      struct v4l2_frmivalenum *frame_intervals);
int akvcam_ioctls_g_priority(akvcam_node_t node, enum v4l2_priority *priority);
int akvcam_ioctls_s_priority(akvcam_node_t node, enum v4l2_priority *priority);
int akvcam_ioctls_subscribe_event(akvcam_node_t node,
                                  struct v4l2_event_subscription *event);
int akvcam_ioctls_unsubscribe_event(akvcam_node_t node,
                                    struct v4l2_event_subscription *event);
int akvcam_ioctls_dqevent(akvcam_node_t node, struct v4l2_event *event);
int akvcam_ioctls_reqbufs(akvcam_node_t node, struct v4l2_requestbuffers *request);
int akvcam_ioctls_querybuf(akvcam_node_t node, struct v4l2_buffer *buffer);

static akvcam_ioctl_handler akvcam_ioctls_private[] = {
    AKVCAM_HANDLER(VIDIOC_QUERYCAP           , akvcam_ioctls_querycap           , struct v4l2_capability        ),
    AKVCAM_HANDLER(VIDIOC_QUERY_EXT_CTRL     , akvcam_ioctls_query_ext_ctrl     , struct v4l2_query_ext_ctrl    ),
    AKVCAM_HANDLER(VIDIOC_G_EXT_CTRLS        , akvcam_ioctls_g_ext_ctrls        , struct v4l2_ext_controls      ),
    AKVCAM_HANDLER(VIDIOC_S_EXT_CTRLS        , akvcam_ioctls_s_ext_ctrls        , struct v4l2_ext_controls      ),
    AKVCAM_HANDLER(VIDIOC_TRY_EXT_CTRLS      , akvcam_ioctls_try_ext_ctrls      , struct v4l2_ext_controls      ),
    AKVCAM_HANDLER(VIDIOC_QUERYCTRL          , akvcam_ioctls_queryctrl          , struct v4l2_queryctrl         ),
    AKVCAM_HANDLER(VIDIOC_G_CTRL             , akvcam_ioctls_g_ctrl             , struct v4l2_control           ),
    AKVCAM_HANDLER(VIDIOC_S_CTRL             , akvcam_ioctls_s_ctrl             , struct v4l2_control           ),
    AKVCAM_HANDLER(VIDIOC_ENUMINPUT          , akvcam_ioctls_enuminput          , struct v4l2_input             ),
    AKVCAM_HANDLER(VIDIOC_G_INPUT            , akvcam_ioctls_g_input            , int                           ),
    AKVCAM_HANDLER(VIDIOC_S_INPUT            , akvcam_ioctls_s_input            , int                           ),
    AKVCAM_HANDLER(VIDIOC_ENUM_FMT           , akvcam_ioctls_enum_fmt           , struct v4l2_fmtdesc           ),
    AKVCAM_HANDLER(VIDIOC_G_FMT              , akvcam_ioctls_g_fmt              , struct v4l2_format            ),
    AKVCAM_HANDLER(VIDIOC_S_FMT              , akvcam_ioctls_s_fmt              , struct v4l2_format            ),
    AKVCAM_HANDLER(VIDIOC_TRY_FMT            , akvcam_ioctls_try_fmt            , struct v4l2_format            ),
    AKVCAM_HANDLER(VIDIOC_G_PARM             , akvcam_ioctls_g_parm             , struct v4l2_streamparm        ),
    AKVCAM_HANDLER(VIDIOC_S_PARM             , akvcam_ioctls_s_parm             , struct v4l2_streamparm        ),
    AKVCAM_HANDLER(VIDIOC_ENUM_FRAMESIZES    , akvcam_ioctls_enum_framesizes    , struct v4l2_frmsizeenum       ),
    AKVCAM_HANDLER(VIDIOC_ENUM_FRAMEINTERVALS, akvcam_ioctls_enum_frameintervals, struct v4l2_frmivalenum       ),
    AKVCAM_HANDLER(VIDIOC_G_PRIORITY         , akvcam_ioctls_g_priority         , enum v4l2_priority            ),
    AKVCAM_HANDLER(VIDIOC_S_PRIORITY         , akvcam_ioctls_s_priority         , enum v4l2_priority            ),
    AKVCAM_HANDLER(VIDIOC_SUBSCRIBE_EVENT    , akvcam_ioctls_subscribe_event    , struct v4l2_event_subscription),
    AKVCAM_HANDLER(VIDIOC_UNSUBSCRIBE_EVENT  , akvcam_ioctls_unsubscribe_event  , struct v4l2_event_subscription),
    AKVCAM_HANDLER(VIDIOC_DQEVENT            , akvcam_ioctls_dqevent            , struct v4l2_event             ),
    AKVCAM_HANDLER(VIDIOC_REQBUFS            , akvcam_ioctls_reqbufs            , struct v4l2_requestbuffers    ),
    AKVCAM_HANDLER(VIDIOC_QUERYBUF           , akvcam_ioctls_querybuf           , struct v4l2_buffer            ),

    AKVCAM_HANDLER_IGNORE(VIDIOC_CROPCAP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_DBG_G_REGISTER),
    AKVCAM_HANDLER_IGNORE(VIDIOC_DECODER_CMD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_DV_TIMINGS_CAP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_ENCODER_CMD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_ENUMAUDIO),
    AKVCAM_HANDLER_IGNORE(VIDIOC_ENUMAUDOUT),
    AKVCAM_HANDLER_IGNORE(VIDIOC_ENUMOUTPUT),
    AKVCAM_HANDLER_IGNORE(VIDIOC_ENUMSTD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_ENUM_DV_TIMINGS),
    AKVCAM_HANDLER_IGNORE(VIDIOC_EXPBUF),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_AUDIO),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_CROP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_DV_TIMINGS),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_EDID),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_ENC_INDEX),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_FBUF),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_FREQUENCY),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_JPEGCOMP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_MODULATOR),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_OUTPUT),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_SELECTION),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_SELECTION),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_SLICED_VBI_CAP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_STD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_TUNER),
    AKVCAM_HANDLER_IGNORE(VIDIOC_LOG_STATUS),
    AKVCAM_HANDLER_IGNORE(VIDIOC_QUERYMENU),
    AKVCAM_HANDLER_IGNORE(VIDIOC_QUERYSTD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_QUERY_DV_TIMINGS),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_AUDIO),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_EDID),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_FREQUENCY),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_HW_FREQ_SEEK),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_JPEGCOMP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_OUTPUT),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_SELECTION),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_STD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_TUNER),

    AKVCAM_HANDLER_END
};

akvcam_ioctl_t akvcam_ioctl_new(void)
{
    size_t i;
    akvcam_ioctl_t self = kzalloc(sizeof(struct akvcam_ioctl), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_ioctl_delete);

    // Check the number of ioctls available.
    self->n_ioctls = 0;

    for (i = 0; akvcam_ioctls_private[i].cmd; i++)
        self->n_ioctls++;

    return self;
}

void akvcam_ioctl_delete(akvcam_ioctl_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

int akvcam_ioctl_do(akvcam_ioctl_t self,
                    struct akvcam_node *node,
                    unsigned int cmd,
                    void *arg)
{
    size_t i;
    size_t size;
    char *data;
    int result;

    for (i = 0; i < self->n_ioctls; i++)
        if (akvcam_ioctls_private[i].cmd == cmd) {
            if (akvcam_ioctls_private[i].proc) {
                size = akvcam_ioctls_private[i].data_size;
                data = kzalloc(size, GFP_KERNEL);
                copy_from_user(data, arg, size);
                result = akvcam_ioctls_private[i].proc(node, data);
                copy_to_user(arg, data, size);
                kfree(data);

                return result;
            }

            return -ENOTTY;
        }

    printk(KERN_INFO "Unhandled ioctl: %s\n", akvcam_string_from_ioctl(cmd));

    return -ENOTTY;
}


int akvcam_ioctls_querycap(akvcam_node_t node,
                           struct v4l2_capability *capability)
{
    __u32 capabilities = 0;
    akvcam_device_t device;
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    device = akvcam_node_device_nr(node);

    snprintf((char *) capability->driver, 16, "%s", akvcam_driver_name());
    snprintf((char *) capability->card, 32, "%s", akvcam_driver_description());
    snprintf((char *) capability->bus_info,
             32, "platform:akvcam-%d", akvcam_device_num(device));
    capability->version = akvcam_driver_version();

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        capabilities = V4L2_CAP_VIDEO_OUTPUT;
    else
        capabilities = V4L2_CAP_VIDEO_CAPTURE;

    capabilities |= V4L2_CAP_STREAMING
                 |  V4L2_CAP_EXT_PIX_FORMAT;

//    capabilities |= V4L2_CAP_READWRITE;

    capability->capabilities = capabilities | V4L2_CAP_DEVICE_CAPS;
    capability->device_caps = capabilities;
    memset(capability->reserved, 0, 3 * sizeof(__u32));

    return 0;
}

int akvcam_ioctls_query_ext_ctrl(akvcam_node_t node,
                                 struct v4l2_query_ext_ctrl *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    controls = akvcam_device_controls_nr(device);

    return akvcam_controls_fill_ext(controls, control);
}

int akvcam_ioctls_g_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_get_ext(controls_, controls, 0);
}

int akvcam_ioctls_s_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_set_ext(controls_, controls, 0);
}

int akvcam_ioctls_try_ext_ctrls(akvcam_node_t node,
                                struct v4l2_ext_controls *controls)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_try_ext(controls_, controls, 0);
}

int akvcam_ioctls_queryctrl(akvcam_node_t node, struct v4l2_queryctrl *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    controls = akvcam_device_controls_nr(device);

    return akvcam_controls_fill(controls, control);
}

int akvcam_ioctls_g_ctrl(akvcam_node_t node, struct v4l2_control *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_get(controls_, control);
}

int akvcam_ioctls_s_ctrl(akvcam_node_t node, struct v4l2_control *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_set(controls_, control);
}

int akvcam_ioctls_enuminput(akvcam_node_t node, struct v4l2_input *input)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    if (input->index > 0)
        return -EINVAL;

    memset(input, 0, sizeof(struct v4l2_input));
    snprintf((char *) input->name, 32, "akvcam-input");
    input->type = V4L2_INPUT_TYPE_CAMERA;

    return 0;
}

int akvcam_ioctls_g_input(akvcam_node_t node, int *input)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    *input = 0;

    return 0;
}

int akvcam_ioctls_s_input(akvcam_node_t node, int *input)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    return *input == 0? 0: -EINVAL;
}

int akvcam_ioctls_enum_fmt(akvcam_node_t node, struct v4l2_fmtdesc *format)
{
    akvcam_device_t device;
    akvcam_list_tt(akvcam_format_t) formats;
    akvcam_list_tt(__u32) pixel_formats;
    __u32 *fourcc;
    enum v4l2_buf_type type;
    const char *description;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (format->type != type)
        return -EINVAL;

    formats = akvcam_device_formats_nr(device);
    pixel_formats = akvcam_format_pixel_formats(formats);
    fourcc = akvcam_list_at(pixel_formats, format->index);

    if (fourcc) {
        format->flags = 0;
        format->pixelformat = *fourcc;
        description = akvcam_format_string_from_fourcc(format->pixelformat);
        snprintf((char *) format->description, 32, "%s", description);
        memset(format->reserved, 0, 4 * sizeof(__u32));
    }

    akvcam_list_delete(&pixel_formats);

    return fourcc? 0: -EINVAL;
}

int akvcam_ioctls_g_fmt(akvcam_node_t node, struct v4l2_format *format)
{
    akvcam_device_t device;
    akvcam_format_t current_format;
    enum v4l2_buf_type type;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (format->type != type)
        return -EINVAL;

    current_format = akvcam_node_format_nr(node);
    memset(&format->fmt.pix, 0, sizeof(struct v4l2_pix_format));
    format->fmt.pix.width = akvcam_format_width(current_format);
    format->fmt.pix.height = akvcam_format_height(current_format);
    format->fmt.pix.pixelformat = akvcam_format_fourcc(current_format);
    format->fmt.pix.field = V4L2_FIELD_NONE;
    format->fmt.pix.bytesperline = (__u32) akvcam_format_bypl(current_format);
    format->fmt.pix.sizeimage = (__u32) akvcam_format_size(current_format);
    format->fmt.pix.colorspace = DEFAULT_COLORSPACE;

    return 0;
}

int akvcam_ioctls_s_fmt(akvcam_node_t node, struct v4l2_format *format)
{
    akvcam_format_t current_format;
    int result;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    result = akvcam_ioctls_try_fmt(node, format);

    if (!result) {
        current_format = akvcam_node_format_nr(node);
        akvcam_format_set_fourcc(current_format, format->fmt.pix.pixelformat);
        akvcam_format_set_width(current_format, format->fmt.pix.width);
        akvcam_format_set_height(current_format, format->fmt.pix.height);
    }

    return result;
}

int akvcam_ioctls_try_fmt(akvcam_node_t node, struct v4l2_format *format)
{
    akvcam_device_t device;
    akvcam_format_t nearest_format;
    akvcam_format_t temp_format;
    struct v4l2_fract frame_rate = {0, 0};
    enum v4l2_buf_type type;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (format->type != type)
        return -EINVAL;

    temp_format = akvcam_format_new(format->fmt.pix.pixelformat,
                                    format->fmt.pix.width,
                                    format->fmt.pix.height,
                                    &frame_rate);
    nearest_format = akvcam_format_nearest_nr(akvcam_device_formats_nr(device),
                                              temp_format);

    akvcam_format_delete(&temp_format);

    memset(&format->fmt.pix, 0, sizeof(struct v4l2_pix_format));
    format->fmt.pix.width = akvcam_format_width(nearest_format);
    format->fmt.pix.height = akvcam_format_height(nearest_format);
    format->fmt.pix.pixelformat = akvcam_format_fourcc(nearest_format);
    format->fmt.pix.field = V4L2_FIELD_NONE;
    format->fmt.pix.bytesperline = (__u32) akvcam_format_bypl(nearest_format);
    format->fmt.pix.sizeimage = (__u32) akvcam_format_size(nearest_format);
    format->fmt.pix.colorspace = DEFAULT_COLORSPACE;

    return 0;
}

int akvcam_ioctls_g_parm(akvcam_node_t node, struct v4l2_streamparm *param)
{
    akvcam_device_t device;
    akvcam_format_t format;
    enum v4l2_buf_type type;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (param->type != type)
        return -EINVAL;

    memset(&param->parm, 0, 200);
    format = akvcam_node_format_nr(node);

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        param->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.output.timeperframe.numerator =
                akvcam_format_frame_rate(format)->denominator;
        param->parm.output.timeperframe.denominator =
                akvcam_format_frame_rate(format)->numerator;
    } else {
        param->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.capture.timeperframe.numerator =
                akvcam_format_frame_rate(format)->denominator;
        param->parm.capture.timeperframe.denominator =
                akvcam_format_frame_rate(format)->numerator;
    }

    return 0;
}

int akvcam_ioctls_s_parm(akvcam_node_t node, struct v4l2_streamparm *param)
{
    akvcam_device_t device;
    akvcam_list_tt(akvcam_format_t) formats;
    akvcam_format_t format;
    akvcam_format_t nearest_format;
    enum v4l2_buf_type type;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (param->type != type)
        return -EINVAL;

    format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(format, akvcam_node_format_nr(node));

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        akvcam_format_frame_rate(format)->numerator =
                param->parm.output.timeperframe.denominator;
        akvcam_format_frame_rate(format)->denominator =
                param->parm.output.timeperframe.numerator;
    } else {
        akvcam_format_frame_rate(format)->numerator =
                param->parm.capture.timeperframe.denominator;
        akvcam_format_frame_rate(format)->denominator =
                param->parm.capture.timeperframe.numerator;
    }

    formats = akvcam_device_formats_nr(device);
    nearest_format = akvcam_format_nearest_nr(formats, format);

    if (!nearest_format) {
        akvcam_format_delete(&format);

        return -EINVAL;
    }

    akvcam_format_delete(&format);
    akvcam_format_copy(akvcam_node_format_nr(node), nearest_format);
    memset(&param->parm, 0, 200);

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        param->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.output.timeperframe.numerator =
                akvcam_format_frame_rate(nearest_format)->denominator;
        param->parm.output.timeperframe.denominator =
                akvcam_format_frame_rate(nearest_format)->numerator;
    } else {
        param->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.capture.timeperframe.numerator =
                akvcam_format_frame_rate(nearest_format)->denominator;
        param->parm.capture.timeperframe.denominator =
                akvcam_format_frame_rate(nearest_format)->numerator;
    }

    return 0;
}

int akvcam_ioctls_enum_framesizes(akvcam_node_t node,
                                  struct v4l2_frmsizeenum *frame_sizes)
{
    akvcam_device_t device;
    akvcam_list_tt(akvcam_format_t) formats;
    akvcam_list_tt(struct v4l2_frmsize_discrete) resolutions;
    struct v4l2_frmsize_discrete *resolution;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    formats = akvcam_device_formats_nr(device);
    resolutions = akvcam_format_resolutions(formats,
                                            frame_sizes->pixel_format);
    resolution = akvcam_list_at(resolutions, frame_sizes->index);

    if (resolution) {
        frame_sizes->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        frame_sizes->discrete.width = resolution->width;
        frame_sizes->discrete.height = resolution->height;
        memset(frame_sizes->reserved, 0, 2 * sizeof(__u32));
    }

    akvcam_list_delete(&resolutions);

    return resolution? 0: -EINVAL;
}

int akvcam_ioctls_enum_frameintervals(akvcam_node_t node,
                                      struct v4l2_frmivalenum *frame_intervals)
{
    akvcam_device_t device;
    akvcam_list_tt(akvcam_format_t) formats;
    akvcam_list_tt(struct v4l2_fract) frame_rates;
    struct v4l2_fract *frame_rate;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_node_device_nr(node);
    formats = akvcam_device_formats_nr(device);
    frame_rates = akvcam_format_frame_rates(formats,
                                            frame_intervals->pixel_format,
                                            frame_intervals->width,
                                            frame_intervals->height);
    frame_rate = akvcam_list_at(frame_rates, frame_intervals->index);

    if (frame_rate) {
        frame_intervals->type = V4L2_FRMIVAL_TYPE_DISCRETE;
        frame_intervals->discrete.numerator = frame_rate->denominator;
        frame_intervals->discrete.denominator = frame_rate->numerator;
        memset(frame_intervals->reserved, 0, 2 * sizeof(__u32));
    }

    akvcam_list_delete(&frame_rates);

    return frame_rate? 0: -EINVAL;
}

int akvcam_ioctls_g_priority(akvcam_node_t node, enum v4l2_priority *priority)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    *priority = akvcam_device_priority(akvcam_node_device_nr(node));

    return 0;
}

int akvcam_ioctls_s_priority(akvcam_node_t node, enum v4l2_priority *priority)
{
    akvcam_node_t priority_node;
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    priority_node = akvcam_device_priority_node(akvcam_node_device_nr(node));

    if (priority_node && priority_node != node)
        return -EINVAL;

    if (*priority == V4L2_PRIORITY_DEFAULT)
        akvcam_device_set_priority(akvcam_node_device_nr(node),
                                   *priority,
                                   NULL);
    else
        akvcam_device_set_priority(akvcam_node_device_nr(node),
                                   *priority,
                                   node);

    return 0;
}

int akvcam_ioctls_subscribe_event(akvcam_node_t node,
                                  struct v4l2_event_subscription *event)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    akvcam_events_t events;
    struct v4l2_event control_event;
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    if (event->type != V4L2_EVENT_CTRL)
        return -EINVAL;

    device = akvcam_node_device_nr(node);
    controls = akvcam_device_controls_nr(device);

    if (!akvcam_controls_contains(controls, event->id))
        return -EINVAL;

    events = akvcam_node_events_nr(node);
    akvcam_events_subscribe(events, event);

    if (event->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL)
        if (akvcam_controls_generate_event(controls, event->id, &control_event))
            akvcam_events_enqueue(events, &control_event);

    return 0;
}

int akvcam_ioctls_unsubscribe_event(akvcam_node_t node,
                                    struct v4l2_event_subscription *event)
{
    akvcam_events_t events;
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    events = akvcam_node_events_nr(node);

    if (event->type == V4L2_EVENT_ALL) {
        akvcam_events_unsubscribe_all(events);

        return 0;
    }

    akvcam_events_unsubscribe(events, event);

    return 0;
}

int akvcam_ioctls_dqevent(akvcam_node_t node, struct v4l2_event *event)
{
    akvcam_events_t events;
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    events = akvcam_node_events_nr(node);

    return akvcam_events_dequeue(events, event)? 0: -EINVAL;
}

int akvcam_ioctls_reqbufs(akvcam_node_t node, struct v4l2_requestbuffers *request)
{
    akvcam_buffers_t buffers;
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    buffers = akvcam_node_buffers_nr(node);

    return akvcam_buffers_allocate(buffers, request)? 0: -EINVAL;
}

int akvcam_ioctls_querybuf(akvcam_node_t node, struct v4l2_buffer *buffer)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    return ENOTTY;
}
