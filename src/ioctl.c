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

#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-dev.h>

#include "ioctl.h"
#include "buffers.h"
#include "device.h"
#include "driver.h"
#include "format.h"
#include "format_specs.h"
#include "list.h"
#include "log.h"

#define DEFAULT_COLORSPACE V4L2_COLORSPACE_RAW

int akvcam_ioctl_querycap(struct file *file,
                          void *fh,
                          struct v4l2_capability *capability);
int akvcam_ioctl_enum_fmt(struct file *file,
                          void *fh,
                          struct v4l2_fmtdesc *format);
int akvcam_ioctl_g_fmt(struct file *file,
                       void *fh,
                       struct v4l2_format *format);
int akvcam_ioctl_s_fmt(struct file *file,
                       void *fh,
                       struct v4l2_format *format);
int akvcam_ioctl_try_fmt(struct file *file,
                         void *fh,
                         struct v4l2_format *format);
int akvcam_ioctl_enum_input(struct file *file,
                            void *fh,
                            struct v4l2_input *input);
int akvcam_ioctl_g_input(struct file *file, void *fh, unsigned int *input);
int akvcam_ioctl_s_input(struct file *file, void *fh, unsigned int input);
int akvcam_ioctl_enum_output(struct file *file, void *fh,
                             struct v4l2_output *output);
int akvcam_ioctl_g_output(struct file *file, void *fh, unsigned int *output);
int akvcam_ioctl_s_output(struct file *file, void *fh, unsigned int output);
int akvcam_ioctl_g_parm(struct file *file,
                        void *fh,
                        struct v4l2_streamparm *param);
int akvcam_ioctl_s_parm(struct file *file,
                        void *fh,
                        struct v4l2_streamparm *param);
int akvcam_ioctl_enum_framesizes(struct file *file,
                                 void *fh,
                                 struct v4l2_frmsizeenum *frame_sizes);
int akvcam_ioctl_enum_frameintervals(struct file *file,
                                     void *fh,
                                     struct v4l2_frmivalenum *frame_intervals);

const struct v4l2_ioctl_ops *akvcam_ioctl_ops(void)
{
    static const struct v4l2_ioctl_ops ops = {
        .vidioc_querycap               = akvcam_ioctl_querycap           ,
        .vidioc_enum_fmt_vid_cap       = akvcam_ioctl_enum_fmt           ,
        .vidioc_enum_fmt_vid_out       = akvcam_ioctl_enum_fmt           ,
        .vidioc_g_fmt_vid_cap          = akvcam_ioctl_g_fmt              ,
        .vidioc_g_fmt_vid_out          = akvcam_ioctl_g_fmt              ,
        .vidioc_s_fmt_vid_cap          = akvcam_ioctl_s_fmt              ,
        .vidioc_s_fmt_vid_out          = akvcam_ioctl_s_fmt              ,
        .vidioc_try_fmt_vid_cap        = akvcam_ioctl_try_fmt            ,
        .vidioc_try_fmt_vid_out        = akvcam_ioctl_try_fmt            ,
        .vidioc_reqbufs                = vb2_ioctl_reqbufs               ,
        .vidioc_querybuf               = vb2_ioctl_querybuf              ,
        .vidioc_qbuf                   = vb2_ioctl_qbuf                  ,
        .vidioc_expbuf                 = vb2_ioctl_expbuf                ,
        .vidioc_dqbuf                  = vb2_ioctl_dqbuf                 ,
        .vidioc_create_bufs            = vb2_ioctl_create_bufs           ,
        .vidioc_prepare_buf            = vb2_ioctl_prepare_buf           ,
        .vidioc_streamon               = vb2_ioctl_streamon              ,
        .vidioc_streamoff              = vb2_ioctl_streamoff             ,
        .vidioc_enum_input             = akvcam_ioctl_enum_input         ,
        .vidioc_g_input                = akvcam_ioctl_g_input            ,
        .vidioc_s_input                = akvcam_ioctl_s_input            ,
        .vidioc_enum_output            = akvcam_ioctl_enum_output        ,
        .vidioc_g_output               = akvcam_ioctl_g_output           ,
        .vidioc_s_output               = akvcam_ioctl_s_output           ,
        .vidioc_g_parm                 = akvcam_ioctl_g_parm             ,
        .vidioc_s_parm                 = akvcam_ioctl_s_parm             ,
        .vidioc_log_status             = v4l2_ctrl_log_status            ,
        .vidioc_enum_framesizes        = akvcam_ioctl_enum_framesizes    ,
        .vidioc_enum_frameintervals    = akvcam_ioctl_enum_frameintervals,
        .vidioc_subscribe_event        = v4l2_ctrl_subscribe_event       ,
        .vidioc_unsubscribe_event      = v4l2_event_unsubscribe          ,
    };

    return &ops;
}

