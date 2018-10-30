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
#include <linux/uvcvideo.h>

#include "ioctl.h"
#include "buffers.h"
#include "controls.h"
#include "device.h"
#include "driver.h"
#include "events.h"
#include "format.h"
#include "list.h"
#include "log.h"
#include "node.h"
#include "object.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#define DEFAULT_COLORSPACE V4L2_COLORSPACE_SRGB
#else
#define DEFAULT_COLORSPACE V4L2_COLORSPACE_RAW
#endif

#ifndef V4L2_CAP_EXT_PIX_FORMAT
#define V4L2_CAP_EXT_PIX_FORMAT 0x00200000
#endif

#define AKVCAM_HANDLER(cmd, proc, arg_type) \
    {cmd, (akvcam_proc_t) proc, sizeof(arg_type)}

#define AKVCAM_HANDLER_IGNORE(cmd) \
    {cmd, NULL, 0}

#define AKVCAM_HANDLER_END \
    {0, NULL, 0}

typedef int (*akvcam_proc_t)(akvcam_node_t node, void *arg);

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
#ifdef VIDIOC_QUERY_EXT_CTRL
int akvcam_ioctls_query_ext_ctrl(akvcam_node_t node,
                                 struct v4l2_query_ext_ctrl *control);
#endif
int akvcam_ioctls_g_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls);
int akvcam_ioctls_s_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls);
int akvcam_ioctls_try_ext_ctrls(akvcam_node_t node,
                                struct v4l2_ext_controls *controls);
int akvcam_ioctls_queryctrl(akvcam_node_t node, struct v4l2_queryctrl *control);
int akvcam_ioctls_querymenu(akvcam_node_t node, struct v4l2_querymenu *menu);
int akvcam_ioctls_g_ctrl(akvcam_node_t node, struct v4l2_control *control);
int akvcam_ioctls_s_ctrl(akvcam_node_t node, struct v4l2_control *control);
int akvcam_ioctls_enuminput(akvcam_node_t node, struct v4l2_input *input);
int akvcam_ioctls_g_input(akvcam_node_t node, int *input);
int akvcam_ioctls_s_input(akvcam_node_t node, int *input);
int akvcam_ioctls_enumoutput(akvcam_node_t node, struct v4l2_output *output);
int akvcam_ioctls_g_output(akvcam_node_t node, int *output);
int akvcam_ioctls_s_output(akvcam_node_t node, int *output);
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
int akvcam_ioctl_create_bufs(akvcam_node_t node, struct v4l2_create_buffers *buffers);
int akvcam_ioctl_qbuf(akvcam_node_t node, struct v4l2_buffer *buffer);
int akvcam_ioctl_dqbuf(akvcam_node_t node, struct v4l2_buffer *buffer);
int akvcam_ioctl_streamon(akvcam_node_t node, const int *type);
int akvcam_ioctl_streamoff(akvcam_node_t node, const int *type);

