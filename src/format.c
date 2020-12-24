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
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/videodev2.h>

#include "format.h"
#include "list.h"

#define DEFAULT_COLORSPACE V4L2_COLORSPACE_RAW

struct akvcam_format
{
    struct kref ref;
    __u32 fourcc;
    size_t width;
    size_t height;
    struct v4l2_fract frame_rate;
    char str[1024];
};

typedef size_t (*akvcam_plane_offset_t)(size_t plane, size_t width, size_t height);
typedef size_t (*akvcam_bypl_t)(size_t plane, size_t width);

typedef struct {
    __u32 fourcc;
    size_t bpp;
    size_t planes;
    akvcam_plane_offset_t plane_offset;
    akvcam_bypl_t bypl;
    char str[32];
} akvcam_format_globals, *akvcam_format_globals_t;
typedef const akvcam_format_globals *akvcam_format_globals_ct;

size_t akvcam_po_nv(size_t plane, size_t width, size_t height);
size_t akvcam_bypl_nv(size_t plane, size_t width);

// Multiplanar formats are not supported at all yet.
static const akvcam_format_globals akvcam_format_globals_formats[] = {
    {V4L2_PIX_FMT_RGB32 , 32, 1,         NULL,           NULL, "RGB32"},
    {V4L2_PIX_FMT_RGB24 , 24, 1,         NULL,           NULL, "RGB24"},
    {V4L2_PIX_FMT_RGB565, 16, 1,         NULL,           NULL, "RGB16"},
    {V4L2_PIX_FMT_RGB555, 16, 1,         NULL,           NULL, "RGB15"},
    {V4L2_PIX_FMT_BGR32 , 32, 1,         NULL,           NULL, "BGR32"},
    {V4L2_PIX_FMT_BGR24 , 24, 1,         NULL,           NULL, "BGR24"},
    {V4L2_PIX_FMT_UYVY  , 16, 1,         NULL,           NULL, "UYVY" },
    {V4L2_PIX_FMT_YUYV  , 16, 1,         NULL,           NULL, "YUY2" },
//    {V4L2_PIX_FMT_NV12  , 12, 2, akvcam_po_nv, akvcam_bypl_nv, "NV12" },
//    {V4L2_PIX_FMT_NV21  , 12, 2, akvcam_po_nv, akvcam_bypl_nv, "NV21" },
    {0                  ,  0, 0,         NULL,           NULL, ""     }
};

size_t akvcam_formats_count(void);
akvcam_format_globals_ct akvcam_format_globals_by_fourcc(__u32 fourcc);
akvcam_format_globals_ct akvcam_format_globals_by_str(const char *str);

akvcam_format_t akvcam_format_new(__u32 fourcc,
                                  size_t width,
                                  size_t height,
                                  const struct v4l2_fract *frame_rate)
{
    akvcam_format_t self = kzalloc(sizeof(struct akvcam_format), GFP_KERNEL);
    kref_init(&self->ref);
    self->fourcc = fourcc;
    self->width = width;
    self->height = height;

    if (frame_rate)
        memcpy(&self->frame_rate, frame_rate, sizeof(struct v4l2_fract));
    else
        memset(&self->frame_rate, 0, sizeof(struct v4l2_fract));

    return self;
}

akvcam_format_t akvcam_format_new_copy(akvcam_format_ct other)
{
    akvcam_format_t self = kzalloc(sizeof(struct akvcam_format), GFP_KERNEL);
    kref_init(&self->ref);
    self->fourcc = other->fourcc;
    self->width = other->width;
    self->height = other->height;
    memcpy(&self->frame_rate, &other->frame_rate, sizeof(struct v4l2_fract));

    return self;
}

static void akvcam_format_free(struct kref *ref)
{
    akvcam_format_t self = container_of(ref, struct akvcam_format, ref);
    kfree(self);
}

void akvcam_format_delete(akvcam_format_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_format_free);
}