static struct v4l2_fract akvcam_ioctl_invert_fract(struct v4l2_fract fract)
{
    struct v4l2_fract inverted = {fract.denominator, fract.numerator};

    return inverted;
}

static akvcam_format_t akvcam_ioctl_nearest_format(akvcam_device_ct device,
                                                   akvcam_format_ct format)
{
    akvcam_formats_list_t formats;
    akvcam_format_t nearest_format;

    if (akvcam_device_dynamic_formats(device))
        return akvcam_format_dynamic_nearest(format);

    formats = akvcam_device_formats(device);
    nearest_format = akvcam_format_nearest(formats, format);
    akvcam_list_delete(formats);

    return nearest_format;
}

int akvcam_ioctl_querycap(struct file *file,
                          void *fh,
                          struct v4l2_capability *capability)
{
    akvcam_device_t device = video_drvdata(file);
    __u32 caps;
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    memset(capability, 0, sizeof(struct v4l2_capability));
    snprintf((char *) capability->driver, 16, "%s", akvcam_driver_name());
    snprintf((char *) capability->card,
             32, "%s", akvcam_device_description(device));
    snprintf((char *) capability->bus_info,
             32, "platform:akvcam-%d", akvcam_device_num(device));
    capability->version = akvcam_driver_version();

    caps = akvcam_device_caps(device);
    capability->capabilities = caps | V4L2_CAP_DEVICE_CAPS;
    capability->device_caps = caps;

    return 0;
}