static akvcam_ioctl_handler akvcam_ioctls_private[] = {
    AKVCAM_HANDLER(VIDIOC_QUERYCAP           , akvcam_ioctls_querycap           , struct v4l2_capability        ),
#ifdef VIDIOC_QUERY_EXT_CTRL
    AKVCAM_HANDLER(VIDIOC_QUERY_EXT_CTRL     , akvcam_ioctls_query_ext_ctrl     , struct v4l2_query_ext_ctrl    ),
#endif
    AKVCAM_HANDLER(VIDIOC_G_EXT_CTRLS        , akvcam_ioctls_g_ext_ctrls        , struct v4l2_ext_controls      ),
    AKVCAM_HANDLER(VIDIOC_S_EXT_CTRLS        , akvcam_ioctls_s_ext_ctrls        , struct v4l2_ext_controls      ),
    AKVCAM_HANDLER(VIDIOC_TRY_EXT_CTRLS      , akvcam_ioctls_try_ext_ctrls      , struct v4l2_ext_controls      ),
    AKVCAM_HANDLER(VIDIOC_QUERYCTRL          , akvcam_ioctls_queryctrl          , struct v4l2_queryctrl         ),
    AKVCAM_HANDLER(VIDIOC_QUERYMENU          , akvcam_ioctls_querymenu          , struct v4l2_querymenu         ),
    AKVCAM_HANDLER(VIDIOC_G_CTRL             , akvcam_ioctls_g_ctrl             , struct v4l2_control           ),
    AKVCAM_HANDLER(VIDIOC_S_CTRL             , akvcam_ioctls_s_ctrl             , struct v4l2_control           ),
    AKVCAM_HANDLER(VIDIOC_ENUMINPUT          , akvcam_ioctls_enuminput          , struct v4l2_input             ),
    AKVCAM_HANDLER(VIDIOC_G_INPUT            , akvcam_ioctls_g_input            , int                           ),
    AKVCAM_HANDLER(VIDIOC_S_INPUT            , akvcam_ioctls_s_input            , int                           ),
    AKVCAM_HANDLER(VIDIOC_ENUMOUTPUT         , akvcam_ioctls_enumoutput         , struct v4l2_output            ),
    AKVCAM_HANDLER(VIDIOC_G_OUTPUT           , akvcam_ioctls_g_output           , int                           ),
    AKVCAM_HANDLER(VIDIOC_S_OUTPUT           , akvcam_ioctls_s_output           , int                           ),
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
    AKVCAM_HANDLER(VIDIOC_CREATE_BUFS        , akvcam_ioctl_create_bufs         , struct v4l2_create_buffers    ),
    AKVCAM_HANDLER(VIDIOC_QBUF               , akvcam_ioctl_qbuf                , struct v4l2_buffer            ),
    AKVCAM_HANDLER(VIDIOC_DQBUF              , akvcam_ioctl_dqbuf               , struct v4l2_buffer            ),
    AKVCAM_HANDLER(VIDIOC_STREAMON           , akvcam_ioctl_streamon            , const int                     ),
    AKVCAM_HANDLER(VIDIOC_STREAMOFF          , akvcam_ioctl_streamoff           , const int                     ),

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
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_AUDOUT),
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
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_SLICED_VBI_CAP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_STD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_G_TUNER),
    AKVCAM_HANDLER_IGNORE(VIDIOC_LOG_STATUS),
    AKVCAM_HANDLER_IGNORE(VIDIOC_QUERYSTD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_QUERY_DV_TIMINGS),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_AUDIO),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_AUDOUT),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_EDID),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_FREQUENCY),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_HW_FREQ_SEEK),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_JPEGCOMP),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_OUTPUT),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_SELECTION),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_STD),
    AKVCAM_HANDLER_IGNORE(VIDIOC_S_TUNER),
    AKVCAM_HANDLER_IGNORE(UVCIOC_CTRL_MAP),

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
                    akvcam_node_t node,
                    unsigned int cmd,
                    void __user *arg)
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

                if (copy_from_user(data, arg, size) == 0) {
                    result = akvcam_ioctls_private[i].proc(node, data);

                    if (copy_to_user(arg, data, size) != 0)
                        result = -EIO;
                } else {
                    result = -EIO;
                }

                kfree(data);

                return result;
            }

            return -ENOTTY;
        }

    akpr_debug("Unhandled ioctl: %s\n", akvcam_string_from_ioctl(cmd));

    return -ENOTTY;
}


int akvcam_ioctls_querycap(akvcam_node_t node,
                           struct v4l2_capability *capability)
{
    __u32 capabilities = 0;
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    memset(capability, 0, sizeof(struct v4l2_capability));
    snprintf((char *) capability->driver, 16, "%s", akvcam_driver_name());
    snprintf((char *) capability->card,
             32, "%s", akvcam_device_description(device));
    snprintf((char *) capability->bus_info,
             32, "platform:akvcam-%d", akvcam_device_num(device));
    capability->version = akvcam_driver_version();

    switch (akvcam_device_v4l2_type(device)) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        capabilities = V4L2_CAP_VIDEO_CAPTURE;
        break;

    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
        capabilities = V4L2_CAP_VIDEO_CAPTURE_MPLANE;
        break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
        capabilities = V4L2_CAP_VIDEO_OUTPUT;
        break;

