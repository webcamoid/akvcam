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
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "frame.h"
#include "file_read.h"
#include "format.h"
#include "global_deleter.h"
#include "log.h"
#include "utils.h"

// FIXME: This is endianness dependent.

typedef struct
{
    uint8_t x;
    uint8_t b;
    uint8_t g;
    uint8_t r;
} akvcam_RGB32, *akvcam_RGB32_t;

typedef struct
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
} akvcam_RGB24, *akvcam_RGB24_t;

typedef struct
{
    uint16_t b: 5;
    uint16_t g: 6;
    uint16_t r: 5;
} akvcam_RGB16, *akvcam_RGB16_t;

typedef struct
{
    uint16_t b: 5;
    uint16_t g: 5;
    uint16_t r: 5;
    uint16_t x: 1;
} akvcam_RGB15, *akvcam_RGB15_t;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t x;
} akvcam_BGR32, *akvcam_BGR32_t;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} akvcam_BGR24, *akvcam_BGR24_t;

typedef struct
{
    uint16_t r: 5;
    uint16_t g: 6;
    uint16_t b: 5;
} akvcam_BGR16, *akvcam_BGR16_t;

typedef struct
{
    uint8_t v0;
    uint8_t y0;
    uint8_t u0;
    uint8_t y1;
} akvcam_UYVY, *akvcam_UYVY_t;

typedef struct
{
    uint8_t y0;
    uint8_t v0;
    uint8_t y1;
    uint8_t u0;
} akvcam_YUY2, *akvcam_YUY2_t;

typedef struct
{
    uint8_t u;
    uint8_t v;
} akvcam_UV, *akvcam_UV_t;

typedef struct
{
    uint8_t v;
    uint8_t u;
} akvcam_VU, *akvcam_VU_t;

typedef struct
{
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offBits;
} akvcam_bmp_header, *akvcam_bmp_header_t;

typedef struct
{
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t compression;
    uint32_t sizeImage;
    uint32_t xPelsPerMeter;
    uint32_t yPelsPerMeter;
    uint32_t clrUsed;
    uint32_t clrImportant;
} akvcam_bmp_image_header, *akvcam_bmp_image_header_t;

typedef struct
{
    AKVCAM_SCALING scaling;
    char  str[32];
} akvcam_frame_scaling_strings, *akvcam_frame_scaling_strings_t;

typedef struct
{
    AKVCAM_ASPECT_RATIO aspect_ratio;
    char  str[32];
} akvcam_frame_aspect_ratio_strings, *akvcam_frame_aspect_ratio_strings_t;

typedef void (*akvcam_extrapolate_t)(size_t dstCoord,
                                     size_t num, size_t den, size_t s,
                                     size_t *srcCoordMin, size_t *srcCoordMax,
                                     size_t *kNum, size_t *kDen);
int akvcam_grayval(int r, int g, int b);

// YUV utility functions
uint8_t akvcam_rgb_y(int r, int g, int b);
uint8_t akvcam_rgb_u(int r, int g, int b);
uint8_t akvcam_rgb_v(int r, int g, int b);
uint8_t akvcam_yuv_r(int y, int u, int v);
uint8_t akvcam_yuv_g(int y, int u, int v);
uint8_t akvcam_yuv_b(int y, int u, int v);

// BGR to RGB formats
void akvcam_bgr24_to_rgb32(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_bgr24_to_rgb24(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_bgr24_to_rgb16(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_bgr24_to_rgb15(akvcam_frame_t dst, akvcam_frame_ct src);

// BGR to BGR formats
void akvcam_bgr24_to_bgr32(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_bgr24_to_bgr16(akvcam_frame_t dst, akvcam_frame_ct src);

// BGR to Luminance+Chrominance formats
void akvcam_bgr24_to_uyvy(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_bgr24_to_yuy2(akvcam_frame_t dst, akvcam_frame_ct src);

// BGR to two planes -- one Y, one Cr + Cb interleaved
void akvcam_bgr24_to_nv12(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_bgr24_to_nv21(akvcam_frame_t dst, akvcam_frame_ct src);

// RGB to RGB formats
void akvcam_rgb24_to_rgb32(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_rgb24_to_rgb16(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_rgb24_to_rgb15(akvcam_frame_t dst, akvcam_frame_ct src);

// RGB to BGR formats
void akvcam_rgb24_to_bgr32(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_rgb24_to_bgr24(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_rgb24_to_bgr16(akvcam_frame_t dst, akvcam_frame_ct src);

// RGB to Luminance+Chrominance formats
void akvcam_rgb24_to_uyvy(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_rgb24_to_yuy2(akvcam_frame_t dst, akvcam_frame_ct src);

// RGB to two planes -- one Y, one Cr + Cb interleaved
void akvcam_rgb24_to_nv12(akvcam_frame_t dst, akvcam_frame_ct src);
void akvcam_rgb24_to_nv21(akvcam_frame_t dst, akvcam_frame_ct src);

void akvcam_extrapolate_up(size_t dstCoord,
                           size_t num, size_t den, size_t s,
                           size_t *src_coord_min, size_t *src_coord_max,
                           size_t *k_num, size_t *k_den);
void akvcam_extrapolate_down(size_t dst_coord,
                             size_t num, size_t den, size_t s,
                             size_t *src_coord_min, size_t *src_coord_max,
                             size_t *k_num, size_t *k_den);
uint8_t akvcam_extrapolate_component(uint8_t min, uint8_t max,
                                     size_t k_num, size_t k_Den);
akvcam_RGB24 akvcam_extrapolate_color(const akvcam_RGB24_t color_min,
                                      const akvcam_RGB24_t color_max,
                                      size_t k_num,
                                      size_t k_den);
akvcam_RGB24 akvcam_extrapolated_color(akvcam_frame_t self,
                                       size_t x_min, size_t x_max,
                                       size_t k_num_x, size_t k_den_x,
                                       size_t y_min, size_t y_max,
                                       size_t k_num_y, size_t k_den_y);
void akvcam_rgb_to_hsl(int r, int g, int b, int *h, int *s, int *l);
void akvcam_hsl_to_rgb(int h, int s, int l, int *r, int *g, int *b);

typedef void (*akvcam_video_convert_funtion_t)(akvcam_frame_t dst,
                                               akvcam_frame_ct src);

typedef struct
{
    __u32 from;
    __u32 to;
    akvcam_video_convert_funtion_t convert;
} akvcam_video_convert, *akvcam_video_convert_t;
typedef const akvcam_video_convert *akvcam_video_convert_ct;

static const akvcam_video_convert akvcam_frame_convert_table[] = {
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_RGB32 , akvcam_bgr24_to_rgb32},
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_RGB24 , akvcam_bgr24_to_rgb24},
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_RGB565, akvcam_bgr24_to_rgb16},
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_RGB555, akvcam_bgr24_to_rgb15},
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_BGR32 , akvcam_bgr24_to_bgr32},
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_UYVY  , akvcam_bgr24_to_uyvy },
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_YUYV  , akvcam_bgr24_to_yuy2 },
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_NV12  , akvcam_bgr24_to_nv12 },
    {V4L2_PIX_FMT_BGR24, V4L2_PIX_FMT_NV21  , akvcam_bgr24_to_nv21 },

    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_RGB32 , akvcam_rgb24_to_rgb32},
    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_RGB565, akvcam_rgb24_to_rgb16},
    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_BGR32 , akvcam_rgb24_to_bgr32},
    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_BGR24 , akvcam_rgb24_to_bgr24},
    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_UYVY  , akvcam_rgb24_to_uyvy },
    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_YUYV  , akvcam_rgb24_to_yuy2 },
    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_NV12  , akvcam_rgb24_to_nv12 },
    {V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_NV21  , akvcam_rgb24_to_nv21 },
    {0                 , 0                  , NULL                 }
};