int akvcam_ioctl_enum_fmt(struct file *file,
                          void *fh,
                          struct v4l2_fmtdesc *format)
{
    akvcam_device_t device = video_drvdata(file);
    akvcam_formats_list_t formats;
    akvcam_pixel_formats_list_t pixel_formats = NULL;
    __u32 *fourcc;
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (format->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    if (akvcam_device_dynamic_formats(device)) {
        __u32 dynamic_fourcc = akvcam_pixel_format_by_index(format->index);

        if (!dynamic_fourcc)
            return -EINVAL;

        format->flags = 0;
        format->pixelformat = dynamic_fourcc;
        snprintf((char *) format->description, 32, "%s",
                 akvcam_string_from_fourcc(dynamic_fourcc));
        akvcam_init_reserved(format);

        return 0;
    }

    formats = akvcam_device_formats(device);
    pixel_formats = akvcam_format_pixel_formats(formats);
    akvcam_list_delete(formats);
    fourcc = akvcam_list_at(pixel_formats, format->index);

    if (fourcc) {
        const char *description;

        format->flags = 0;
        format->pixelformat = *fourcc;
        description = akvcam_string_from_fourcc(format->pixelformat);
        snprintf((char *) format->description, 32, "%s", description);
        akvcam_init_reserved(format);
    }

    akvcam_list_delete(pixel_formats);

    return fourcc? 0: -EINVAL;
}

int akvcam_ioctl_g_fmt(struct file *file, void *fh, struct v4l2_format *format)
{
    akvcam_device_t device = video_drvdata(file);
    akvcam_format_t current_format;
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (format->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    current_format = akvcam_device_format_nr(device);
    memset(&format->fmt, 0, sizeof(format->fmt));
    format->fmt.pix.width = (__u32) akvcam_format_width(current_format);
    format->fmt.pix.height = (__u32) akvcam_format_height(current_format);
    format->fmt.pix.pixelformat = akvcam_format_fourcc(current_format);
    format->fmt.pix.field = V4L2_FIELD_NONE;
    format->fmt.pix.bytesperline = (__u32) akvcam_format_line_size(current_format, 0);
    format->fmt.pix.sizeimage = (__u32) akvcam_format_size(current_format);
    format->fmt.pix.colorspace = DEFAULT_COLORSPACE;

    return 0;
}

int akvcam_ioctl_s_fmt(struct file *file, void *fh, struct v4l2_format *format)
{
    akvcam_device_t device = video_drvdata(file);
    int result;

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    result = akvcam_ioctl_try_fmt(file, fh, format);

    if (result == 0) {
        akvcam_format_t device_format = akvcam_device_format_nr(device);
        akvcam_format_t current_format = NULL;
        struct v4l2_fract frame_rate = akvcam_format_frame_rate(device_format);

        current_format = akvcam_format_new(format->fmt.pix.pixelformat,
                                           format->fmt.pix.width,
                                           format->fmt.pix.height,
                                           &frame_rate);
        akvcam_device_set_format(device, current_format);
        akvcam_format_delete(current_format);
    }

    return result;
}

int akvcam_ioctl_try_fmt(struct file *file,
                         void *fh,
                         struct v4l2_format *format)
{
    akvcam_device_t device = video_drvdata(file);
    akvcam_format_t nearest_format = NULL;
    akvcam_format_t temp_format;
    struct v4l2_fract frame_rate = {0, 0};
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (format->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    if (akvcam_device_streaming(device))
        return -EBUSY;

    temp_format = akvcam_format_new(format->fmt.pix.pixelformat,
                                    format->fmt.pix.width,
                                    format->fmt.pix.height,
                                    &frame_rate);
    nearest_format = akvcam_ioctl_nearest_format(device, temp_format);
    akvcam_format_delete(temp_format);

    if (!nearest_format)
        return -EINVAL;

    memset(&format->fmt, 0, sizeof(format->fmt));
    format->fmt.pix.width = (__u32) akvcam_format_width(nearest_format);
    format->fmt.pix.height = (__u32) akvcam_format_height(nearest_format);
    format->fmt.pix.pixelformat = akvcam_format_fourcc(nearest_format);
    format->fmt.pix.field = V4L2_FIELD_NONE;
    format->fmt.pix.bytesperline = (__u32) akvcam_format_line_size(nearest_format, 0);
    format->fmt.pix.sizeimage = (__u32) akvcam_format_size(nearest_format);
    format->fmt.pix.colorspace = DEFAULT_COLORSPACE;
    akvcam_format_delete(nearest_format);

    return 0;
}

int akvcam_ioctl_enum_input(struct file *file,
                            void *fh,
                            struct v4l2_input *input)
{
    akvcam_device_t device = video_drvdata(file);
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        return -ENOTTY;

    if (input->index > 0)
        return -EINVAL;

    memset(input, 0, sizeof(struct v4l2_input));
    snprintf((char *) input->name, 32, "akvcam-input");
    input->type = V4L2_INPUT_TYPE_CAMERA;

    return 0;
}

int akvcam_ioctl_g_input(struct file *file, void *fh, unsigned int *input)
{
    akvcam_device_t device = video_drvdata(file);
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (!input)
        return -EINVAL;

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        return -ENOTTY;

    *input = 0;

    return 0;
}

int akvcam_ioctl_s_input(struct file *file, void *fh, unsigned int input)
{
    akvcam_device_t device = video_drvdata(file);
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
        return -ENOTTY;

    return input == 0? 0: -EINVAL;
}

int akvcam_ioctl_enum_output(struct file *file,
                             void *fh,
                             struct v4l2_output *output)
{
    akvcam_device_t device = video_drvdata(file);
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE)
        return -ENOTTY;

    if (output->index > 0)
        return -EINVAL;

    memset(output, 0, sizeof(struct v4l2_output));
    snprintf((char *) output->name, 32, "akvcam-output");
    output->type = V4L2_OUTPUT_TYPE_ANALOG;

    return 0;
}

int akvcam_ioctl_g_output(struct file *file, void *fh, unsigned int *output)
{
    akvcam_device_t device = video_drvdata(file);
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (!output)
        return -EINVAL;

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE)
        return -ENOTTY;

    *output = 0;

    return 0;
}

int akvcam_ioctl_s_output(struct file *file, void *fh, unsigned int output)
{
    akvcam_device_t device = video_drvdata(file);
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_CAPTURE)
        return -ENOTTY;

