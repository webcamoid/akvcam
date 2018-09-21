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
#include <linux/version.h>
#include <linux/videodev2.h>

#include "format.h"
#include "list.h"
#include "object.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#define DEFAULT_COLORSPACE V4L2_COLORSPACE_SRGB
#else
#define DEFAULT_COLORSPACE V4L2_COLORSPACE_RAW
#endif

struct akvcam_format
{
    akvcam_object_t self;
    __u32 fourcc;
    __u32 width;
    __u32 height;
    struct v4l2_fract frame_rate;
};

typedef struct {
    __u32 fourcc;
    size_t bpp;
    char str[32];
} akvcam_format_globals, *akvcam_format_globals_t;

#define AKVCAM_NUM_FORMATS 10

static akvcam_format_globals akvcam_format_globals_formats[] = {
    {V4L2_PIX_FMT_RGB32 , 32, "RGB32"},
    {V4L2_PIX_FMT_RGB24 , 24, "RGB24"},
    {V4L2_PIX_FMT_RGB565, 16, "RGB16"},
    {V4L2_PIX_FMT_RGB555, 16, "RGB15"},
    {V4L2_PIX_FMT_BGR32 , 32, "BGR32"},
    {V4L2_PIX_FMT_BGR24 , 24, "BGR24"},
    {V4L2_PIX_FMT_UYVY  , 16, "UYVY" },
    {V4L2_PIX_FMT_YUYV  , 16, "YUY2" },
    {V4L2_PIX_FMT_NV12  , 12, "NV12" },
    {V4L2_PIX_FMT_NV21  , 12, "NV21" }
};

static akvcam_format_globals_t akvcam_format_globals_by_fourcc(__u32 fourcc)
{
    size_t i;

    for (i = 0; i < AKVCAM_NUM_FORMATS; i++)
        if (akvcam_format_globals_formats[i].fourcc == fourcc)
            return akvcam_format_globals_formats + i;

    return NULL;
}

static akvcam_format_globals_t akvcam_format_globals_by_str(const char *str)
{
    size_t i;

    for (i = 0; i < AKVCAM_NUM_FORMATS; i++)
        if (strcmp(akvcam_format_globals_formats[i].str, str) == 0)
            return akvcam_format_globals_formats + i;

    return NULL;
}

akvcam_format_t akvcam_format_new(__u32 fourcc,
                                  __u32 width,
                                  __u32 height,
                                  const struct v4l2_fract *frame_rate)
{
    akvcam_format_t self = kzalloc(sizeof(struct akvcam_format), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_format_delete);
    self->fourcc = fourcc;
    self->width = width;
    self->height = height;

    if (frame_rate)
        memcpy(&self->frame_rate, frame_rate, sizeof(struct v4l2_fract));
    else
        memset(&self->frame_rate, 0, sizeof(struct v4l2_fract));

    return self;
}