    default:
        capabilities = V4L2_CAP_VIDEO_OUTPUT_MPLANE;
        break;
    }

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        capabilities |= V4L2_CAP_READWRITE;

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_MMAP
        || akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_USERPTR)
        capabilities |= V4L2_CAP_STREAMING;

    capabilities |= V4L2_CAP_EXT_PIX_FORMAT;

    capability->capabilities = capabilities | V4L2_CAP_DEVICE_CAPS;
    capability->device_caps = capabilities;

    return 0;
}

#ifdef VIDIOC_QUERY_EXT_CTRL
int akvcam_ioctls_query_ext_ctrl(akvcam_node_t node,
                                 struct v4l2_query_ext_ctrl *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls = akvcam_device_controls_nr(device);

    return akvcam_controls_fill_ext(controls, control);
}
#endif

int akvcam_ioctls_g_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_get_ext(controls_, controls, 0);
}

int akvcam_ioctls_s_ext_ctrls(akvcam_node_t node,
                              struct v4l2_ext_controls *controls)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_set_ext(controls_, controls, 0);
}

int akvcam_ioctls_try_ext_ctrls(akvcam_node_t node,
                                struct v4l2_ext_controls *controls)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_try_ext(controls_, controls, 0);
}

int akvcam_ioctls_queryctrl(akvcam_node_t node, struct v4l2_queryctrl *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls = akvcam_device_controls_nr(device);

    return akvcam_controls_fill(controls, control);
}

int akvcam_ioctls_querymenu(akvcam_node_t node, struct v4l2_querymenu *menu)
{
    akvcam_device_t device;
    akvcam_controls_t controls;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls = akvcam_device_controls_nr(device);

    return akvcam_controls_fill_menu(controls, menu);
}

int akvcam_ioctls_g_ctrl(akvcam_node_t node, struct v4l2_control *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_get(controls_, control);
}

int akvcam_ioctls_s_ctrl(akvcam_node_t node, struct v4l2_control *control)
{
    akvcam_device_t device;
    akvcam_controls_t controls_;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    controls_ = akvcam_device_controls_nr(device);

    return akvcam_controls_set(controls_, control);
}

int akvcam_ioctls_enuminput(akvcam_node_t node, struct v4l2_input *input)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        return -ENOTTY;

    if (input->index > 0)
        return -EINVAL;

    memset(input, 0, sizeof(struct v4l2_input));
    snprintf((char *) input->name, 32, "akvcam-input");
    input->type = V4L2_INPUT_TYPE_CAMERA;

    return 0;
}

int akvcam_ioctls_g_input(akvcam_node_t node, int *input)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        return -ENOTTY;

    *input = 0;

    return 0;
}

int akvcam_ioctls_s_input(akvcam_node_t node, int *input)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        return -ENOTTY;

    return *input == 0? 0: -EINVAL;
}

int akvcam_ioctls_enumoutput(akvcam_node_t node, struct v4l2_output *output)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE)
        return -ENOTTY;

    if (output->index > 0)
        return -EINVAL;

    memset(output, 0, sizeof(struct v4l2_output));
    snprintf((char *) output->name, 32, "akvcam-output");
    output->type = V4L2_OUTPUT_TYPE_ANALOG;

    return 0;
}

int akvcam_ioctls_g_output(akvcam_node_t node, int *output)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE)
        return -ENOTTY;

    *output = 0;

    return 0;
}

int akvcam_ioctls_s_output(akvcam_node_t node, int *output)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE)
        return -ENOTTY;

    return *output == 0? 0: -EINVAL;
}