    return output == 0? 0: -EINVAL;
}

int akvcam_ioctl_g_parm(struct file *file,
                        void *fh,
                        struct v4l2_streamparm *param)
{
    akvcam_device_t device = video_drvdata(file);
    akvcam_format_t format;
    __u32 *n_buffers;
    struct v4l2_fract frame_rate;
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (param->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    memset(&param->parm, 0, sizeof(param->parm));
    format = akvcam_device_format_nr(device);
    frame_rate = akvcam_format_frame_rate(format);
    frame_rate = akvcam_ioctl_invert_fract(frame_rate);

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        param->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.output.timeperframe = frame_rate;
        n_buffers = &param->parm.output.writebuffers;
    } else {
        param->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.capture.timeperframe = frame_rate;
        n_buffers = &param->parm.capture.readbuffers;
    }

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE) {
        akvcam_buffers_t buffers = akvcam_device_buffers_nr(device);
        *n_buffers = (__u32) akvcam_buffers_count(buffers);
    }

    return 0;
}

int akvcam_ioctl_s_parm(struct file *file,
                        void *fh,
                        struct v4l2_streamparm *param)
{
    akvcam_device_t device = video_drvdata(file);
    akvcam_format_t device_format;
    akvcam_format_t format = NULL;
    akvcam_format_t nearest_format = NULL;
    struct v4l2_fract frame_rate;
    __u32 total_buffers = 0;
    __u32 *n_buffers;
    int result = 0;
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_streaming(device))
        return -EBUSY;

    if (param->type != akvcam_device_v4l2_type(device))
        return -EINVAL;

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        frame_rate = param->parm.output.timeperframe;
    } else {
        frame_rate = param->parm.capture.timeperframe;
        total_buffers = param->parm.capture.readbuffers;
    }

    frame_rate = akvcam_ioctl_invert_fract(frame_rate);
    device_format = akvcam_device_format_nr(device);
    format = akvcam_format_new(akvcam_format_fourcc(device_format),
                               akvcam_format_width(device_format),
                               akvcam_format_height(device_format),
                               &frame_rate);
    nearest_format = akvcam_ioctl_nearest_format(device, format);

    if (!nearest_format) {
        akvcam_format_delete(format);

        return -EINVAL;
    }

    akvcam_format_delete(format);
    akvcam_device_set_format(device, nearest_format);
    memset(&param->parm, 0, 200);

    if (param->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        param->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.output.timeperframe.numerator =
                akvcam_format_frame_rate(nearest_format).denominator;
        param->parm.output.timeperframe.denominator =
                akvcam_format_frame_rate(nearest_format).numerator;
        n_buffers = &param->parm.output.writebuffers;
    } else {
        param->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
        param->parm.capture.timeperframe.numerator =
                akvcam_format_frame_rate(nearest_format).denominator;
        param->parm.capture.timeperframe.denominator =
                akvcam_format_frame_rate(nearest_format).numerator;
        n_buffers = &param->parm.capture.readbuffers;
    }

    akvcam_format_delete(nearest_format);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE) {
        akvcam_buffers_t buffers = akvcam_device_buffers_nr(device);

        if (total_buffers) {
            akvcam_buffers_set_count(buffers, total_buffers);
            *n_buffers = total_buffers;
        } else {
            *n_buffers = (__u32) akvcam_buffers_count(buffers);
        }
    }

    return result;
}