size_t akvcam_convert_funcs_count(void);
akvcam_video_convert_funtion_t akvcam_convert_func(__u32 from, __u32 to);

struct akvcam_frame
{
    struct kref ref;
    akvcam_format_t format;
    void *data;
    size_t size;
};

bool akvcam_frame_adjust_format_supported(__u32 fourcc);
const uint8_t *akvcam_contrast_table(void);
const uint8_t *akvcam_gamma_table(void);

akvcam_frame_t akvcam_frame_new(akvcam_format_t format,
                                const void *data,
                                size_t size)
{
    akvcam_frame_t self = kzalloc(sizeof(struct akvcam_frame), GFP_KERNEL);
    kref_init(&self->ref);
    self->format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(self->format, format);

    if (size < 1)
        size = akvcam_format_size(format);

    self->size = size;

    if (size > 0) {
        self->data = vzalloc(size);

        if (data)
            memcpy(self->data, data, size);
    }

    return self;
}

akvcam_frame_t akvcam_frame_new_copy(akvcam_frame_ct other)
{
    akvcam_frame_t self = kzalloc(sizeof(struct akvcam_frame), GFP_KERNEL);
    kref_init(&self->ref);
    self->format = akvcam_format_new_copy(other->format);
    self->size = other->size;

    if (self->size > 0) {
        self->data = vzalloc(self->size);

        if (self->data)
            memcpy(self->data, other->data, self->size);
    }

    return self;
}

static void akvcam_frame_free(struct kref *ref)
{
    akvcam_frame_t self = container_of(ref, struct akvcam_frame, ref);

    if (self->data)
        vfree(self->data);

    akvcam_format_delete(self->format);
    kfree(self);
}

void akvcam_frame_delete(akvcam_frame_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_frame_free);
}