int akvcam_ioctls_enum_fmt(akvcam_node_t node, struct v4l2_fmtdesc *format)
{
    akvcam_device_t device;
    akvcam_formats_list_t formats;
    akvcam_pixel_formats_list_t pixel_formats;
    __u32 *fourcc;
    const char *description;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (format->type != akvcam_device_v4l2_type(device))
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
    size_t i;
    size_t bypl;
    size_t plane_size;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (format->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    current_format = akvcam_device_format_nr(device);
    memset(&format->fmt, 0, 200);

    if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE
        || format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        format->fmt.pix.width = (__u32) akvcam_format_width(current_format);
        format->fmt.pix.height = (__u32) akvcam_format_height(current_format);
        format->fmt.pix.pixelformat = akvcam_format_fourcc(current_format);
        format->fmt.pix.field = V4L2_FIELD_NONE;
        format->fmt.pix.bytesperline = (__u32) akvcam_format_bypl(current_format, 0);
        format->fmt.pix.sizeimage = (__u32) akvcam_format_size(current_format);
        format->fmt.pix.colorspace = DEFAULT_COLORSPACE;
    } else {
        format->fmt.pix_mp.width = (__u32) akvcam_format_width(current_format);
        format->fmt.pix_mp.height = (__u32) akvcam_format_height(current_format);
        format->fmt.pix_mp.pixelformat = akvcam_format_fourcc(current_format);
        format->fmt.pix_mp.field = V4L2_FIELD_NONE;
        format->fmt.pix_mp.colorspace = DEFAULT_COLORSPACE;
        format->fmt.pix_mp.num_planes = (__u8) akvcam_format_planes(current_format);

        for (i = 0; i < format->fmt.pix_mp.num_planes; i++) {
            bypl = akvcam_format_bypl(current_format, i);
            plane_size = akvcam_format_plane_size(current_format, i);
            format->fmt.pix_mp.plane_fmt[i].bytesperline = (__u32) bypl;
            format->fmt.pix_mp.plane_fmt[i].sizeimage = (__u32) plane_size;
        }
    }

    return 0;
}

int akvcam_ioctls_s_fmt(akvcam_node_t node, struct v4l2_format *format)
{
    akvcam_device_t device;
    akvcam_format_t current_format;
    akvcam_buffers_t buffers;
    int result;

    akpr_function();
    device = akvcam_node_device_nr(node);
    result = akvcam_ioctls_try_fmt(node, format);

    if (!result) {
        current_format = akvcam_device_format_nr(device);
        akvcam_format_set_fourcc(current_format, format->fmt.pix.pixelformat);
        akvcam_format_set_width(current_format, format->fmt.pix.width);
        akvcam_format_set_height(current_format, format->fmt.pix.height);

        buffers = akvcam_device_buffers_nr(device);
        akvcam_buffers_resize_rw(buffers,
                                 akvcam_buffers_size_rw(buffers));
    }

    return result;
}

int akvcam_ioctls_try_fmt(akvcam_node_t node, struct v4l2_format *format)
{
    akvcam_device_t device;
    akvcam_format_t nearest_format;
    akvcam_format_t temp_format;
    struct v4l2_fract frame_rate = {0, 0};
    size_t i;
    size_t bypl;
    size_t plane_size;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (format->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    if (akvcam_device_streaming(device))
        return -EBUSY;

    temp_format = akvcam_format_new(format->fmt.pix.pixelformat,
                                    format->fmt.pix.width,
                                    format->fmt.pix.height,
                                    &frame_rate);
    nearest_format = akvcam_format_nearest_nr(akvcam_device_formats_nr(device),
                                              temp_format);

    akvcam_format_delete(&temp_format);

    memset(&format->fmt, 0, 200);

    if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE
        || format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        format->fmt.pix.width = (__u32) akvcam_format_width(nearest_format);
        format->fmt.pix.height = (__u32) akvcam_format_height(nearest_format);
        format->fmt.pix.pixelformat = akvcam_format_fourcc(nearest_format);
        format->fmt.pix.field = V4L2_FIELD_NONE;
        format->fmt.pix.bytesperline = (__u32) akvcam_format_bypl(nearest_format, 0);
        format->fmt.pix.sizeimage = (__u32) akvcam_format_size(nearest_format);
        format->fmt.pix.colorspace = DEFAULT_COLORSPACE;
    } else {
        format->fmt.pix_mp.width = (__u32) akvcam_format_width(nearest_format);
        format->fmt.pix_mp.height = (__u32) akvcam_format_height(nearest_format);
        format->fmt.pix_mp.pixelformat = akvcam_format_fourcc(nearest_format);
        format->fmt.pix_mp.field = V4L2_FIELD_NONE;
        format->fmt.pix_mp.colorspace = DEFAULT_COLORSPACE;
        format->fmt.pix_mp.num_planes = (__u8) akvcam_format_planes(nearest_format);

        for (i = 0; i < format->fmt.pix_mp.num_planes; i++) {
            bypl = akvcam_format_bypl(nearest_format, i);
            plane_size = akvcam_format_plane_size(nearest_format, i);
            format->fmt.pix_mp.plane_fmt[i].bytesperline = (__u32) bypl;
            format->fmt.pix_mp.plane_fmt[i].sizeimage = (__u32) plane_size;
        }
    }

    return 0;
}

