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
#include <linux/module.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>

#include "ioctl_ops.h"
#include "controls.h"
#include "driver.h"
#include "format.h"
#include "list.h"
#include "utils.h"

int akvcam_querycap(struct file *filp, void *fh, struct v4l2_capability *cap)
{
    akvcam_device_t device;
    const char *name;
    const char *description;
    __u32 capabilities;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    name = akvcam_driver_name();
    description = akvcam_driver_description();

    snprintf((char *) cap->driver, 16, "%s", name);
    snprintf((char *) cap->card, 32, "%s", description);
    snprintf((char *) cap->bus_info, 32, "platform:akvcam-%03d",
             akvcam_device_num(device));

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        capabilities = V4L2_CAP_VIDEO_OUTPUT;
    else
        capabilities = V4L2_CAP_VIDEO_CAPTURE;

    capabilities |= V4L2_CAP_STREAMING;

    cap->capabilities = capabilities | V4L2_CAP_DEVICE_CAPS;
    cap->device_caps = capabilities;
    memset(cap->reserved, 0, 3 * sizeof(__u32));

    return 0;
}

int akvcam_enum_fmt_vid_cap(struct file *filp,
                            void *fh,
                            struct v4l2_fmtdesc *desc)
{
    akvcam_device_t device;
    akvcam_list_t formats;
    akvcam_list_t pixel_formats;
    __u32 *fourcc;
    enum v4l2_buf_type type;
    const char *description;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (desc->type != type)
        return -EINVAL;

    formats = akvcam_device_formats_nr(device);
    pixel_formats = akvcam_format_pixel_formats(formats);
    fourcc = akvcam_list_at(pixel_formats, desc->index);

    if (fourcc) {
        desc->flags = 0;
        desc->pixelformat = *fourcc;
        description = akvcam_format_string_from_fourcc(desc->pixelformat);
        snprintf((char *) desc->description, 32, "%s", description);
        memset(desc->reserved, 0, 4 * sizeof(__u32));
    }

    akvcam_list_delete(&pixel_formats);

    return fourcc? 0: -EINVAL;
}

int akvcam_g_fmt_vid_cap(struct file *filp,
                         void *fh,
                         struct v4l2_format *format)
{
    akvcam_device_t device;
    akvcam_format_t current_format;
    enum v4l2_buf_type type;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (format->type != type)
        return -EINVAL;

    current_format = akvcam_device_format_nr(device);
    memset(&format->fmt.pix, 0, sizeof(struct v4l2_pix_format));
    format->fmt.pix.width = akvcam_format_width(current_format);
    format->fmt.pix.height = akvcam_format_height(current_format);
    format->fmt.pix.pixelformat = akvcam_format_fourcc(current_format);
    format->fmt.pix.field = V4L2_FIELD_NONE;
    format->fmt.pix.bytesperline = (__u32) akvcam_format_bypl(current_format);
    format->fmt.pix.sizeimage = (__u32) akvcam_format_size(current_format);
    format->fmt.pix.colorspace = 0;

    return 0;
}

int akvcam_try_fmt_vid_cap(struct file *filp,
                           void *fh,
                           struct v4l2_format *format)
{
    akvcam_device_t device;
    akvcam_format_t nearest_format;
    akvcam_format_t temp_format;
    struct v4l2_fract frame_rate = {0, 0};
    enum v4l2_buf_type type;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
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
    format->fmt.pix.colorspace = 0;

    return 0;
}

int akvcam_s_fmt_vid_cap(struct file *filp,
                         void *fh,
                         struct v4l2_format *format)
{
    akvcam_device_t device;
    akvcam_format_t current_format;
    int result;

    printk(KERN_INFO "%s\n", __FUNCTION__);
    result = akvcam_try_fmt_vid_cap(filp, fh, format);