akvcam_frame_t akvcam_frame_ref(akvcam_frame_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_frame_copy(akvcam_frame_t self, akvcam_frame_ct other)
{
    akvcam_format_copy(self->format, other->format);
    self->size = other->size;

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    if (self->size > 0) {
        self->data = vzalloc(self->size);

        if (other->data)
            memcpy(self->data, other->data, other->size);
    }
}

akvcam_format_t akvcam_frame_format(akvcam_frame_ct self)
{
    return akvcam_format_new_copy(self->format);
}

void *akvcam_frame_data(akvcam_frame_ct self)
{
    return self->data;
}

void *akvcam_frame_line(akvcam_frame_ct self, size_t plane, size_t y)
{
    return (char *) self->data
            + akvcam_format_offset(self->format, plane)
            + y * akvcam_format_bypl(self->format, plane);
}

const void *akvcam_frame_const_line(akvcam_frame_ct self, size_t plane, size_t y)
{
    return akvcam_frame_line(self, plane, y);
}

size_t akvcam_frame_size(akvcam_frame_ct self)
{
    return self->size;
}

void akvcam_frame_resize(akvcam_frame_t self, size_t size)
{
    if (size < 1)
        size = akvcam_format_size(self->format);

    self->size = size;

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    if (size > 0)
        self->data = vzalloc(size);
}

void akvcam_frame_clear(akvcam_frame_t self)
{
    akvcam_format_clear(self->format);

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    self->size = 0;
}

bool akvcam_frame_load(akvcam_frame_t self, const char *file_name)
{
    akvcam_file_t bmp_file;
    char type[2];
    akvcam_bmp_header header;
    akvcam_bmp_image_header image_header;
    akvcam_RGB24_t line;
    akvcam_BGR24 pixel24;
    akvcam_BGR32 pixel32;
    uint32_t x;
    uint32_t y;

    akvcam_frame_clear(self);

    if (!file_name
        || strnlen(file_name, AKVCAM_MAX_STRING_SIZE) < 1) {
        akpr_err("Bitmap file name not valid\n");

        return false;
    }

    bmp_file = akvcam_file_new(file_name);

    if (!akvcam_file_open(bmp_file)) {
        akpr_err("Can't open bitmap file: %s\n", file_name);

        goto akvcam_frame_load_failed;
    }

    akvcam_file_read(bmp_file, type, 2);

    if (memcmp(type, "BM", 2) != 0) {
        akpr_err("Invalid bitmap signature: %c%c\n", type[0], type[1]);

        goto akvcam_frame_load_failed;
    }

    akvcam_file_read(bmp_file,
                     (char *) &header,
                     sizeof(akvcam_bmp_header));
    akvcam_file_read(bmp_file,
                     (char *) &image_header,
                     sizeof(akvcam_bmp_image_header));
    akvcam_file_seek(bmp_file, header.offBits, AKVCAM_FILE_SEEK_BEG);
    akvcam_format_set_fourcc(self->format, V4L2_PIX_FMT_RGB24);
    akvcam_format_set_width(self->format, image_header.width);
    akvcam_format_set_height(self->format, image_header.height);
    self->size = akvcam_format_size(self->format);

    if (!self->size) {
        akpr_err("Bitmap format is invalid\n");

        goto akvcam_frame_load_failed;
    }

    self->data = vmalloc(self->size);

    switch (image_header.bitCount) {
        case 24:
            for (y = 0; y < image_header.height; y++) {
                line = akvcam_frame_line(self, 0, image_header.height - y - 1);

                for (x = 0; x < image_header.width; x++) {
                    akvcam_file_read(bmp_file,
                                     (char *) &pixel24,
                                     sizeof(akvcam_BGR24));
                    line[x].r = pixel24.r;
                    line[x].g = pixel24.g;
                    line[x].b = pixel24.b;
                }
            }

            break;

        case 32:
            for (y = 0; y < image_header.height; y++) {
                line = akvcam_frame_line(self, 0, image_header.height - y - 1);

                for (x = 0; x < image_header.width; x++) {
                    akvcam_file_read(bmp_file,
                                     (char *) &pixel32,
                                     sizeof(akvcam_BGR32));
                    line[x].r = pixel32.r;
                    line[x].g = pixel32.g;
                    line[x].b = pixel32.b;
                }
            }

            break;

        default:
            akpr_err("Bit count not supported in bitmap: %u\n",
                     image_header.bitCount);

            goto akvcam_frame_load_failed;
    }

    akvcam_file_delete(bmp_file);

    return true;

akvcam_frame_load_failed:
    akvcam_frame_clear(self);
    akvcam_file_delete(bmp_file);

    return false;
}

void akvcam_frame_mirror(akvcam_frame_t self,
                         bool horizontalMirror,
                         bool verticalMirror)
{
    __u32 fourcc;
    akvcam_RGB24_t src_line;
    akvcam_RGB24_t dst_line;
    akvcam_RGB24_t tmp_line;
    akvcam_RGB24 tmp_pixel;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    size_t line_size;

    if (!horizontalMirror && !verticalMirror)
        return;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return;

    width = akvcam_format_width(self->format);
    height = akvcam_format_height(self->format);

    if (horizontalMirror)
        for (y = 0; y < height; y++) {
            src_line = akvcam_frame_line(self, 0, y);

            for (x = 0; x < width / 2; x++) {
                tmp_pixel = src_line[x];
                src_line[x] = src_line[width - x - 1];
                src_line[width - x - 1] = tmp_pixel;
            }
        }

    if (verticalMirror) {
        line_size = akvcam_format_bypl(self->format, 0);
        tmp_line = vmalloc(line_size);

        for (y = 0; y < height / 2; y++) {
            src_line = akvcam_frame_line(self, 0, height - y - 1);
            dst_line = akvcam_frame_line(self, 0, y);
            memcpy(tmp_line, dst_line, line_size);
            memcpy(dst_line, src_line, line_size);
            memcpy(src_line, tmp_line, line_size);
        }

        vfree(tmp_line);
    }
}

bool akvcam_frame_scaled(akvcam_frame_t self,
                         size_t width,
                         size_t height,
                         AKVCAM_SCALING mode,
                         AKVCAM_ASPECT_RATIO aspectRatio)
{
    __u32 fourcc;
    akvcam_format_t format;
    akvcam_extrapolate_t extrapolate_x;
    akvcam_extrapolate_t extrapolate_y;
    akvcam_RGB24_t src_line;
    akvcam_RGB24_t dst_line;
    size_t x_dst_min;
    size_t y_dst_min;
    size_t x_dst_max;
    size_t y_dst_max;
    size_t i_width;
    size_t i_height;
    size_t o_width;
    size_t o_height;
    size_t x_num;
    size_t x_den;
    size_t xs;
    size_t y_num;
    size_t y_den;
    size_t ys;
    size_t x;
    size_t y;
    size_t x_min;
    size_t x_max;
    size_t k_num_x;
    size_t k_den_x;
    size_t y_min;
    size_t y_max;
    size_t k_num_y;
    size_t k_den_y;
    void *data;

    if (akvcam_format_width(self->format) == width
        && akvcam_format_height(self->format) == height)
        return true;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return false;

    x_dst_min = 0;
    y_dst_min = 0;
    x_dst_max = width;
    y_dst_max = height;

    if (aspectRatio == AKVCAM_ASPECT_RATIO_KEEP) {
        if (width * akvcam_format_height(self->format)
            > akvcam_format_width(self->format) * height) {
            // Right and left black bars
            x_dst_min = (width * akvcam_format_height(self->format)
                         - akvcam_format_width(self->format) * height)
                         / (2 * akvcam_format_height(self->format));
            x_dst_max = (width * akvcam_format_height(self->format)
                         + akvcam_format_width(self->format) * height)
                         / (2 * akvcam_format_height(self->format));
        } else if (width * akvcam_format_height(self->format)
                   < akvcam_format_width(self->format) * height) {
            // Top and bottom black bars
            y_dst_min = (akvcam_format_width(self->format) * height
                         - width * akvcam_format_height(self->format))
                         / (2 * akvcam_format_width(self->format));
            y_dst_max = (akvcam_format_width(self->format) * height
                         + width * akvcam_format_height(self->format))
                         / (2 * akvcam_format_width(self->format));
        }
    }

    i_width = akvcam_format_width(self->format) - 1;
    i_height = akvcam_format_height(self->format) - 1;
    o_width = x_dst_max - x_dst_min - 1;
    o_height = y_dst_max - y_dst_min - 1;
    x_num = i_width;
    x_den = o_width;
    xs = 0;
    y_num = i_height;
    y_den = o_height;
    ys = 0;

    if (aspectRatio == AKVCAM_ASPECT_RATIO_EXPANDING) {
        if (mode == AKVCAM_SCALING_LINEAR) {
            i_width--;
            i_height--;
            o_width--;
            o_height--;
        }

        if (width * akvcam_format_height(self->format)
            < akvcam_format_width(self->format) * height) {
            // Right and left cut
            x_num = 2 * i_height;
            x_den = 2 * o_height;
            xs = i_width * o_height - o_width * i_height;
        } else if (width * akvcam_format_height(self->format)
                   > akvcam_format_width(self->format) * height) {
            // Top and bottom cut
            y_num = 2 * i_width;
            y_den = 2 * o_width;
            ys = o_width * i_height - i_width * o_height;
        }
    }

    format = akvcam_format_new(fourcc, width, height, NULL);
    data = vzalloc(akvcam_format_size(format));

    switch (mode) {
        case AKVCAM_SCALING_FAST:
            for (y = y_dst_min; y < y_dst_max; y++) {
                size_t srcY = (y_num * (y - y_dst_min) + ys) / y_den;
                src_line = akvcam_frame_line(self, 0, srcY);
                dst_line = (akvcam_RGB24_t)
                           ((char *) data + y * akvcam_format_bypl(format, 0));

                for (x = x_dst_min; x < x_dst_max; x++) {
                    size_t srcX = (x_num * (x - x_dst_min) + xs) / x_den;
                    dst_line[x] = src_line[srcX];
                }
            }

            break;

        case AKVCAM_SCALING_LINEAR: {
            extrapolate_x = akvcam_format_width(self->format) < width?
                                &akvcam_extrapolate_up:
                                &akvcam_extrapolate_down;
            extrapolate_y = akvcam_format_height(self->format) < height?
                                &akvcam_extrapolate_up:
                                &akvcam_extrapolate_down;

            for (y = y_dst_min; y < y_dst_max; y++) {
                dst_line = (akvcam_RGB24_t)
                           ((char *) data + y * akvcam_format_bypl(format, 0));
                extrapolate_y(y - y_dst_min,
                              y_num, y_den, ys,
                              &y_min, &y_max,
                              &k_num_y, &k_den_y);

                for (x = x_dst_min; x < x_dst_max; x++) {
                    extrapolate_x(x - x_dst_min,
                                  x_num, x_den, xs,
                                  &x_min, &x_max,
                                  &k_num_x, &k_den_x);

                    dst_line[x] =
                            akvcam_extrapolated_color(self,
                                                      x_min, x_max,
                                                      k_num_x, k_den_x,
                                                      y_min, y_max,
                                                      k_num_y, k_den_y);
                }
            }

            break;
        }
    }

    akvcam_format_copy(self->format, format);
    akvcam_format_delete(format);

    if (self->data)
        vfree(self->data);

    self->data = data;
    self->size = akvcam_format_size(self->format);

    return true;
}

void akvcam_frame_swap_rgb(akvcam_frame_t self)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_RGB24_t line;
    uint8_t tmp;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return;

    width = akvcam_format_width(self->format);
    height = akvcam_format_height(self->format);

    for (y = 0; y < height; y++) {
        line = akvcam_frame_line(self, 0, y);

        for (x = 0; x < width; x++) {
            tmp = line[x].r;
            line[x].r = line[x].b;
            line[x].b = tmp;
        }
    }
}

