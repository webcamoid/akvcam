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
#include "format_specs.h"
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
    size_t data_size;
    size_t nplanes;
    size_t bpp;
    size_t plane_size[MAX_PLANES];
    size_t plane_offset[MAX_PLANES];
    size_t pixel_size[MAX_PLANES];
    size_t line_size[MAX_PLANES];
    size_t bytes_used[MAX_PLANES];
    size_t width_div[MAX_PLANES];
    size_t height_div[MAX_PLANES];
    size_t align;
};

static void akvcam_format_private_update_params(akvcam_format_t self,
                                                akvcam_format_specs_ct specs);

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
    akvcam_format_specs_ct specs =
            akvcam_format_specs_from_fixel_format(fourcc);
    self->nplanes = specs? specs->nplanes: 0;
    self->align = 32;
    akvcam_format_private_update_params(self, specs);

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
    memcpy(&self->str, &other->str, 1024);
    self->data_size = other->data_size;
    self->nplanes = other->nplanes;
    self->bpp = other->bpp;
    self->align = other->align;

    size_t mem_size = MAX_PLANES * sizeof(size_t);
    memcpy(self->plane_size, other->plane_size, mem_size);
    memcpy(self->plane_offset, other->plane_offset, mem_size);
    memcpy(self->pixel_size, other->pixel_size, mem_size);
    memcpy(self->line_size, other->line_size, mem_size);
    memcpy(self->bytes_used, other->bytes_used, mem_size);
    memcpy(self->width_div, other->width_div, mem_size);
    memcpy(self->height_div, other->height_div, mem_size);

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
        self->data_size = other->data_size;
        self->nplanes = other->nplanes;
        self->bpp = other->bpp;
        self->align = other->align;

        size_t mem_size = MAX_PLANES * sizeof(size_t);
        memcpy(self->plane_size, other->plane_size, mem_size);
        memcpy(self->plane_offset, other->plane_offset, mem_size);
        memcpy(self->pixel_size, other->pixel_size, mem_size);
        memcpy(self->line_size, other->line_size, mem_size);
        memcpy(self->bytes_used, other->bytes_used, mem_size);
        memcpy(self->width_div, other->width_div, mem_size);
        memcpy(self->height_div, other->height_div, mem_size);
    } else {
        self->fourcc = 0;
        self->width = 0;
        self->height = 0;
        memset(&self->frame_rate, 0, sizeof(struct v4l2_fract));
        self->data_size = 0;
        self->nplanes = 0;
        self->bpp = 0;
        self->align = 32;

        size_t mem_size = MAX_PLANES * sizeof(size_t);
        memset(self->plane_size, 0, mem_size);
        memset(self->plane_offset, 0, mem_size);
        memset(self->pixel_size, 0, mem_size);
        memset(self->line_size, 0, mem_size);
        memset(self->bytes_used, 0, mem_size);
        memset(self->width_div, 0, mem_size);
        memset(self->height_div, 0, mem_size);
    }
}

__u32 akvcam_format_fourcc(akvcam_format_ct self)
{
    return self->fourcc;
}

size_t akvcam_format_width(akvcam_format_ct self)
{
    return self->width;
}

size_t akvcam_format_height(akvcam_format_ct self)
{
    return self->height;
}

struct v4l2_fract akvcam_format_frame_rate(akvcam_format_ct self)
{
    return self->frame_rate;
}

size_t akvcam_format_size(akvcam_format_ct self)
{
    return self->data_size;
}

size_t akvcam_format_planes(akvcam_format_ct self)
{
    return self->nplanes;
}

size_t akvcam_format_bpp(akvcam_format_ct self)
{
    return self->bpp;
}

size_t akvcam_format_plane_size(akvcam_format_ct self, size_t plane)
{
    return plane < self->nplanes? self->plane_size[plane]: 0;
}

size_t akvcam_format_pixel_size(akvcam_format_ct self, size_t plane)
{
    return plane < self->nplanes? self->pixel_size[plane]: 0;
}

size_t akvcam_format_line_size(akvcam_format_ct self, size_t plane)
{
    return plane < self->nplanes? self->line_size[plane]: 0;
}

size_t akvcam_format_offset(akvcam_format_ct self, size_t plane)
{
    return plane < self->nplanes? self->plane_offset[plane]: 0;
}

size_t akvcam_format_bytes_used(akvcam_format_ct self, size_t plane)
{
    return plane < self->nplanes? self->bytes_used[plane]: 0;
}

size_t akvcam_format_width_div(akvcam_format_ct self, size_t plane)
{
    return plane < self->nplanes? self->width_div[plane]: 0;
}