int akvcam_ioctls_g_parm(akvcam_node_t node, struct v4l2_streamparm *param)
{
    akvcam_device_t device;
    akvcam_format_t format;
    akvcam_buffers_t buffers;
    __u32 *n_buffers;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (param->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    memset(&param->parm, 0, 200);
    format = akvcam_device_format_nr(device);

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT
        || param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        param->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.output.timeperframe.numerator =
                akvcam_format_frame_rate(format)->denominator;
        param->parm.output.timeperframe.denominator =
                akvcam_format_frame_rate(format)->numerator;
        n_buffers = &param->parm.output.writebuffers;
    } else {
        param->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.capture.timeperframe.numerator =
                akvcam_format_frame_rate(format)->denominator;
        param->parm.capture.timeperframe.denominator =
                akvcam_format_frame_rate(format)->numerator;
        n_buffers = &param->parm.capture.readbuffers;
    }

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE) {
        buffers = akvcam_device_buffers_nr(device);
        *n_buffers = (__u32) akvcam_buffers_size_rw(buffers);
    }

    return 0;
}

int akvcam_ioctls_s_parm(akvcam_node_t node, struct v4l2_streamparm *param)
{
    akvcam_device_t device;
    akvcam_formats_list_t formats;
    akvcam_format_t format;
    akvcam_format_t nearest_format;
    akvcam_buffers_t buffers;
    __u32 total_buffers = 0;
    __u32 *n_buffers;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (param->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(format, akvcam_device_format_nr(device));

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT
        || param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        akvcam_format_frame_rate(format)->numerator =
                param->parm.output.timeperframe.denominator;
        akvcam_format_frame_rate(format)->denominator =
                param->parm.output.timeperframe.numerator;
    } else {
        akvcam_format_frame_rate(format)->numerator =
                param->parm.capture.timeperframe.denominator;
        akvcam_format_frame_rate(format)->denominator =
                param->parm.capture.timeperframe.numerator;
        total_buffers = param->parm.capture.readbuffers;
    }

    formats = akvcam_device_formats_nr(device);
    nearest_format = akvcam_format_nearest_nr(formats, format);

    if (!nearest_format) {
        akvcam_format_delete(&format);

        return -EINVAL;
    }

    akvcam_format_delete(&format);
    akvcam_format_copy(akvcam_device_format_nr(device), nearest_format);
    memset(&param->parm, 0, 200);

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT
        || param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        param->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.output.timeperframe.numerator =
                akvcam_format_frame_rate(nearest_format)->denominator;
        param->parm.output.timeperframe.denominator =
                akvcam_format_frame_rate(nearest_format)->numerator;
        n_buffers = &param->parm.output.writebuffers;
    } else {
        param->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.capture.timeperframe.numerator =
                akvcam_format_frame_rate(nearest_format)->denominator;
        param->parm.capture.timeperframe.denominator =
                akvcam_format_frame_rate(nearest_format)->numerator;
        n_buffers = &param->parm.capture.readbuffers;
    }

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE) {
        buffers = akvcam_device_buffers_nr(device);

        if (total_buffers) {
            if (akvcam_buffers_resize_rw(buffers, total_buffers))
                *n_buffers = total_buffers;
        } else {
            *n_buffers = (__u32) akvcam_buffers_size_rw(buffers);
        }
    }

    return 0;
}