bool akvcam_frame_convert(akvcam_frame_t self, __u32 fourcc)
{
    akvcam_format_t format;
    akvcam_frame_t frame;
    akvcam_video_convert_funtion_t convert = NULL;

    if (akvcam_format_fourcc(self->format) == fourcc)
        return true;

    convert = akvcam_convert_func(akvcam_format_fourcc(self->format), fourcc);

    if (!convert)
        return false;

    format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(format, self->format);
    akvcam_format_set_fourcc(format, fourcc);
    frame = akvcam_frame_new(format, NULL, 0);

    convert(frame, self);
    akvcam_frame_copy(self, frame);

    akvcam_frame_delete(frame);
    akvcam_format_delete(format);

    return true;
}

void akvcam_frame_adjust_hsl(akvcam_frame_t self,
                             int hue,
                             int saturation,
                             int luminance)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_RGB24_t line;
    int h;
    int s;
    int l;
    int r;
    int g;
    int b;

    if (hue == 0 && saturation == 0 && luminance == 0)
        return;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return;

    width = akvcam_format_width(self->format);
    height = akvcam_format_height(self->format);

    for (y = 0; y < height; y++) {
        line = akvcam_frame_line(self, 0, y);

        for (x = 0; x < width; x++) {
            akvcam_rgb_to_hsl(line[x].r, line[x].g, line[x].b, &h, &s, &l);

            h = akvcam_mod(h + hue, 360);
            s = akvcam_bound(0, s + saturation, 255);
            l = akvcam_bound(0, l + luminance, 255);

            akvcam_hsl_to_rgb(h, s, l, &r, &g, &b);

            line[x].r = (uint8_t) r;
            line[x].g = (uint8_t) g;
            line[x].b = (uint8_t) b;
        }
    }
}

void akvcam_frame_adjust_contrast(akvcam_frame_t self, int contrast)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_RGB24_t line;
    const uint8_t *contrast_table = akvcam_contrast_table();
    size_t contrast_offset;

    if (!contrast_table || contrast == 0)
        return;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return;

    width = akvcam_format_width(self->format);
    height = akvcam_format_height(self->format);

    contrast = akvcam_bound(-255, contrast, 255);
    contrast_offset = (size_t) (contrast + 255) << 8;

    for (y = 0; y < height; y++) {
        line = akvcam_frame_line(self, 0, y);

        for (x = 0; x < width; x++) {
            line[x].r = contrast_table[contrast_offset | line[x].r];
            line[x].g = contrast_table[contrast_offset | line[x].g];
            line[x].b = contrast_table[contrast_offset | line[x].b];
        }
    }
}

void akvcam_frame_adjust_gamma(akvcam_frame_t self, int gamma)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_RGB24_t line;
    const uint8_t *gamma_table = akvcam_gamma_table();
    size_t gamma_offset;

    if (!gamma_table || gamma == 0)
        return;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return;

    width = akvcam_format_width(self->format);
    height = akvcam_format_height(self->format);

    gamma = akvcam_bound(-255, gamma, 255);
    gamma_offset = (size_t) (gamma + 255) << 8;

    for (y = 0; y < height; y++) {
        line = akvcam_frame_line(self, 0, y);

        for (x = 0; x < width; x++) {
            line[x].r = gamma_table[gamma_offset | line[x].r];
            line[x].g = gamma_table[gamma_offset | line[x].g];
            line[x].b = gamma_table[gamma_offset | line[x].b];
        }
    }
}

void akvcam_frame_to_gray_scale(akvcam_frame_t self)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_RGB24_t line;
    int luma;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return;

    width = akvcam_format_width(self->format);
    height = akvcam_format_height(self->format);

    for (y = 0; y < height; y++) {
        line = akvcam_frame_line(self, 0, y);

        for (x = 0; x < width; x++) {
            luma = akvcam_grayval(line[x].r, line[x].g, line[x].b);

            line[x].r = (uint8_t) luma;
            line[x].g = (uint8_t) luma;
            line[x].b = (uint8_t) luma;
        }
    }
}