size_t akvcam_format_height_div(akvcam_format_ct self, size_t plane)
{
    return plane < self->nplanes? self->height_div[plane]: 0;
}

bool akvcam_format_is_valid(akvcam_format_ct self)
{
    return akvcam_format_size(self) > 0
            && self->frame_rate.numerator > 0
            && self->frame_rate.denominator > 0;
}

bool akvcam_format_is_same_format(akvcam_format_ct self, akvcam_format_ct other)
{
    return self->fourcc == other->fourcc
            && self->width == other->width
            && self->height == other->height;
}

const char *akvcam_format_to_string(akvcam_format_t self)
{
    memset(self->str, 0, 1024);
    snprintf(self->str,
             1024,
             "%s %zux%zu %u/%u Hz",
             akvcam_string_from_fourcc(self->fourcc),
             self->width,
             self->height,
             self->frame_rate.numerator,
             self->frame_rate.denominator);

    return self->str;
}

static bool akvcam_format_fourcc_are_equal(__u32 *fourcc1, __u32 *fourcc2)
{
    return *fourcc1 == *fourcc2;
}

static __u32 *akvcam_format_fourcc_copy(__u32 *fourcc)
{
    return kmemdup(fourcc, sizeof(__u32), GFP_KERNEL);
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

        diff_fourcc = temp_format->fourcc != format->fourcc?
                          1:
                          0;
        diff_width = (ssize_t) temp_format->width - (ssize_t) format->width;
        diff_height = (ssize_t) temp_format->height - (ssize_t) format->height;
        diff_fps = (ssize_t) temp_format->frame_rate.numerator
                             * format->frame_rate.denominator
                 - (ssize_t) format->frame_rate.numerator
                             * temp_format->frame_rate.denominator;

        r = (size_t) (diff_fourcc * diff_fourcc)
          + (size_t) (diff_width * diff_width)
          + (size_t) (diff_height * diff_height)
          + (size_t) (diff_fps * diff_fps);

        if (r < s) {
            s = r;
            nearest_format = temp_format;
        }
    }

    if (!nearest_format)
        return NULL;

    return akvcam_format_new_copy(nearest_format);
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
                && format->fmt.pix.bytesperline == (__u32) akvcam_format_line_size(akformat, 0)
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
                    size_t bypl = akvcam_format_line_size(akformat, i);

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
        akvcam_format_t format = akvcam_list_next(formats, &it);

        if (!it)
            break;

        if (format->nplanes > 1)
            return true;
    }

    return false;
}

static void akvcam_format_private_update_params(akvcam_format_t self,
                                                akvcam_format_specs_ct specs)
{
    self->data_size = 0;
    self->bpp = 0;

    if (!specs) {
        size_t mem_size = MAX_PLANES * sizeof(size_t);
        memset(self->plane_size, 0, mem_size);
        memset(self->plane_offset, 0, mem_size);
        memset(self->pixel_size, 0, mem_size);
        memset(self->line_size, 0, mem_size);
        memset(self->bytes_used, 0, mem_size);
        memset(self->width_div, 0, mem_size);
        memset(self->height_div, 0, mem_size);

        return;
    }

    // Calculate parameters for each plane
    for (size_t i = 0; i < specs->nplanes; ++i) {
        akvcam_plane_ct plane = specs->planes + i;
        size_t pixel_size = akvcam_plane_pixel_size(plane);
        size_t width_div = akvcam_plane_width_div(plane);
        size_t height_div = akvcam_plane_height_div(plane);

        // Calculate bytes used per line (bits per pixel * width / 8)
        size_t bytes_used = plane->bits_size * (self->width >> width_div) / 8;

        // Align line size for SIMD compatibility
        size_t line_size = akvcam_align_up(bytes_used, (size_t)self->align);

        // Store pixel size, line size, and bytes used
        self->pixel_size[i] = pixel_size;
        self->line_size[i] = line_size;
        self->bytes_used[i] = bytes_used;

        // Calculate plane size, considering sub-sampling
        size_t plane_size = (line_size * self->height) >> height_div;

        // Align plane size to ensure next plane starts aligned
        plane_size = akvcam_align_up(plane_size, (size_t)self->align);

        // Store plane size and offset
        self->plane_size[i] = plane_size;
        self->plane_offset[i] = self->data_size;

        // Update total data size
        self->data_size += plane_size;

        // Store width and height divisors for sub-sampling
        self->width_div[i] = width_div;
        self->height_div[i] = height_div;

        // Calculate pixel depth
        self->bpp += plane->bits_size;
    }

    // Align total data size for buffer allocation
    self->data_size = akvcam_align_up(self->data_size, (size_t)self->align);
}