int akvcam_ioctls_enum_framesizes(akvcam_node_t node,
                                  struct v4l2_frmsizeenum *frame_sizes)
{
    akvcam_device_t device;
    akvcam_formats_list_t formats;
    akvcam_resolutions_list_t resolutions;
    struct v4l2_frmsize_discrete *resolution;

    akpr_function();
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
    akvcam_formats_list_t formats;
    akvcam_fps_list_t frame_rates;
    struct v4l2_fract *frame_rate;

    akpr_function();
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
    akpr_function();
    *priority = akvcam_device_priority(akvcam_node_device_nr(node));

    return 0;
}

int akvcam_ioctls_s_priority(akvcam_node_t node, enum v4l2_priority *priority)
{
    akvcam_node_t priority_node;

    akpr_function();
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

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

    if (event->type != V4L2_EVENT_CTRL
        && event->type != V4L2_EVENT_FRAME_SYNC)
        return -EINVAL;

    controls = akvcam_device_controls_nr(device);

    if (!akvcam_controls_contains(controls, event->id))
        return -EINVAL;

    events = akvcam_node_events_nr(node);
    akvcam_events_subscribe(events, event);

    if (event->type == V4L2_EVENT_CTRL
        && event->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL)
        if (akvcam_controls_generate_event(controls, event->id, &control_event))
            akvcam_events_enqueue(events, &control_event);

    return 0;
}

int akvcam_ioctls_unsubscribe_event(akvcam_node_t node,
                                    struct v4l2_event_subscription *event)
{
    akvcam_device_t device;
    akvcam_events_t events;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE)
        return -ENOTTY;

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

    akpr_function();
    events = akvcam_node_events_nr(node);

    return akvcam_events_dequeue(events, event)? 0: -EINVAL;
}

int akvcam_ioctls_reqbufs(akvcam_node_t node, struct v4l2_requestbuffers *request)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;

    akpr_function();
    device = akvcam_node_device_nr(node);
    buffers = akvcam_device_buffers_nr(device);

    return akvcam_buffers_allocate(buffers, node, request);
}

int akvcam_ioctls_querybuf(akvcam_node_t node, struct v4l2_buffer *buffer)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    akvcam_format_t format;
    struct v4l2_plane *planes;
    size_t n_planes;
    size_t i;

    akpr_function();
    device = akvcam_node_device_nr(node);
    buffers = akvcam_device_buffers_nr(device);

    if (!akvcam_buffers_fill(buffers, buffer))
        return -EINVAL;

    if (!akvcam_device_multiplanar(device))
        return 0;

    if (buffer->length < 1)
        return 0;

    format = akvcam_device_format_nr(device);
    planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);
    n_planes = akvcam_min(buffer->length, akvcam_format_planes(format));
    copy_from_user(planes,
                   (char __user *) buffer->m.planes,
                   buffer->length * sizeof(struct v4l2_plane));

    for (i = 0; i < n_planes; i++) {
        if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE
            || planes[i].bytesused < 1) {
            planes[i].bytesused = (__u32) akvcam_format_plane_size(format, i);
            planes[i].data_offset = 0;
        }

        planes[i].length = (__u32) akvcam_format_plane_size(format, i);
        memset(&planes[i].m, 0, sizeof(struct v4l2_plane));

        if (buffer->memory == V4L2_MEMORY_MMAP) {
            planes[i].m.mem_offset =
                    buffer->index
                    * (__u32) akvcam_format_size(format)
                    + (__u32) akvcam_format_offset(format, i);
        }

        memset(planes[i].reserved, 0, 11 * sizeof(__u32));
    }

    copy_to_user((char __user *) buffer->m.planes,
                 planes,
                 buffer->length * sizeof(struct v4l2_plane));
    kfree(planes);

    return 0;
}

int akvcam_ioctl_create_bufs(akvcam_node_t node, struct v4l2_create_buffers *buffers)
{
    akvcam_device_t device;
    akvcam_buffers_t buffs;

    akpr_function();
    device = akvcam_node_device_nr(node);
    buffs = akvcam_device_buffers_nr(device);

    return akvcam_buffers_create(buffs, node, buffers);
}