    if (!result) {
        device = akvcam_device_from_file_nr(filp);
        current_format = akvcam_device_format_nr(device);
        akvcam_format_set_fourcc(current_format, format->fmt.pix.pixelformat);
        akvcam_format_set_width(current_format, format->fmt.pix.width);
        akvcam_format_set_height(current_format, format->fmt.pix.height);
    }

    return result;
}

int akvcam_enum_fmt_vid_out(struct file *filp,
                            void *fh,
                            struct v4l2_fmtdesc *desc)
{
    printk(KERN_INFO "%s\n", __FUNCTION__);

    return -1;
}

int akvcam_g_fmt_vid_out(struct file *filp,
                         void *fh,
                         struct v4l2_format *format)
{
    printk(KERN_INFO "%s\n", __FUNCTION__);

    return -1;
}

int akvcam_s_fmt_vid_out(struct file *filp,
                         void *fh,
                         struct v4l2_format *format)
{
    printk(KERN_INFO "%s\n", __FUNCTION__);

    return -1;
}

int akvcam_try_fmt_vid_out(struct file *filp,
                           void *fh,
                           struct v4l2_format *format)
{
    printk(KERN_INFO "%s\n", __FUNCTION__);

    return -1;
}

int akvcam_enum_input(struct file *filp, void *fh, struct v4l2_input *input)
{
    UNUSED(filp);
    UNUSED(fh);
    printk(KERN_INFO "%s\n", __FUNCTION__);

    if (input->index > 0)
        return -EINVAL;

    snprintf((char *) input->name, 32, "akvcam-input");
    input->type = V4L2_INPUT_TYPE_CAMERA;
    input->audioset = 0;
    input->tuner = 0;
    input->std = 0;
    input->status = 0;
    input->capabilities = 0;
    memset(input->reserved, 0, 3 * sizeof(__u32));

    return 0;
}

int akvcam_g_input(struct file *filp, void *fh, unsigned int *i)
{
    UNUSED(filp);
    UNUSED(fh);
    printk(KERN_INFO "%s\n", __FUNCTION__);
    *i = 0;

    return 0;
}

int akvcam_s_input(struct file *filp, void *fh, unsigned int i)
{
    UNUSED(filp);
    UNUSED(fh);
    printk(KERN_INFO "%s\n", __FUNCTION__);

    return i == 0? 0: -EINVAL;
}

int akvcam_enum_output(struct file *filp, void *fh, struct v4l2_output *output)
{
    UNUSED(filp);
    UNUSED(fh);
    printk(KERN_INFO "%s\n", __FUNCTION__);

    if (output->index > 0)
        return -EINVAL;

    snprintf((char *) output->name, 32, "akvcam-output");
    output->type = V4L2_INPUT_TYPE_CAMERA;
    output->audioset = 0;
    output->modulator = 0;
    output->std = 0;
    output->capabilities = 0;
    memset(output->reserved, 0, 3 * sizeof(__u32));

    return 0;
}

int akvcam_g_output(struct file *filp, void *fh, unsigned int *i)
{
    UNUSED(filp);
    UNUSED(fh);
    printk(KERN_INFO "%s\n", __FUNCTION__);
    *i = 0;

    return 0;
}

int akvcam_s_output(struct file *filp, void *fh, unsigned int i)
{
    UNUSED(filp);
    UNUSED(fh);
    printk(KERN_INFO "%s\n", __FUNCTION__);

    return i == 0? 0: -EINVAL;
}

int akvcam_queryctrl(struct file *filp, void *fh, struct v4l2_queryctrl *ctrl)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_queryctrl(akvcam_controls_handler(controls), ctrl);
}

int akvcam_query_ext_ctrl(struct file *filp,
                          void *fh,
                          struct v4l2_query_ext_ctrl *ctrl)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_query_ext_ctrl(akvcam_controls_handler(controls), ctrl);
}

int akvcam_g_ctrl(struct file *filp, void *fh, struct v4l2_control *ctrl)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_g_ctrl(akvcam_controls_handler(controls), ctrl);
}

