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

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "frame.h"
#include "format.h"
#include "object.h"
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
void akvcam_bgr24_to_rgb32(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_bgr24_to_rgb24(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_bgr24_to_rgb16(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_bgr24_to_rgb15(akvcam_frame_t dst, akvcam_frame_t src);

// BGR to BGR formats
void akvcam_bgr24_to_bgr32(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_bgr24_to_bgr16(akvcam_frame_t dst, akvcam_frame_t src);

// BGR to Luminance+Chrominance formats
void akvcam_bgr24_to_uyvy(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_bgr24_to_yuy2(akvcam_frame_t dst, akvcam_frame_t src);

// BGR to two planes -- one Y, one Cr + Cb interleaved
void akvcam_bgr24_to_nv12(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_bgr24_to_nv21(akvcam_frame_t dst, akvcam_frame_t src);

// RGB to RGB formats
void akvcam_rgb24_to_rgb32(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_rgb24_to_rgb16(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_rgb24_to_rgb15(akvcam_frame_t dst, akvcam_frame_t src);

// RGB to BGR formats
void akvcam_rgb24_to_bgr32(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_rgb24_to_bgr24(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_rgb24_to_bgr16(akvcam_frame_t dst, akvcam_frame_t src);

// RGB to Luminance+Chrominance formats
void akvcam_rgb24_to_uyvy(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_rgb24_to_yuy2(akvcam_frame_t dst, akvcam_frame_t src);

// RGB to two planes -- one Y, one Cr + Cb interleaved
void akvcam_rgb24_to_nv12(akvcam_frame_t dst, akvcam_frame_t src);
void akvcam_rgb24_to_nv21(akvcam_frame_t dst, akvcam_frame_t src);

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
                                               akvcam_frame_t src);

typedef struct
{
    __u32 from;
    __u32 to;
    akvcam_video_convert_funtion_t convert;
} akvcam_video_convert, *akvcam_video_convert_t;

static akvcam_video_convert akvcam_frame_convert_table[] = {
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

static struct
{
    uint8_t *data;
    ssize_t ref;
} akvcam_contrast_table = {NULL, 0};

void akvcam_contrast_table_init(void);
void akvcam_contrast_table_uninit(void);

struct akvcam_frame
{
    akvcam_object_t self;
    akvcam_format_t format;
    void *data;
    size_t size;
};

bool akvcam_frame_adjust_format_supported(__u32 fourcc)
{
    size_t i;
    __u32 adjust_formats[] = {
        V4L2_PIX_FMT_BGR24,
        V4L2_PIX_FMT_RGB24,
        0
    };

    for (i = 0; adjust_formats[i]; i++)
        if (adjust_formats[i] == fourcc)
            return true;

    return false;
}

akvcam_frame_t akvcam_frame_new(akvcam_format_t format,
                                const void *data,
                                size_t size)
{
    akvcam_frame_t self = kzalloc(sizeof(struct akvcam_frame), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_frame_delete);
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

    akvcam_contrast_table_init();

    return self;
}

void akvcam_frame_delete(akvcam_frame_t *self)
{
    if (!self || !*self)
        return;

    akvcam_contrast_table_uninit();

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    if ((*self)->data)
        vfree((*self)->data);

    akvcam_format_delete(&((*self)->format));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

void akvcam_frame_copy(akvcam_frame_t self, const akvcam_frame_t other)
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

akvcam_format_t akvcam_frame_format_nr(const akvcam_frame_t self)
{
    return self->format;
}

akvcam_format_t akvcam_frame_format(const akvcam_frame_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->format));

    return self->format;
}

void *akvcam_frame_data(const akvcam_frame_t self)
{
    return self->data;
}

void *akvcam_frame_line(const akvcam_frame_t self, size_t plane, size_t i)
{
    return (char *) self->data
            + akvcam_format_offset(self->format, plane)
            + i * akvcam_format_bypl(self->format, plane);
}

const void *akvcam_frame_const_line(const akvcam_frame_t self, size_t plane, size_t i)
{
    return akvcam_frame_line(self, plane, i);
}

size_t akvcam_frame_size(const akvcam_frame_t self)
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
    struct file *bmp_file;
    char type[2];
    akvcam_bmp_header header;
    akvcam_bmp_image_header image_header;
    akvcam_RGB24_t line;
    akvcam_BGR24 pixel24;
    akvcam_BGR32 pixel32;
    struct kstat stats;
    mm_segment_t oldfs;
    loff_t offset;
    uint32_t x;
    uint32_t y;

    akvcam_frame_clear(self);

    if (!file_name || strlen(file_name) < 1)
        return false;

    memset(&stats, 0, sizeof(struct kstat));
    oldfs = get_fs();
    set_fs(KERNEL_DS);

    if (vfs_stat((const char __user *) file_name, &stats)) {
        set_fs(oldfs);

        return false;
    }

    set_fs(oldfs);

    bmp_file = filp_open(file_name, O_RDONLY, 0);

    if (!bmp_file)
        return false;

    offset = 0;
    akvcam_file_read(bmp_file, type, 2, offset);

    if (memcmp(type, "BM", 2) != 0)
        goto akvcam_frame_load_failed;

    offset = 0;
    akvcam_file_read(bmp_file,
                     (char *) &header,
                     sizeof(akvcam_bmp_header),
                     offset);
    offset = 0;
    akvcam_file_read(bmp_file,
                     (char *) &image_header,
                     sizeof(akvcam_bmp_image_header),
                     offset);

    vfs_setpos(bmp_file, header.offBits, stats.size);
    akvcam_format_set_fourcc(self->format, V4L2_PIX_FMT_RGB24);
    akvcam_format_set_width(self->format, image_header.width);
    akvcam_format_set_height(self->format, image_header.height);
    self->size = akvcam_format_size(self->format);

    if (!self->size)
        goto akvcam_frame_load_failed;

    self->data = vmalloc(self->size);

    switch (image_header.bitCount) {
        case 24:
            for (y = 0; y < image_header.height; y++) {
                line = akvcam_frame_line(self, 0, image_header.height - y - 1);

                for (x = 0; x < image_header.width; x++) {
                    offset = 0;
                    akvcam_file_read(bmp_file,
                                     (char *) &pixel24,
                                     sizeof(akvcam_BGR24),
                                     offset);
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
                    offset = 0;
                    akvcam_file_read(bmp_file,
                                     (char *) &pixel32,
                                     sizeof(akvcam_BGR32),
                                     offset);
                    line[x].r = pixel32.r;
                    line[x].g = pixel32.g;
                    line[x].b = pixel32.b;
                }
            }

            break;

        default:
            goto akvcam_frame_load_failed;
    }

    filp_close(bmp_file, NULL);

    return true;

akvcam_frame_load_failed:
    akvcam_frame_clear(self);

    if (bmp_file)
        filp_close(bmp_file, NULL);

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
    akvcam_format_delete(&format);

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

    akvcam_frame_delete(&frame);
    akvcam_format_delete(&format);

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
    size_t contrast_offset;

    if (!akvcam_contrast_table.data || contrast == 0)
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
            line[x].r = akvcam_contrast_table.data[contrast_offset | line[x].r];
            line[x].g = akvcam_contrast_table.data[contrast_offset | line[x].g];
            line[x].b = akvcam_contrast_table.data[contrast_offset | line[x].b];
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
    size_t contrast_offset;

    if (hue == 0
        && saturation == 0
        && luminance == 0
        && contrast == 0
        && !gray)
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

            if (contrast != 0) {
                r = akvcam_contrast_table.data[contrast_offset | (size_t) r];
                g = akvcam_contrast_table.data[contrast_offset | (size_t) g];
                b = akvcam_contrast_table.data[contrast_offset | (size_t) b];
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

void akvcam_bgr24_to_rgb32(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_rgb24(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_rgb16(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_rgb15(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_bgr32(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_bgr16(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_uyvy(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_yuy2(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_nv12(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_bgr24_to_nv21(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_rgb32(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_rgb16(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_rgb15(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_bgr32(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_bgr24(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_bgr16(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_uyvy(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_yuy2(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    size_t x_yuv;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_nv12(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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

void akvcam_rgb24_to_nv21(akvcam_frame_t dst, akvcam_frame_t src)
{
    size_t x;
    size_t y;
    akvcam_format_t format = akvcam_frame_format_nr(src);
    size_t width = akvcam_format_width(format);
    size_t height = akvcam_format_height(format);
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
    akvcam_video_convert_t convert;

    for (i = 0; i < akvcam_convert_funcs_count(); i++) {
        convert = akvcam_frame_convert_table + i;

        if (convert->from == from && convert->to == to)
            return convert->convert;
    }

    return NULL;
}

void akvcam_contrast_table_init(void)
{
    size_t i;
    size_t j = 0;
    ssize_t contrast;
    ssize_t f_num;
    ssize_t f_den;
    ssize_t ic;

    if (akvcam_contrast_table.ref > 0) {
        akvcam_contrast_table.ref++;

        return;
    }

    akvcam_contrast_table.data = vmalloc(511 * 256);

    for (contrast = -255; contrast < 256; contrast++) {
        f_num = 259 * (255 + contrast);
        f_den = 255 * (259 - contrast);

        for (i = 0; i < 256; i++, j++) {
            ic = (f_num * ((ssize_t) i - 128) + 128 * f_den) / f_den;
            akvcam_contrast_table.data[j] = (uint8_t) akvcam_bound(0, ic, 255);
        }
    }

    akvcam_contrast_table.ref++;
}

void akvcam_contrast_table_uninit(void)
{

    if (akvcam_contrast_table.ref > 0)
        akvcam_contrast_table.ref--;

    if (akvcam_contrast_table.ref > 0)
        return;

    if (akvcam_contrast_table.data) {
        vfree(akvcam_contrast_table.data);
        akvcam_contrast_table.data = NULL;
    }
}