void akvcam_frame_adjust(akvcam_frame_t self,
                         int hue,
                         int saturation,
                         int luminance,
                         int contrast,
                         int gamma,
                         bool gray)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_RGB24_t line;
    int h;
    int s;
    int l;
    int r;
    int g;
    int b;
    int luma;
    const uint8_t *contrast_table = akvcam_contrast_table();
    const uint8_t *gamma_table = akvcam_gamma_table();
    size_t contrast_offset;
    size_t gamma_offset;

    if (hue == 0
        && saturation == 0
        && luminance == 0
        && contrast == 0
        && gamma == 0
        && !gray)
        return;

    fourcc = akvcam_format_fourcc(self->format);

    if (!akvcam_frame_adjust_format_supported(fourcc))
        return;

    width = akvcam_format_width(self->format);
    height = akvcam_format_height(self->format);

    contrast = akvcam_bound(-255, contrast, 255);
    contrast_offset = (size_t) (contrast + 255) << 8;

    gamma = akvcam_bound(-255, gamma, 255);
    gamma_offset = (size_t) (gamma + 255) << 8;

    for (y = 0; y < height; y++) {
        line = akvcam_frame_line(self, 0, y);

        for (x = 0; x < width; x++) {
            r = line[x].r;
            g = line[x].g;
            b = line[x].b;

            if (hue != 0 || saturation != 0 ||  luminance != 0) {
                akvcam_rgb_to_hsl(r, g, b, &h, &s, &l);

                h = akvcam_mod(h + hue, 360);
                s = akvcam_bound(0, s + saturation, 255);
                l = akvcam_bound(0, l + luminance, 255);

                akvcam_hsl_to_rgb(h, s, l, &r, &g, &b);
            }

            if (gamma_table && gamma != 0) {
                r = gamma_table[gamma_offset | (size_t) r];
                g = gamma_table[gamma_offset | (size_t) g];
                b = gamma_table[gamma_offset | (size_t) b];
            }

            if (contrast_table && contrast != 0) {
                r = contrast_table[contrast_offset | (size_t) r];
                g = contrast_table[contrast_offset | (size_t) g];
                b = contrast_table[contrast_offset | (size_t) b];
            }

            if (gray) {
                luma = akvcam_grayval(r, g, b);

                r = luma;
                g = luma;
                b = luma;
            }

            line[x].r = (uint8_t) r;
            line[x].g = (uint8_t) g;
            line[x].b = (uint8_t) b;
        }
    }
}

const char *akvcam_frame_scaling_to_string(AKVCAM_SCALING scaling)
{
    size_t i;
    static char scaling_str[AKVCAM_MAX_STRING_SIZE];
    static const akvcam_frame_scaling_strings scaling_strings[] = {
        {AKVCAM_SCALING_FAST  , "Fast"  },
        {AKVCAM_SCALING_LINEAR, "Linear"},
        {-1                   , ""      },
    };

    memset(scaling_str, 0, AKVCAM_MAX_STRING_SIZE);

    for (i = 0; scaling_strings[i].scaling >= 0; i++)
        if (scaling_strings[i].scaling == scaling) {
            snprintf(scaling_str,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s",
                     scaling_strings[i].str);

            return scaling_str;
        }

    snprintf(scaling_str, AKVCAM_MAX_STRING_SIZE, "AKVCAM_SCALING(%d)", scaling);

    return scaling_str;
}

const char *akvcam_frame_aspect_ratio_to_string(AKVCAM_ASPECT_RATIO aspect_ratio)
{
    size_t i;
    static char aspect_ratio_str[AKVCAM_MAX_STRING_SIZE];
    static const akvcam_frame_aspect_ratio_strings aspect_ratio_strings[] = {
        {AKVCAM_ASPECT_RATIO_IGNORE   , "Ignore"   },
        {AKVCAM_ASPECT_RATIO_KEEP     , "Keep"     },
        {AKVCAM_ASPECT_RATIO_EXPANDING, "Expanding"},
        {-1                           , ""         },
    };

    memset(aspect_ratio_str, 0, AKVCAM_MAX_STRING_SIZE);

    for (i = 0; aspect_ratio_strings[i].aspect_ratio >= 0; i++)
        if (aspect_ratio_strings[i].aspect_ratio == aspect_ratio) {
            snprintf(aspect_ratio_str,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s",
                     aspect_ratio_strings[i].str);

            return aspect_ratio_str;
        }

    snprintf(aspect_ratio_str, AKVCAM_MAX_STRING_SIZE, "AKVCAM_ASPECT_RATIO(%d)", aspect_ratio);

    return aspect_ratio_str;
}

bool akvcam_frame_can_convert(__u32 in_fourcc, __u32 out_fourcc)
{
    size_t i;

    if (in_fourcc == out_fourcc)
        return true;

    for (i = 0; i < akvcam_convert_funcs_count(); i++)
        if (akvcam_frame_convert_table[i].from == in_fourcc
            && akvcam_frame_convert_table[i].to == out_fourcc) {
            return true;
        }

    return false;
}

int akvcam_grayval(int r, int g, int b)
{
    return (11 * r + 16 * g + 5 * b) >> 5;
}

uint8_t akvcam_rgb_y(int r, int g, int b)
{
    return (uint8_t) (((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
}

uint8_t akvcam_rgb_u(int r, int g, int b)
{
    return (uint8_t) (((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
}

uint8_t akvcam_rgb_v(int r, int g, int b)
{
    return (uint8_t) (((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
}

uint8_t akvcam_yuv_r(int y, int u, int v)
{
    int r;
    UNUSED(u);
    r = (298 * (y - 16) + 409 * (v - 128) + 128) >> 8;

    return (uint8_t) (akvcam_bound(0, r, 255));
}

uint8_t akvcam_yuv_g(int y, int u, int v)
{
    int g = (298 * (y - 16) - 100 * (u - 128) - 208 * (v - 128) + 128) >> 8;

    return (uint8_t) (akvcam_bound(0, g, 255));
}

uint8_t akvcam_yuv_b(int y, int u, int v)
{
    int b;
    UNUSED(v);
    b = (298 * (y - 16) + 516 * (u - 128) + 128) >> 8;

    return (uint8_t) (akvcam_bound(0, b, 255));
}

void akvcam_bgr24_to_rgb32(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_RGB32_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].x = 255;
            dst_line[x].r = src_line[x].r;
            dst_line[x].g = src_line[x].g;
            dst_line[x].b = src_line[x].b;
        }
    }
}

void akvcam_bgr24_to_rgb24(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_RGB24_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].r = src_line[x].r;
            dst_line[x].g = src_line[x].g;
            dst_line[x].b = src_line[x].b;
        }
    }
}

void akvcam_bgr24_to_rgb16(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_RGB16_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].r = src_line[x].r >> 3;
            dst_line[x].g = src_line[x].g >> 2;
            dst_line[x].b = src_line[x].b >> 3;
        }
    }
}