int akvcam_s_ctrl(struct file *filp, void *fh, struct v4l2_control *ctrl)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_s_ctrl(NULL, akvcam_controls_handler(controls), ctrl);
}

int akvcam_g_ext_ctrls(struct file *filp,
                       void *fh,
                       struct v4l2_ext_controls *ctrl)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_g_ext_ctrls(akvcam_controls_handler(controls), ctrl);
}

int akvcam_s_ext_ctrls(struct file *filp,
                       void *fh,
                       struct v4l2_ext_controls *ctrl)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_s_ext_ctrls(NULL, akvcam_controls_handler(controls), ctrl);
}

int akvcam_try_ext_ctrls(struct file *filp,
                         void *fh,
                         struct v4l2_ext_controls *ctrl)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_try_ext_ctrls(akvcam_controls_handler(controls), ctrl);
}

int akvcam_querymenu(struct file *filp, void *fh, struct v4l2_querymenu *menu)
{
    akvcam_device_t device;
    akvcam_controls_t controls;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    controls = akvcam_device_controls_nr(device);

    return v4l2_querymenu(akvcam_controls_handler(controls), menu);
}

int akvcam_g_parm(struct file *filp, void *fh, struct v4l2_streamparm *param)
{
    akvcam_device_t device;
    akvcam_format_t format;
    enum v4l2_buf_type type;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (param->type != type)
        return -EINVAL;

    memset(&param->parm, 0, 200);
    format = akvcam_device_format_nr(device);

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

int akvcam_s_parm(struct file *filp, void *fh, struct v4l2_streamparm *param)
{
    akvcam_device_t device;
    akvcam_list_t formats;
    akvcam_format_t format;
    akvcam_format_t nearest_format;
    enum v4l2_buf_type type;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (param->type != type)
        return -EINVAL;

    format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(format, akvcam_device_format_nr(device));

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
    akvcam_format_copy(akvcam_device_format_nr(device), nearest_format);
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

int akvcam_enum_framesizes(struct file *filp,
                           void *fh,
                           struct v4l2_frmsizeenum *fsize)
{
    akvcam_device_t device;
    akvcam_list_t formats;
    akvcam_list_t resolutions;
    struct v4l2_frmsize_discrete *resolution;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    formats = akvcam_device_formats_nr(device);
    resolutions = akvcam_format_resolutions(formats, fsize->pixel_format);
    resolution = akvcam_list_at(resolutions, fsize->index);

    if (resolution) {
        fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        fsize->discrete.width = resolution->width;
        fsize->discrete.height = resolution->height;
        memset(fsize->reserved, 0, 2 * sizeof(__u32));
    }

    akvcam_list_delete(&resolutions);

    return resolution? 0: -EINVAL;
}

int akvcam_enum_frameintervals(struct file *filp,
                               void *fh,
                               struct v4l2_frmivalenum *fival)
{
    akvcam_device_t device;
    akvcam_list_t formats;
    akvcam_list_t frame_rates;
    struct v4l2_fract *frame_rate;
    UNUSED(fh);

    printk(KERN_INFO "%s\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    formats = akvcam_device_formats_nr(device);
    frame_rates = akvcam_format_frame_rates(formats,
                                            fival->pixel_format,
                                            fival->width,
                                            fival->height);
    frame_rate = akvcam_list_at(frame_rates, fival->index);

    if (frame_rate) {
        fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
        fival->discrete.numerator = frame_rate->denominator;
        fival->discrete.denominator = frame_rate->numerator;
        memset(fival->reserved, 0, 2 * sizeof(__u32));
    }

    akvcam_list_delete(&frame_rates);

    return frame_rate? 0: -EINVAL;
}

static struct v4l2_ioctl_ops ioctl_ops = {
    .vidioc_querycap = akvcam_querycap,

    // Buffer handlers
    .vidioc_reqbufs     = vb2_ioctl_reqbufs    ,
    .vidioc_querybuf    = vb2_ioctl_querybuf   ,
    .vidioc_qbuf        = vb2_ioctl_qbuf       ,
    .vidioc_expbuf      = vb2_ioctl_expbuf     ,
    .vidioc_dqbuf       = vb2_ioctl_dqbuf      ,
    .vidioc_create_bufs = vb2_ioctl_create_bufs,
    .vidioc_prepare_buf = vb2_ioctl_prepare_buf,

    // Stream on/off
    .vidioc_streamon  = vb2_ioctl_streamon ,
    .vidioc_streamoff = vb2_ioctl_streamoff,

    // Control handling
    .vidioc_queryctrl      = akvcam_queryctrl     ,
    .vidioc_query_ext_ctrl = akvcam_query_ext_ctrl,
    .vidioc_g_ctrl         = akvcam_g_ctrl        ,
    .vidioc_s_ctrl         = akvcam_s_ctrl        ,
    .vidioc_g_ext_ctrls    = akvcam_g_ext_ctrls   ,
    .vidioc_s_ext_ctrls    = akvcam_s_ext_ctrls   ,
    .vidioc_try_ext_ctrls  = akvcam_try_ext_ctrls ,
    .vidioc_querymenu      = akvcam_querymenu     ,

    // Stream type-dependent parameter ioctls
    .vidioc_g_parm = akvcam_g_parm,
    .vidioc_s_parm = akvcam_s_parm,

    .vidioc_log_status = v4l2_ctrl_log_status,

    // Debugging ioctls
    .vidioc_enum_framesizes     = akvcam_enum_framesizes    ,
    .vidioc_enum_frameintervals = akvcam_enum_frameintervals,

    .vidioc_subscribe_event   = v4l2_ctrl_subscribe_event,
    .vidioc_unsubscribe_event = v4l2_event_unsubscribe   ,
};

struct v4l2_ioctl_ops *akvcam_ioctl_ops_get(AKVCAM_DEVICE_TYPE type)
{
    struct v4l2_ioctl_ops *ops =
            kzalloc(sizeof(struct v4l2_ioctl_ops), GFP_KERNEL);

    if (!ops) {
        akvcam_set_last_error(-ENOMEM);

        return NULL;
    }

    memcpy(ops, &ioctl_ops, sizeof(struct v4l2_ioctl_ops));

    if (type == AKVCAM_DEVICE_TYPE_CAPTURE) {
        // VIDIOC_ENUM_FMT handlers
        ops->vidioc_enum_fmt_vid_cap = akvcam_enum_fmt_vid_cap;

        // VIDIOC_G_FMT handlers
        ops->vidioc_g_fmt_vid_cap = akvcam_g_fmt_vid_cap;

        // VIDIOC_S_FMT handlers
        ops->vidioc_s_fmt_vid_cap = akvcam_s_fmt_vid_cap;

        // VIDIOC_TRY_FMT handlers
        ops->vidioc_try_fmt_vid_cap = akvcam_try_fmt_vid_cap;

        // Input handling
        ops->vidioc_enum_input = akvcam_enum_input;
        ops->vidioc_g_input = akvcam_g_input;
        ops->vidioc_s_input = akvcam_s_input;
    } else {
        // VIDIOC_ENUM_FMT handlers
        ops->vidioc_enum_fmt_vid_out = akvcam_enum_fmt_vid_out;

        // VIDIOC_G_FMT handlers
        ops->vidioc_g_fmt_vid_out = akvcam_g_fmt_vid_out;

        // VIDIOC_S_FMT handlers
        ops->vidioc_s_fmt_vid_out = akvcam_s_fmt_vid_out;

        // VIDIOC_TRY_FMT handlers
        ops->vidioc_try_fmt_vid_out = akvcam_try_fmt_vid_out;

        // Output handling
        ops->vidioc_enum_output = akvcam_enum_output;
        ops->vidioc_g_output = akvcam_g_output;
        ops->vidioc_s_output = akvcam_s_output;
    }

    akvcam_set_last_error(0);

    return ops;
}