int akvcam_ioctl_enum_framesizes(struct file *file,
                                 void *fh,
                                 struct v4l2_frmsizeenum *frame_sizes)
{
    akvcam_device_t device = video_drvdata(file);
    akvcam_formats_list_t formats;
    akvcam_resolutions_list_t resolutions = NULL;
    struct v4l2_frmsize_discrete *resolution;
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_dynamic_formats(device)) {
        size_t step_width;
        size_t step_height;

        // A single stepwise range per supported pixel format.
        if (frame_sizes->index != 0
            || !akvcam_format_is_supported(frame_sizes->pixel_format))
            return -EINVAL;

        akvcam_format_size_step(frame_sizes->pixel_format,
                                &step_width,
                                &step_height);
        frame_sizes->type = V4L2_FRMSIZE_TYPE_STEPWISE;
        frame_sizes->stepwise.min_width = (__u32) step_width;
        frame_sizes->stepwise.max_width = AKVCAM_DYNAMIC_MAX_WIDTH;
        frame_sizes->stepwise.step_width = (__u32) step_width;
        frame_sizes->stepwise.min_height = (__u32) step_height;
        frame_sizes->stepwise.max_height = AKVCAM_DYNAMIC_MAX_HEIGHT;
        frame_sizes->stepwise.step_height = (__u32) step_height;
        akvcam_init_reserved(frame_sizes);

        return 0;
    }

    formats = akvcam_device_formats(device);
    resolutions = akvcam_format_resolutions(formats,
                                            frame_sizes->pixel_format);
    akvcam_list_delete(formats);
    resolution = akvcam_list_at(resolutions, frame_sizes->index);

    if (resolution) {
        frame_sizes->type = V4L2_FRMSIZE_TYPE_DISCRETE;
        frame_sizes->discrete.width = resolution->width;
        frame_sizes->discrete.height = resolution->height;
        akvcam_init_reserved(frame_sizes);
    }

    akvcam_list_delete(resolutions);

    return resolution? 0: -EINVAL;
}

int akvcam_ioctl_enum_frameintervals(struct file *file,
                                     void *fh,
                                     struct v4l2_frmivalenum *frame_intervals)
{
    akvcam_device_t device = video_drvdata(file);
    akvcam_formats_list_t formats;
    akvcam_fps_list_t frame_rates = NULL;
    struct v4l2_fract *frame_rate;
    UNUSED(fh);

    akpr_function();
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_dynamic_formats(device)) {
        size_t step_width;
        size_t step_height;

        // A single continuous range for every valid pixel format and size.
        if (frame_intervals->index != 0
            || !akvcam_format_is_supported(frame_intervals->pixel_format))
            return -EINVAL;

        akvcam_format_size_step(frame_intervals->pixel_format,
                                &step_width,
                                &step_height);

        if (frame_intervals->width < step_width
            || frame_intervals->width > AKVCAM_DYNAMIC_MAX_WIDTH
            || frame_intervals->width % step_width
            || frame_intervals->height < step_height
            || frame_intervals->height > AKVCAM_DYNAMIC_MAX_HEIGHT
            || frame_intervals->height % step_height)
            return -EINVAL;

        frame_intervals->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
        frame_intervals->stepwise.min.numerator = 1;
        frame_intervals->stepwise.min.denominator = AKVCAM_DYNAMIC_MAX_FPS;
        frame_intervals->stepwise.max.numerator = 1;
        frame_intervals->stepwise.max.denominator = AKVCAM_DYNAMIC_MIN_FPS;
        frame_intervals->stepwise.step.numerator = 1;
        frame_intervals->stepwise.step.denominator = 1;
        akvcam_init_reserved(frame_intervals);

        return 0;
    }

    formats = akvcam_device_formats(device);
    frame_rates = akvcam_format_frame_rates(formats,
                                            frame_intervals->pixel_format,
                                            frame_intervals->width,
                                            frame_intervals->height);
    akvcam_list_delete(formats);
    frame_rate = akvcam_list_at(frame_rates, frame_intervals->index);

    if (frame_rate) {
        frame_intervals->type = V4L2_FRMIVAL_TYPE_DISCRETE;
        frame_intervals->discrete.numerator = frame_rate->denominator;
        frame_intervals->discrete.denominator = frame_rate->numerator;
        akvcam_init_reserved(frame_intervals);
    }

    akvcam_list_delete(frame_rates);

    return frame_rate? 0: -EINVAL;
}