void akvcam_bgr24_to_rgb15(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_RGB15_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].x = 1;
            dst_line[x].r = src_line[x].r >> 3;
            dst_line[x].g = src_line[x].g >> 3;
            dst_line[x].b = src_line[x].b >> 3;
        }
    }
}

void akvcam_bgr24_to_bgr32(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_BGR32_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].x = 255;
            dst_line[x].r = src_line[x].r;
            dst_line[x].g = src_line[x].g;
            dst_line[x].b = src_line[x].b;
        }
    }
}

void akvcam_bgr24_to_bgr16(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_BGR16_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].r = src_line[x].r >> 3;
            dst_line[x].g = src_line[x].g >> 2;
            dst_line[x].b = src_line[x].b >> 3;
        }
    }
}

void akvcam_bgr24_to_uyvy(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_UYVY_t dst_line;
    uint8_t r0;
    uint8_t g0;
    uint8_t b0;
    uint8_t r1;
    uint8_t g1;
    uint8_t b1;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            x_yuv = x / 2;

            r0 = src_line[x].r;
            g0 = src_line[x].g;
            b0 = src_line[x].b;

            x++;

            r1 = src_line[x].r;
            g1 = src_line[x].g;
            b1 = src_line[x].b;

            dst_line[x_yuv].u0 = akvcam_rgb_u(r0, g0, b0);
            dst_line[x_yuv].y0 = akvcam_rgb_y(r0, g0, b0);
            dst_line[x_yuv].v0 = akvcam_rgb_v(r0, g0, b0);
            dst_line[x_yuv].y1 = akvcam_rgb_y(r1, g1, b1);
        }
    }
}

void akvcam_bgr24_to_yuy2(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    akvcam_YUY2_t dst_line;
    uint8_t r0;
    uint8_t g0;
    uint8_t b0;
    uint8_t r1;
    uint8_t g1;
    uint8_t b1;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            x_yuv = x / 2;

            r0 = src_line[x].r;
            g0 = src_line[x].g;
            b0 = src_line[x].b;

            x++;

            r1 = src_line[x].r;
            g1 = src_line[x].g;
            b1 = src_line[x].b;

            dst_line[x_yuv].y0 = akvcam_rgb_y(r0, g0, b0);
            dst_line[x_yuv].u0 = akvcam_rgb_u(r0, g0, b0);
            dst_line[x_yuv].y1 = akvcam_rgb_y(r1, g1, b1);
            dst_line[x_yuv].v0 = akvcam_rgb_v(r0, g0, b0);
        }
    }
}

void akvcam_bgr24_to_nv12(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    uint8_t *dst_line_y;
    akvcam_VU_t dst_line_vu;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line_y = akvcam_frame_line(dst, 0, y);
        dst_line_vu = akvcam_frame_line(dst, 1, y / 2);

        for (x = 0; x < width; x++) {
            r = src_line[x].r;
            g = src_line[x].g;
            b = src_line[x].b;

            dst_line_y[y] = akvcam_rgb_y(r, g, b);

            if (!(x & 0x1) && !(y & 0x1)) {
                dst_line_vu[x / 2].v = akvcam_rgb_v(r, g, b);
                dst_line_vu[x / 2].u = akvcam_rgb_u(r, g, b);
            }
        }
    }
}

void akvcam_bgr24_to_nv21(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_BGR24_t src_line;
    uint8_t *dst_line_y;
    akvcam_UV_t dst_line_vu;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line_y = akvcam_frame_line(dst, 0, y);
        dst_line_vu = akvcam_frame_line(dst, 1, y / 2);

        for (x = 0; x < width; x++) {
            r = src_line[x].r;
            g = src_line[x].g;
            b = src_line[x].b;

            dst_line_y[y] = akvcam_rgb_y(r, g, b);

            if (!(x & 0x1) && !(y & 0x1)) {
                dst_line_vu[x / 2].v = akvcam_rgb_v(r, g, b);
                dst_line_vu[x / 2].u = akvcam_rgb_u(r, g, b);
            }
        }
    }
}

void akvcam_rgb24_to_rgb32(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_RGB32_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].x = 255;
            dst_line[x].r = src_line[x].r;
            dst_line[x].g = src_line[x].g;
            dst_line[x].b = src_line[x].b;
        }
    }
}

void akvcam_rgb24_to_rgb16(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_RGB16_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].r = src_line[x].r >> 3;
            dst_line[x].g = src_line[x].g >> 2;
            dst_line[x].b = src_line[x].b >> 3;
        }
    }
}

void akvcam_rgb24_to_rgb15(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_RGB15_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].x = 1;
            dst_line[x].r = src_line[x].r >> 3;
            dst_line[x].g = src_line[x].g >> 3;
            dst_line[x].b = src_line[x].b >> 3;
        }
    }
}

void akvcam_rgb24_to_bgr32(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_BGR32_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].x = 255;
            dst_line[x].r = src_line[x].r;
            dst_line[x].g = src_line[x].g;
            dst_line[x].b = src_line[x].b;
        }
    }
}

void akvcam_rgb24_to_bgr24(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_BGR24_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].r = src_line[x].r;
            dst_line[x].g = src_line[x].g;
            dst_line[x].b = src_line[x].b;
        }
    }
}

void akvcam_rgb24_to_bgr16(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_BGR16_t dst_line;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            dst_line[x].r = src_line[x].r >> 3;
            dst_line[x].g = src_line[x].g >> 2;
            dst_line[x].b = src_line[x].b >> 3;
        }
    }
}