void akvcam_format_delete(akvcam_format_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

void akvcam_format_copy(akvcam_format_t self, const akvcam_format_t other)
{
    self->fourcc = other->fourcc;
    self->width = other->width;
    self->height = other->height;
    memcpy(&self->frame_rate, &other->frame_rate, sizeof(struct v4l2_fract));
}

__u32 akvcam_format_fourcc(const akvcam_format_t self)
{
    return self->fourcc;
}

void akvcam_format_set_fourcc(akvcam_format_t self, __u32 fourcc)
{
    self->fourcc = fourcc;
}

__u32 akvcam_format_width(const akvcam_format_t self)
{
    return self->width;
}

void akvcam_format_set_width(akvcam_format_t self, __u32 width)
{
    self->width = width;
}

__u32 akvcam_format_height(const akvcam_format_t self)
{
    return self->height;
}

void akvcam_format_set_height(akvcam_format_t self, __u32 height)
{
    self->height = height;
}

struct v4l2_fract *akvcam_format_frame_rate(const akvcam_format_t self)
{
    return &self->frame_rate;
}

size_t akvcam_format_bpp(const akvcam_format_t self)
{
    akvcam_format_globals_t vf = akvcam_format_globals_by_fourcc(self->fourcc);

    return vf? vf->bpp: 0;
}

size_t akvcam_format_bypl(const akvcam_format_t self)
{
    return self->width * akvcam_format_bpp(self) / 8;
}

size_t akvcam_format_size(const akvcam_format_t self)
{
    return self->height * akvcam_format_bypl(self);
}

void akvcam_format_clear(akvcam_format_t self)
{
    self->fourcc = 0;
    self->width = 0;
    self->height = 0;
    self->frame_rate.numerator = 0;
    self->frame_rate.denominator = 0;
}

void akvcam_format_round_nearest(int width, int height,
                                 int *owidth, int *oheight,
                                 int align)
{
    // *owidth = align * round(width / align)
    *owidth = (width + (align >> 1)) & ~(align - 1);

    // *oheight = round(height * owidth / width)
    *oheight = (2 * height * *owidth + width) / (2 * width);
}

__u32 akvcam_format_fourcc_from_string(const char *fourcc_str)
{
    akvcam_format_globals_t vf = akvcam_format_globals_by_str(fourcc_str);

    return vf? vf->fourcc: 0;
}

const char *akvcam_format_string_from_fourcc(__u32 fourcc)
{
    akvcam_format_globals_t vf = akvcam_format_globals_by_fourcc(fourcc);

    return vf? vf->str: NULL;
}

akvcam_format_t akvcam_format_nearest_nr(struct akvcam_list *formats,
                                         const akvcam_format_t format)
{
    akvcam_list_element_t element = NULL;
    akvcam_format_t nearest_format = NULL;
    akvcam_format_t temp_format;
    __s64 diff_fourcc;
    __s64 diff_width;
    __s64 diff_height;
    __s64 diff_fps;
    __u64 r;
    __u64 s;
    memset(&s, 0xff, sizeof(__u64));

    for (;;) {
        temp_format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        diff_fourcc = akvcam_format_fourcc(temp_format)
                    - akvcam_format_fourcc(format);
        diff_width = akvcam_format_width(temp_format)
                   - akvcam_format_width(format);
        diff_height = akvcam_format_height(temp_format)
                    - akvcam_format_height(format);
        diff_fps = akvcam_format_frame_rate(temp_format)->numerator
                 * akvcam_format_frame_rate(format)->denominator
                 - akvcam_format_frame_rate(format)->numerator
                 * akvcam_format_frame_rate(temp_format)->denominator;

        r = (__u64)(diff_fourcc * diff_fourcc)
          + (__u64)(diff_width * diff_width)
          + (__u64)(diff_height * diff_height)
          + (__u64)(diff_fps * diff_fps);

        if (r < s) {
            s = r;
            nearest_format = temp_format;
        }
    }

    return nearest_format;
}

akvcam_format_t akvcam_format_nearest(struct akvcam_list *formats,
                                      const akvcam_format_t format)
{
    akvcam_format_t nearest_format = akvcam_format_nearest_nr(formats, format);

    if (nearest_format)
        akvcam_object_ref(AKVCAM_TO_OBJECT(nearest_format));

    return nearest_format;
}

struct akvcam_list *akvcam_format_pixel_formats(struct akvcam_list *formats)
{
    akvcam_list_element_t element = NULL;
    akvcam_list_element_t it;
    akvcam_list_tt(akvcam_format_t) supported_formats = akvcam_list_new();
    akvcam_format_t format = NULL;
    __u32 fourcc;

    for (;;) {
        format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        fourcc = akvcam_format_fourcc(format);
        it = akvcam_list_find(supported_formats, &fourcc, sizeof(__u32), NULL);

        if (!it)
            akvcam_list_push_back_copy(supported_formats,
                                       &fourcc,
                                       sizeof(__u32),
                                       akvcam_delete_data);
    }

    return supported_formats;
}

struct akvcam_list *akvcam_format_resolutions(struct akvcam_list *formats,
                                              __u32 fourcc)
{
    akvcam_list_element_t element = NULL;
    akvcam_list_element_t it;
    akvcam_list_tt(struct v4l2_frmsize_discrete) supported_resolutions =
            akvcam_list_new();
    akvcam_format_t format = NULL;
    struct v4l2_frmsize_discrete resolution;

    for (;;) {
        format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        if (akvcam_format_fourcc(format) != fourcc)
            continue;

        resolution.width = akvcam_format_width(format);
        resolution.height = akvcam_format_height(format);
        it = akvcam_list_find(supported_resolutions,
                              &resolution,
                              sizeof(struct v4l2_frmsize_discrete),
                              NULL);

        if (!it)
            akvcam_list_push_back_copy(supported_resolutions,
                                       &resolution,
                                       sizeof(struct v4l2_frmsize_discrete),
                                       akvcam_delete_data);
    }

    return supported_resolutions;
}

struct akvcam_list *akvcam_format_frame_rates(struct akvcam_list *formats,
                                              __u32 fourcc,
                                              __u32 width,
                                              __u32 height)
{
    akvcam_list_element_t element = NULL;
    akvcam_list_element_t it;
    akvcam_list_tt(struct v4l2_fract) supported_frame_rates = akvcam_list_new();
    akvcam_format_t format = NULL;
    struct v4l2_fract frame_rate;

    for (;;) {
        format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        if (akvcam_format_fourcc(format) != fourcc
            || akvcam_format_width(format) != width
            || akvcam_format_height(format) != height)
            continue;

        frame_rate.numerator = akvcam_format_frame_rate(format)->numerator;
        frame_rate.denominator = akvcam_format_frame_rate(format)->denominator;
        it = akvcam_list_find(supported_frame_rates,
                              &frame_rate,
                              sizeof(struct v4l2_fract),
                              NULL);

        if (!it)
            akvcam_list_push_back_copy(supported_frame_rates,
                                       &frame_rate,
                                       sizeof(struct v4l2_fract),
                                       akvcam_delete_data);
    }

    return supported_frame_rates;
}

akvcam_format_t akvcam_format_from_v4l2_nr(struct akvcam_list *formats,
                                           const struct v4l2_format *format)
{
    akvcam_list_element_t element = NULL;
    akvcam_format_t akformat = NULL;

    for (;;) {
        akformat = akvcam_list_next(formats, &element);

        if (!element)
            break;

        if (format->fmt.pix.width == akvcam_format_width(akformat)
            && format->fmt.pix.height == akvcam_format_height(akformat)
            && format->fmt.pix.pixelformat == akvcam_format_fourcc(akformat)
            && format->fmt.pix.field == V4L2_FIELD_NONE
            && format->fmt.pix.bytesperline == (__u32) akvcam_format_bypl(akformat)
            && format->fmt.pix.sizeimage == (__u32) akvcam_format_size(akformat)
            && format->fmt.pix.colorspace == DEFAULT_COLORSPACE) {
            return akformat;
        }
    }

    return NULL;
}

akvcam_format_t akvcam_format_from_v4l2(struct akvcam_list *formats,
                                        const struct v4l2_format *format)
{
    akvcam_format_t akformat =
            akvcam_format_from_v4l2_nr(formats, format);

    if (!akformat)
        return NULL;

    akvcam_object_ref(AKVCAM_TO_OBJECT(akformat));

    return akformat;
}
