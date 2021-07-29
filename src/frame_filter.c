/* akvcam, virtual camera for Linux.
 * Copyright (C) 2021  Gonzalo Exequiel Pedone
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

#include "frame_filter.h"
#include "frame.h"
#include "format.h"
#include "log.h"
#include "utils.h"

typedef struct
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
} akvcam_RGB24, *akvcam_RGB24_t;

struct akvcam_frame_filter
{
    struct kref ref;
    uint8_t *contrast_table;
    uint8_t *gamma_table;
};

void akvcam_rgb_to_hsl(int r, int g, int b, int *h, int *s, int *l);
void akvcam_hsl_to_rgb(int h, int s, int l, int *r, int *g, int *b);
void akvcam_init_contrast_table(akvcam_frame_filter_t self);
void akvcam_init_gamma_table(akvcam_frame_filter_t self);
int akvcam_grayval(int r, int g, int b);
bool akvcam_filter_format_supported(__u32 fourcc);

akvcam_frame_filter_t akvcam_frame_filter_new(void)
{
    akvcam_frame_filter_t self =
            kzalloc(sizeof(struct akvcam_frame_filter), GFP_KERNEL);
    kref_init(&self->ref);
    akvcam_init_contrast_table(self);
    akvcam_init_gamma_table(self);

    return self;
}

static void akvcam_frame_filter_free(struct kref *ref)
{
    akvcam_frame_filter_t self =
            container_of(ref, struct akvcam_frame_filter, ref);

    if (self->gamma_table)
        vfree(self->gamma_table);

    if (self->contrast_table)
        vfree(self->contrast_table);

    kfree(self);
}

void akvcam_frame_filter_delete(akvcam_frame_filter_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_frame_filter_free);
}

akvcam_frame_filter_t akvcam_frame_filter_ref(akvcam_frame_filter_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_frame_filter_swap_rgb(akvcam_frame_filter_ct self,
                                  akvcam_frame_t frame)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_format_t format;
    UNUSED(self);

    akpr_function();

    format = akvcam_frame_format(frame);
    fourcc = akvcam_format_fourcc(format);

    if (!akvcam_filter_format_supported(fourcc)) {
        akvcam_format_delete(format);

        return;
    }

    width = akvcam_format_width(format);
    height = akvcam_format_height(format);
    akvcam_format_delete(format);

    for (y = 0; y < height; y++) {
        akvcam_RGB24_t line = akvcam_frame_line(frame, 0, y);

        for (x = 0; x < width; x++) {
            uint8_t tmp = line[x].r;
            line[x].r = line[x].b;
            line[x].b = tmp;
        }
    }
}

void akvcam_frame_filter_hsl(akvcam_frame_filter_ct self,
                             akvcam_frame_t frame,
                             int hue,
                             int saturation,
                             int luminance)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    int h;
    int s;
    int l;
    int r;
    int g;
    int b;
    akvcam_format_t format;
    UNUSED(self);

    akpr_function();

    if (hue == 0 && saturation == 0 && luminance == 0)
        return;

    format = akvcam_frame_format(frame);
    fourcc = akvcam_format_fourcc(format);

    if (!akvcam_filter_format_supported(fourcc)) {
        akvcam_format_delete(format);

        return;
    }

    width = akvcam_format_width(format);
    height = akvcam_format_height(format);
    akvcam_format_delete(format);

    for (y = 0; y < height; y++) {
        akvcam_RGB24_t line = akvcam_frame_line(frame, 0, y);

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

void akvcam_frame_filter_contrast(akvcam_frame_filter_ct self,
                                  akvcam_frame_t frame,
                                  int contrast)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    size_t contrast_offset;
    akvcam_format_t format;

    akpr_function();

    if (!self->contrast_table || contrast == 0)
        return;

    format = akvcam_frame_format(frame);
    fourcc = akvcam_format_fourcc(format);

    if (!akvcam_filter_format_supported(fourcc)) {
        akvcam_format_delete(format);

        return;
    }

    width = akvcam_format_width(format);
    height = akvcam_format_height(format);
    akvcam_format_delete(format);
    contrast = akvcam_bound(-255, contrast, 255);
    contrast_offset = (size_t) (contrast + 255) << 8;

    for (y = 0; y < height; y++) {
        akvcam_RGB24_t line = akvcam_frame_line(frame, 0, y);

        for (x = 0; x < width; x++) {
            line[x].r = self->contrast_table[contrast_offset | line[x].r];
            line[x].g = self->contrast_table[contrast_offset | line[x].g];
            line[x].b = self->contrast_table[contrast_offset | line[x].b];
        }
    }
}

void akvcam_frame_filter_gamma(akvcam_frame_filter_ct self,
                               akvcam_frame_t frame,
                               int gamma)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    size_t gamma_offset;
    akvcam_format_t format;

    akpr_function();

    if (!self->gamma_table || gamma == 0)
        return;

    format = akvcam_frame_format(frame);
    fourcc = akvcam_format_fourcc(format);

    if (!akvcam_filter_format_supported(fourcc)) {
        akvcam_format_delete(format);

        return;
    }

    width = akvcam_format_width(format);
    height = akvcam_format_height(format);
    akvcam_format_delete(format);
    gamma = akvcam_bound(-255, gamma, 255);
    gamma_offset = (size_t) (gamma + 255) << 8;

    for (y = 0; y < height; y++) {
        akvcam_RGB24_t line = akvcam_frame_line(frame, 0, y);

        for (x = 0; x < width; x++) {
            line[x].r = self->gamma_table[gamma_offset | line[x].r];
            line[x].g = self->gamma_table[gamma_offset | line[x].g];
            line[x].b = self->gamma_table[gamma_offset | line[x].b];
        }
    }
}

void akvcam_frame_filter_gray(akvcam_frame_filter_ct self,
                              akvcam_frame_t frame)
{
    __u32 fourcc;
    size_t width;
    size_t height;
    size_t x;
    size_t y;
    akvcam_format_t format;
    UNUSED(self);

    akpr_function();
    format = akvcam_frame_format(frame);
    fourcc = akvcam_format_fourcc(format);

    if (!akvcam_filter_format_supported(fourcc)) {
        akvcam_format_delete(format);

        return;
    }

    width = akvcam_format_width(format);
    height = akvcam_format_height(format);
    akvcam_format_delete(format);

    for (y = 0; y < height; y++) {
        akvcam_RGB24_t line = akvcam_frame_line(frame, 0, y);

        for (x = 0; x < width; x++) {
            int luma = akvcam_grayval(line[x].r, line[x].g, line[x].b);

            line[x].r = (uint8_t) luma;
            line[x].g = (uint8_t) luma;
            line[x].b = (uint8_t) luma;
        }
    }
}

void akvcam_frame_filter_apply(akvcam_frame_filter_ct self,
                               akvcam_frame_t frame,
                               int hue,
                               int saturation,
                               int luminance,
                               int contrast,
                               int gamma,
                               bool gray,
                               bool swap_rgb)
{
    akpr_function();

    if (swap_rgb)
        akvcam_frame_filter_swap_rgb(self, frame);

    akvcam_frame_filter_hsl(self, frame, hue, saturation, luminance);
    akvcam_frame_filter_gamma(self, frame, gamma);
    akvcam_frame_filter_contrast(self, frame, contrast);

    if (gray)
        akvcam_frame_filter_gray(self, frame);
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

void akvcam_init_contrast_table(akvcam_frame_filter_t self)
{
    static const int64_t min_contrast = -255;
    static const int64_t max_contrast = 255;
    static const int64_t max_color = 255;
    int64_t contrast;

    self->contrast_table = vmalloc((max_color + 1) * (max_contrast - min_contrast + 1));

    for (contrast = min_contrast; contrast <= max_contrast; contrast++) {
        uint64_t i;
        int64_t f_num = 259 * (255 + contrast);
        int64_t f_den = 255 * (259 - contrast);
        size_t offset = (size_t) (contrast + 255) << 8;

        for (i = 0; i <= max_color; i++) {
            int64_t ic = (f_num * ((ssize_t) i - 128) + 128 * f_den) / f_den;
            self->contrast_table[offset | i] = (uint8_t) akvcam_bound(0, ic, 255);
        }
    }
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
void akvcam_init_gamma_table(akvcam_frame_filter_t self)
{
    static const int64_t min_gamma = -255;
    static const int64_t max_gamma = 255;
    static const int64_t max_color = 255;
    int64_t gamma;

    self->gamma_table = vmalloc((max_color + 1) * (max_gamma - min_gamma + 1));

    for (gamma = min_gamma; gamma <= max_gamma; gamma++) {
        int64_t i;
        int64_t g = (255 + gamma) >> 1;
        int64_t f_num = 2 * g - 255;
        int64_t f_den = g * (g - 255);
        size_t offset = (size_t) (gamma + 255) << 8;

        for (i = 0; i <= max_color; i++) {
            int64_t ig;

            if (g > 0 && g != 255) {
                ig = (f_num * i * i + (f_den - f_num * 255) * i) / f_den;
                ig = akvcam_bound(0, ig, 255);
            } else if (g != 255) {
                ig = 0;
            } else {
                ig = 255;
            }

            self->gamma_table[offset | i] = (uint8_t) ig;
        }
    }
}

int akvcam_grayval(int r, int g, int b)
{
    return (11 * r + 16 * g + 5 * b) >> 5;
}

bool akvcam_filter_format_supported(__u32 fourcc)
{
    size_t i;
    static const __u32 filter_contrast_formats[] = {
        V4L2_PIX_FMT_BGR24,
        V4L2_PIX_FMT_RGB24,
        0
    };

    for (i = 0; filter_contrast_formats[i]; i++)
        if (filter_contrast_formats[i] == fourcc)
            return true;

    return false;
}