void akvcam_rgb24_to_uyvy(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_UYVY_t dst_line;
    uint8_t r0;
    uint8_t g0;
    uint8_t b0;
    uint8_t r1;
    uint8_t g1;
    uint8_t b1;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            x_yuv = x / 2;

            r0 = src_line[x].r;
            g0 = src_line[x].g;
            b0 = src_line[x].b;

            x++;

            r1 = src_line[x].r;
            g1 = src_line[x].g;
            b1 = src_line[x].b;

            dst_line[x_yuv].u0 = akvcam_rgb_u(r0, g0, b0);
            dst_line[x_yuv].y0 = akvcam_rgb_y(r0, g0, b0);
            dst_line[x_yuv].v0 = akvcam_rgb_v(r0, g0, b0);
            dst_line[x_yuv].y1 = akvcam_rgb_y(r1, g1, b1);
        }
    }
}

void akvcam_rgb24_to_yuy2(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    akvcam_YUY2_t dst_line;
    uint8_t r0;
    uint8_t g0;
    uint8_t b0;
    uint8_t r1;
    uint8_t g1;
    uint8_t b1;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line = akvcam_frame_line(dst, 0, y);

        for (x = 0; x < width; x++) {
            x_yuv = x / 2;

            r0 = src_line[x].r;
            g0 = src_line[x].g;
            b0 = src_line[x].b;

            x++;

            r1 = src_line[x].r;
            g1 = src_line[x].g;
            b1 = src_line[x].b;

            dst_line[x_yuv].y0 = akvcam_rgb_y(r0, g0, b0);
            dst_line[x_yuv].u0 = akvcam_rgb_u(r0, g0, b0);
            dst_line[x_yuv].y1 = akvcam_rgb_y(r1, g1, b1);
            dst_line[x_yuv].v0 = akvcam_rgb_v(r0, g0, b0);
        }
    }
}

void akvcam_rgb24_to_nv12(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    uint8_t *dst_line_y;
    akvcam_VU_t dst_line_vu;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line_y = akvcam_frame_line(dst, 0, y);
        dst_line_vu = akvcam_frame_line(dst, 1, y / 2);

        for (x = 0; x < width; x++) {
            r = src_line[x].r;
            g = src_line[x].g;
            b = src_line[x].b;

            dst_line_y[y] = akvcam_rgb_y(r, g, b);

            if (!(x & 0x1) && !(y & 0x1)) {
                dst_line_vu[x / 2].v = akvcam_rgb_v(r, g, b);
                dst_line_vu[x / 2].u = akvcam_rgb_u(r, g, b);
            }
        }
    }
}

void akvcam_rgb24_to_nv21(akvcam_frame_t dst, akvcam_frame_ct src)
{
    size_t x;
    size_t y;
    size_t width = akvcam_format_width(src->format);
    size_t height = akvcam_format_height(src->format);
    akvcam_RGB24_t src_line;
    uint8_t *dst_line_y;
    akvcam_UV_t dst_line_vu;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    for (y = 0; y < height; y++) {
        src_line = akvcam_frame_line(src, 0, y);
        dst_line_y = akvcam_frame_line(dst, 0, y);
        dst_line_vu = akvcam_frame_line(dst, 1, y / 2);

        for (x = 0; x < width; x++) {
            r = src_line[x].r;
            g = src_line[x].g;
            b = src_line[x].b;

            dst_line_y[y] = akvcam_rgb_y(r, g, b);

            if (!(x & 0x1) && !(y & 0x1)) {
                dst_line_vu[x / 2].v = akvcam_rgb_v(r, g, b);
                dst_line_vu[x / 2].u = akvcam_rgb_u(r, g, b);
            }
        }
    }
}

void akvcam_extrapolate_up(size_t dst_coord,
                           size_t num, size_t den, size_t s,
                           size_t *src_coord_min, size_t *src_coord_max,
                           size_t *k_num, size_t *k_den)
{
    size_t dst_coord_min;
    size_t dst_coord_max;
    *src_coord_min = (num * dst_coord + s) / den;
    *src_coord_max = *src_coord_min + 1;
    dst_coord_min = (den * *src_coord_min - s) / num;
    dst_coord_max = (den * *src_coord_max - s) / num;
    *k_num = dst_coord - dst_coord_min;
    *k_den = dst_coord_max - dst_coord_min;
}

void akvcam_extrapolate_down(size_t dst_coord,
                             size_t num, size_t den, size_t s,
                             size_t *src_coord_min, size_t *src_coord_max,
                             size_t *k_num, size_t *k_den)
{
    *src_coord_min = (num * dst_coord + s) / den;
    *src_coord_max = *src_coord_min;
    *k_num = 0;
    *k_den = 1;
}

uint8_t akvcam_extrapolate_component(uint8_t min, uint8_t max,
                                     size_t k_num, size_t k_Den)
{
    return (uint8_t) ((k_num * (max - min) + k_Den * min) / k_Den);
}

akvcam_RGB24 akvcam_extrapolate_color(const akvcam_RGB24_t color_min,
                                      const akvcam_RGB24_t color_max,
                                      size_t k_num,
                                      size_t k_den)
{
    akvcam_RGB24 color = {
        .r = akvcam_extrapolate_component(color_min->r, color_max->r, k_num, k_den),
        .g = akvcam_extrapolate_component(color_min->g, color_max->g, k_num, k_den),
        .b = akvcam_extrapolate_component(color_min->b, color_max->b, k_num, k_den),
    };

    return color;
}

akvcam_RGB24 akvcam_extrapolated_color(akvcam_frame_t self,
                                       size_t x_min, size_t x_max,
                                       size_t k_num_x, size_t k_den_x,
                                       size_t y_min, size_t y_max,
                                       size_t k_num_y, size_t k_den_y)
{
    size_t line_size = akvcam_format_bypl(self->format, 0);
    akvcam_RGB24_t min_line =
            (akvcam_RGB24_t) ((char *) self->data + y_min * line_size);
    akvcam_RGB24_t max_line =
            (akvcam_RGB24_t) ((char *) self->data + y_max * line_size);
    akvcam_RGB24 color_min = akvcam_extrapolate_color(min_line + x_min,
                                                      min_line + x_max,
                                                      k_num_x,
                                                      k_den_x);
    akvcam_RGB24 color_max = akvcam_extrapolate_color(max_line + x_min,
                                                      max_line + x_max,
                                                      k_num_x,
                                                      k_den_x);

    return akvcam_extrapolate_color(&color_min, &color_max, k_num_y, k_den_y);
}