akvcam_format_t akvcam_format_ref(akvcam_format_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_format_copy(akvcam_format_t self, akvcam_format_ct other)
{
    if (!self)
        return;

    if (other) {
        self->fourcc = other->fourcc;
        self->width = other->width;
        self->height = other->height;
        memcpy(&self->frame_rate, &other->frame_rate, sizeof(struct v4l2_fract));
    } else {
        self->fourcc = 0;
        self->width = 0;
        self->height = 0;
        memset(&self->frame_rate, 0, sizeof(struct v4l2_fract));
    }
}

__u32 akvcam_format_fourcc(akvcam_format_ct self)
{
    return self->fourcc;
}

void akvcam_format_set_fourcc(akvcam_format_t self, __u32 fourcc)
{
    self->fourcc = fourcc;
}

const char *akvcam_format_fourcc_str(akvcam_format_ct self)
{
    return akvcam_format_string_from_fourcc(self->fourcc);
}

void akvcam_format_set_fourcc_str(akvcam_format_t self, const char *fourcc)
{
    self->fourcc = akvcam_format_fourcc_from_string(fourcc);
}

size_t akvcam_format_width(akvcam_format_ct self)
{
    return self->width;
}

void akvcam_format_set_width(akvcam_format_t self, size_t width)
{
    self->width = width;
}

size_t akvcam_format_height(akvcam_format_ct self)
{
    return self->height;
}

void akvcam_format_set_height(akvcam_format_t self, size_t height)
{
    self->height = height;
}

struct v4l2_fract akvcam_format_frame_rate(akvcam_format_ct self)
{
    return self->frame_rate;
}

void akvcam_format_set_frame_rate(akvcam_format_t self, const struct v4l2_fract frame_rate)
{
    memcpy(&self->frame_rate, &frame_rate, sizeof(struct v4l2_fract));
}

size_t akvcam_format_bpp(akvcam_format_ct self)
{
    akvcam_format_globals_ct vf = akvcam_format_globals_by_fourcc(self->fourcc);

    return vf? vf->bpp: 0;
}

size_t akvcam_format_bypl(akvcam_format_ct self, size_t plane)
{
    akvcam_format_globals_ct vf = akvcam_format_globals_by_fourcc(self->fourcc);

    if (!vf)
        return 0;

    if (vf->bypl)
        return vf->bypl(plane, self->width);

    return (size_t) akvcam_align32((ssize_t) (self->width * vf->bpp)) / 8;
}

size_t akvcam_format_size(akvcam_format_ct self)
{
    akvcam_format_globals_ct vf;

    if (!self)
        return 0;

    vf = akvcam_format_globals_by_fourcc(self->fourcc);

    if (!vf)
        return 0;

    if (vf->plane_offset)
        return vf->plane_offset(vf->planes, self->width, self->height);

    return self->height
           * (size_t) akvcam_align32((ssize_t) (self->width * vf->bpp)) / 8;
}

size_t akvcam_format_planes(akvcam_format_ct self)
{
    akvcam_format_globals_ct vf = akvcam_format_globals_by_fourcc(self->fourcc);

    return vf? vf->planes: 0;
}

size_t akvcam_format_offset(akvcam_format_ct self, size_t plane)
{
    akvcam_format_globals_ct vf = akvcam_format_globals_by_fourcc(self->fourcc);

    if (!vf)
        return 0;

    if (vf->plane_offset)
        return vf->plane_offset(plane, self->width, self->height);

    return 0;
}

size_t akvcam_format_plane_size(akvcam_format_ct self, size_t plane)
{
    return self->height * akvcam_format_bypl(self, plane);
}

bool akvcam_format_is_valid(akvcam_format_ct self)
{
    return akvcam_format_size(self) > 0
            && self->frame_rate.numerator != 0
            && self->frame_rate.denominator != 0
            && self->frame_rate.numerator / self->frame_rate.denominator > 0;
}

void akvcam_format_clear(akvcam_format_t self)
{
    self->fourcc = 0;
    self->width = 0;
    self->height = 0;
    self->frame_rate.numerator = 0;
    self->frame_rate.denominator = 0;
}

const char *akvcam_format_to_string(akvcam_format_t self)
{
    memset(self->str, 0, 1024);
    snprintf(self->str,
             1024,
             "%s %zux%zu %u/%u Hz",
             akvcam_format_string_from_fourcc(self->fourcc),
             self->width,
             self->height,
             self->frame_rate.numerator,
             self->frame_rate.denominator);

    return self->str;
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
    size_t i;

    for (i = 0; i < akvcam_formats_count(); i++)
        if (strcasecmp(akvcam_format_globals_formats[i].str, fourcc_str) == 0)
            return (akvcam_format_globals_formats + i)->fourcc;

    return 0;
}

const char *akvcam_format_string_from_fourcc(__u32 fourcc)
{
    akvcam_format_globals_ct vf = akvcam_format_globals_by_fourcc(fourcc);

    return vf? vf->str: NULL;
}

akvcam_format_t akvcam_format_nearest(akvcam_formats_list_ct formats,
                                      akvcam_format_ct format)
{
    akvcam_list_element_t element = NULL;
    akvcam_format_t nearest_format = NULL;
    size_t s;

    memset(&s, 0xff, sizeof(size_t));

    for (;;) {
        ssize_t diff_fourcc;
        ssize_t diff_width;
        ssize_t diff_height;
        ssize_t diff_fps;
        size_t r;
        akvcam_format_t temp_format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        diff_fourcc = (ssize_t) akvcam_format_fourcc(temp_format)
                    - (ssize_t) akvcam_format_fourcc(format);
        diff_width = (ssize_t) akvcam_format_width(temp_format)
                   - (ssize_t) akvcam_format_width(format);
        diff_height = (ssize_t) akvcam_format_height(temp_format)
                    - (ssize_t) akvcam_format_height(format);
        diff_fps = (ssize_t) (akvcam_format_frame_rate(temp_format).numerator
                              * akvcam_format_frame_rate(format).denominator)
                 - (ssize_t) (akvcam_format_frame_rate(format).numerator
                              * akvcam_format_frame_rate(temp_format).denominator);

        r = (size_t) (diff_fourcc * diff_fourcc)
          + (size_t) (diff_width * diff_width)
          + (size_t) (diff_height * diff_height)
          + (size_t) (diff_fps * diff_fps);

        if (r < s) {
            s = r;
            nearest_format = temp_format;
        }
    }

    return akvcam_format_new_copy(nearest_format);
}

static bool akvcam_format_fourcc_are_equal(__u32 *fourcc1, __u32 *fourcc2)
{
    return *fourcc1 == *fourcc2;
}

static __u32 *akvcam_format_fourcc_copy(__u32 *fourcc)
{
    return kmemdup(fourcc, sizeof(__u32), GFP_KERNEL);
}

akvcam_pixel_formats_list_t akvcam_format_pixel_formats(akvcam_formats_list_ct formats)
{
    akvcam_list_element_t element = NULL;
    akvcam_pixel_formats_list_t supported_formats = akvcam_list_new();

    if (!formats)
        return supported_formats;

    for (;;) {
        __u32 fourcc;
        akvcam_list_element_t it;
        akvcam_format_t format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        fourcc = akvcam_format_fourcc(format);
        it = akvcam_list_find(supported_formats,
                              &fourcc,
                              (akvcam_are_equals_t) akvcam_format_fourcc_are_equal);

        if (!it)
            akvcam_list_push_back(supported_formats,
                                  &fourcc,
                                  (akvcam_copy_t) akvcam_format_fourcc_copy,
                                  (akvcam_delete_t) kfree);
    }

    return supported_formats;
}

static bool akvcam_format_resolutions_are_equal(struct v4l2_frmsize_discrete *resolution1,
                                                struct v4l2_frmsize_discrete *resolution2)
{
    return !memcmp(resolution1,
                   resolution2,
                   sizeof(struct v4l2_frmsize_discrete));
}

static struct v4l2_frmsize_discrete *akvcam_format_resolution_copy(const struct v4l2_frmsize_discrete *resolution)
{
    return kmemdup(resolution, sizeof(struct v4l2_frmsize_discrete), GFP_KERNEL);
}

akvcam_resolutions_list_t akvcam_format_resolutions(akvcam_formats_list_t formats,
                                                    __u32 fourcc)
{
    akvcam_list_element_t element = NULL;
    akvcam_list_element_t it;
    akvcam_resolutions_list_t supported_resolutions = akvcam_list_new();

    for (;;) {
        struct v4l2_frmsize_discrete resolution;
        akvcam_format_t format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        if (akvcam_format_fourcc(format) != fourcc)
            continue;

        resolution.width = (__u32) akvcam_format_width(format);
        resolution.height = (__u32) akvcam_format_height(format);
        it = akvcam_list_find(supported_resolutions,
                              &resolution,
                              (akvcam_are_equals_t) akvcam_format_resolutions_are_equal);

        if (!it)
            akvcam_list_push_back(supported_resolutions,
                                  &resolution,
                                  (akvcam_copy_t) akvcam_format_resolution_copy,
                                  (akvcam_delete_t) kfree);
    }

    return supported_resolutions;
}

static bool akvcam_format_frame_rates_are_equal(struct v4l2_fract *fps1,
                                                struct v4l2_fract *fps2)
{
    return !memcmp(fps1, fps2, sizeof(struct v4l2_fract));
}

static struct v4l2_fract *akvcam_format_frame_rate_copy(struct v4l2_fract *fps)
{
    return kmemdup(fps, sizeof(struct v4l2_fract), GFP_KERNEL);
}

akvcam_fps_list_t akvcam_format_frame_rates(akvcam_formats_list_ct formats,
                                            __u32 fourcc,
                                            size_t width,
                                            size_t height)
{
    akvcam_list_element_t element = NULL;
    akvcam_list_element_t it;
    akvcam_fps_list_t supported_frame_rates = akvcam_list_new();

    for (;;) {
        struct v4l2_fract frame_rate;
        akvcam_format_t format = akvcam_list_next(formats, &element);

        if (!element)
            break;

        if (akvcam_format_fourcc(format) != fourcc
            || akvcam_format_width(format) != width
            || akvcam_format_height(format) != height)
            continue;

        frame_rate = akvcam_format_frame_rate(format);
        it = akvcam_list_find(supported_frame_rates,
                              &frame_rate,
                              (akvcam_are_equals_t) akvcam_format_frame_rates_are_equal);

        if (!it)
            akvcam_list_push_back(supported_frame_rates,
                                  &frame_rate,
                                  (akvcam_copy_t) akvcam_format_frame_rate_copy,
                                  (akvcam_delete_t) kfree);
    }

    return supported_frame_rates;
}

akvcam_format_ct akvcam_format_from_v4l2_nr(akvcam_formats_list_ct formats,
                                            const struct v4l2_format *format)
{
    akvcam_list_element_t element = NULL;
    size_t i;

    for (;;) {
        akvcam_format_t akformat = akvcam_list_next(formats, &element);

        if (!element)
            break;

        if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE
            || format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
            if (format->fmt.pix.width == akvcam_format_width(akformat)
                && format->fmt.pix.height == akvcam_format_height(akformat)
                && format->fmt.pix.pixelformat == akvcam_format_fourcc(akformat)
                && format->fmt.pix.field == V4L2_FIELD_NONE
                && format->fmt.pix.bytesperline == (__u32) akvcam_format_bypl(akformat, 0)
                && format->fmt.pix.colorspace == DEFAULT_COLORSPACE) {
                return akformat;
            }
        } else if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                   || format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
            if (format->fmt.pix_mp.width == akvcam_format_width(akformat)
                && format->fmt.pix_mp.height == akvcam_format_height(akformat)
                && format->fmt.pix_mp.pixelformat == akvcam_format_fourcc(akformat)
                && format->fmt.pix_mp.field == V4L2_FIELD_NONE
                && format->fmt.pix_mp.colorspace == DEFAULT_COLORSPACE
                && format->fmt.pix_mp.num_planes == akvcam_format_planes(akformat)) {
                bool is_valid = true;

                for (i = 0; i < format->fmt.pix_mp.num_planes; i++) {
                    size_t bypl = akvcam_format_bypl(akformat, i);

                    if (format->fmt.pix_mp.plane_fmt[i].bytesperline != bypl) {
                        is_valid = false;

                        break;
                    }
                }

                if (is_valid)
                    return akformat;
            }
        }
    }

    return NULL;
}