int akvcam_ioctl_qbuf(akvcam_node_t node, struct v4l2_buffer *buffer)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    akvcam_format_t format;
    struct v4l2_plane *planes;
    size_t n_planes;
    size_t i;
    void *data;
    int result;

    akpr_function();
    device = akvcam_node_device_nr(node);
    buffers = akvcam_device_buffers_nr(device);
    result = akvcam_buffers_queue(buffers, buffer);

    if (result == 0
        && buffer->m.userptr
        && buffer->length > 1
        && buffer->memory == V4L2_MEMORY_USERPTR
        && akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT) {
        data = akvcam_buffers_buffers_data(buffers, buffer);

        if (data) {
            if (akvcam_device_multiplanar(device)) {
                format = akvcam_device_format_nr(device);
                planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);
                n_planes = akvcam_min(buffer->length, akvcam_format_planes(format));
                copy_from_user(planes,
                               (char __user *) buffer->m.planes,
                               buffer->length * sizeof(struct v4l2_plane));

                for (i = 0; i < n_planes; i++)
                    copy_from_user((char *) data + akvcam_format_offset(format, i),
                                   (char __user *) planes[i].m.userptr,
                                   planes[i].length);

                kfree(planes);
            } else {
                copy_from_user(data,
                               (char __user *) buffer->m.userptr,
                               buffer->length);
            }

            akvcam_buffers_process_frame(buffers, buffer);
        }
    }

    return result;
}

int akvcam_ioctl_dqbuf(akvcam_node_t node, struct v4l2_buffer *buffer)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    akvcam_format_t format;
    struct v4l2_plane *planes;
    size_t n_planes;
    size_t i;
    void *data;
    int result;

    akpr_function();
    device = akvcam_node_device_nr(node);
    buffers = akvcam_device_buffers_nr(device);
    result = akvcam_buffers_dequeue(buffers, buffer);

    if (result == 0
        && buffer->m.userptr
        && buffer->length > 1
        && buffer->memory == V4L2_MEMORY_USERPTR
        && akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE) {
        data = akvcam_buffers_buffers_data(buffers, buffer);

        if (data) {
            if (akvcam_device_multiplanar(device)) {
                format = akvcam_device_format_nr(device);
                planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);
                n_planes = akvcam_min(buffer->length, akvcam_format_planes(format));
                copy_from_user(planes,
                               (char __user *) buffer->m.planes,
                               buffer->length * sizeof(struct v4l2_plane));

                for (i = 0; i < n_planes; i++)
                    copy_to_user((char __user *) planes[i].m.userptr,
                                 (char *) data + akvcam_format_offset(format, i),
                                 planes[i].length);

                kfree(planes);
            } else {
                copy_to_user((char __user *) buffer->m.userptr,
                             data,
                             buffer->length);
            }
        }
    } else if (result == 0
               && buffer->memory == V4L2_MEMORY_MMAP
               && akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE
               && akvcam_device_multiplanar(device)) {
        format = akvcam_device_format_nr(device);
        planes = kmalloc(buffer->length * sizeof(struct v4l2_plane), GFP_KERNEL);
        n_planes = akvcam_min(buffer->length, akvcam_format_planes(format));
        copy_from_user(planes,
                       (char __user *) buffer->m.planes,
                       buffer->length * sizeof(struct v4l2_plane));

        for (i = 0; i < n_planes; i++)
            planes[i].m.mem_offset =
                    buffer->index
                    * (__u32) akvcam_format_size(format)
                    + (__u32) akvcam_format_offset(format, i);

        copy_to_user((char __user *) buffer->m.planes,
                     planes,
                     buffer->length * sizeof(struct v4l2_plane));
        kfree(planes);
    }

    return result;
}

int akvcam_ioctl_streamon(akvcam_node_t node, const int *type)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if ((enum v4l2_buf_type) *type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    akvcam_device_set_streaming(device, true);

    return 0;
}

int akvcam_ioctl_streamoff(akvcam_node_t node, const int *type)
{
    akvcam_device_t device;

    akpr_function();
    device = akvcam_node_device_nr(node);

    if ((enum v4l2_buf_type) *type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    akvcam_device_set_streaming(device, false);

    return 0;
}