void akvcam_rgb_to_hsl(int r, int g, int b, int *h, int *s, int *l)
{
    int max = akvcam_max(r, akvcam_max(g, b));
    int min = akvcam_min(r, akvcam_min(g, b));
    int c = max - min;

    *l = (max + min) / 2;

    if (!c) {
        *h = 0;
        *s = 0;
    } else {
        if (max == r)
            *h = akvcam_mod(g - b, 6 * c);
        else if (max == g)
            *h = b - r + 2 * c;
        else
            *h = r - g + 4 * c;

        *h = 60 * (*h) / c;
        *s = 255 * c / (255 - akvcam_abs(max + min - 255));
    }
}

void akvcam_hsl_to_rgb(int h, int s, int l, int *r, int *g, int *b)
{
    int c = s * (255 - akvcam_abs(2 * l - 255)) / 255;
    int x = c * (60 - akvcam_abs((h % 120) - 60)) / 60;
    int m;

    if (h >= 0 && h < 60) {
        *r = c;
        *g = x;
        *b = 0;
    } else if (h >= 60 && h < 120) {
        *r = x;
        *g = c;
        *b = 0;
    } else if (h >= 120 && h < 180) {
        *r = 0;
        *g = c;
        *b = x;
    } else if (h >= 180 && h < 240) {
        *r = 0;
        *g = x;
        *b = c;
    } else if (h >= 240 && h < 300) {
        *r = x;
        *g = 0;
        *b = c;
    } else if (h >= 300 && h < 360) {
        *r = c;
        *g = 0;
        *b = x;
    } else {
        *r = 0;
        *g = 0;
        *b = 0;
    }

    m = 2 * l - c;

    *r = (2 * (*r) + m) / 2;
    *g = (2 * (*g) + m) / 2;
    *b = (2 * (*b) + m) / 2;
}

size_t akvcam_convert_funcs_count(void)
{
    size_t i;
    static size_t count = 0;

    if (count < 1)
        for (i = 0; akvcam_frame_convert_table[i].from; i++)
            count++;

    return count;
}

akvcam_video_convert_funtion_t akvcam_convert_func(__u32 from, __u32 to)
{
    size_t i;
    akvcam_video_convert_ct convert;

    for (i = 0; i < akvcam_convert_funcs_count(); i++) {
        convert = akvcam_frame_convert_table + i;

        if (convert->from == from && convert->to == to)
            return convert->convert;
    }

    return NULL;
}

bool akvcam_frame_adjust_format_supported(__u32 fourcc)
{
    size_t i;
    static const __u32 adjust_formats[] = {
        V4L2_PIX_FMT_BGR24,
        V4L2_PIX_FMT_RGB24,
        0
    };

    for (i = 0; adjust_formats[i]; i++)
        if (adjust_formats[i] == fourcc)
            return true;

    return false;
}

const uint8_t *akvcam_contrast_table(void)
{
    static uint8_t *contrast_table = NULL;
    size_t i;
    size_t j = 0;
    ssize_t contrast;
    ssize_t f_num;
    ssize_t f_den;
    ssize_t ic;

    if (contrast_table)
        return contrast_table;

    contrast_table = vmalloc(511 * 256);

    for (contrast = -255; contrast < 256; contrast++) {
        f_num = 259 * (255 + contrast);
        f_den = 255 * (259 - contrast);

        for (i = 0; i < 256; i++, j++) {
            ic = (f_num * ((ssize_t) i - 128) + 128 * f_den) / f_den;
            contrast_table[j] = (uint8_t) akvcam_bound(0, ic, 255);
        }
    }

    akvcam_global_deleter_add(contrast_table, (akvcam_delete_t) vfree);

    return contrast_table;
}

/* Gamma correction is traditionally computed with the following formula:
 *
 * c = N * (c / N) ^ gamma
 *
 * Where 'c' is the color component and 'N' is the maximum value of the color
 * component, 255 in this case. The formula will define a curve between 0 and
 * N. When 'gamma' is 1 it will draw a rect, returning the identity image at the
 * output. when 'gamma' is near to 0 it will draw a decreasing curve (mountain),
 * Giving more light to darker colors. When 'gamma' is higher than 1 it will
 * draw a increasing curve (valley), making bright colors darker.
 *
 * Explained in a simple way, gamma correction will modify image brightness
 * preserving the contrast.
 *
 * The problem with the original formula is that it requires floating point
 * computing which is not possible in the kernel because not all target
 * architectures have a FPU.
 *
 * So instead, we will use a quadric function, that even if it does not returns
 * the same values, it will cause the same effect and is good enough for our
 * purpose. We use the formula:
 *
 * y = a * x ^ 2 + b * x
 *
 * and because we have the point (0, N) already defined, then we can calculate
 * b as :
 *
 * b = 1 - a * N
 *
 * and replacing
 *
 * y = a * x ^ 2 + (1 - a * N) * x
 *
 * we are missing a third point (x', y') to fully define the value of 'a', so
 * the value of 'a' will be given by:
 *
 * a = (y' - x') / (x' ^ 2 - N * x')
 *
 * we will take the point (x', y') from the segment orthogonal to the curve's
 * segment, that is:
 *
 * y' = N - x'
 *
 * Here x' will be our fake 'gamma' value.
 * Then the value of 'a' becomes:
 *
 * a = (N - 2 * x') / (x' ^ 2 - N * x')
 *
 * finally we clamp/bound the resulting value between 0 and N and that's what
 * this code does.
 */
const uint8_t *akvcam_gamma_table(void)
{
    static uint8_t *gamma_table = NULL;
    ssize_t i;
    size_t j = 0;
    ssize_t gamma;
    ssize_t f_num;
    ssize_t f_den;
    ssize_t g;
    ssize_t ig;

    if (gamma_table)
        return gamma_table;

    gamma_table = vmalloc(511 * 256);

    for (gamma = -255; gamma < 256; gamma++) {
        g = (255 + gamma) >> 1;
        f_num = 2 * g - 255;
        f_den = g * (g - 255);

        for (i = 0; i < 256; i++, j++) {
            if (g > 0 && g != 255) {
                ig = (f_num * i * i + (f_den - f_num * 255) * i) / f_den;
                ig = akvcam_bound(0, ig, 255);
            } else if (g != 255) {
                ig = 0;
            } else {
                ig = 255;
            }

            gamma_table[j] = (uint8_t) ig;
        }
    }

    akvcam_global_deleter_add(gamma_table, (akvcam_delete_t) vfree);

    return gamma_table;
}