akvcam_format_t akvcam_format_from_v4l2(akvcam_formats_list_ct formats,
                                        const struct v4l2_format *format)
{
    akvcam_format_ct akformat = akvcam_format_from_v4l2_nr(formats, format);

    if (!akformat)
        return NULL;

    return akvcam_format_new_copy(akformat);
}

bool akvcam_format_have_multiplanar(akvcam_formats_list_ct formats)
{
    akvcam_list_element_t it = NULL;

    for (;;) {
        akvcam_format_globals_ct vf;
        akvcam_format_t format = akvcam_list_next(formats, &it);

        if (!it)
            break;

        vf = akvcam_format_globals_by_fourcc(format->fourcc);

        if (!vf)
            continue;

        if (vf->planes > 1)
            return true;
    }

    return false;
}

size_t akvcam_formats_count(void)
{
    static size_t count = 0;

    if (count < 1) {
        size_t i;

        for (i = 0; akvcam_format_globals_formats[i].fourcc; i++)
            count++;
    }

    return count;
}

akvcam_format_globals_ct akvcam_format_globals_by_fourcc(__u32 fourcc)
{
    size_t i;

    for (i = 0; i < akvcam_formats_count(); i++)
        if (akvcam_format_globals_formats[i].fourcc == fourcc)
            return akvcam_format_globals_formats + i;

    return NULL;
}

akvcam_format_globals_ct akvcam_format_globals_by_str(const char *str)
{
    size_t i;

    for (i = 0; i < akvcam_formats_count(); i++)
        if (strcmp(akvcam_format_globals_formats[i].str, str) == 0)
            return akvcam_format_globals_formats + i;

    return NULL;
}

size_t akvcam_po_nv(size_t plane, size_t width, size_t height)
{
    size_t offset[] = {
        0,
        (size_t) akvcam_align32((ssize_t) width) * height,
        5 * (size_t) akvcam_align32((ssize_t) width) * height / 4
    };

    return offset[plane];
}

size_t akvcam_bypl_nv(size_t plane, size_t width)
{
    UNUSED(plane);

    return (size_t) akvcam_align32((ssize_t) width);
}
