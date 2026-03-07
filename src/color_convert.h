/* akvcam, virtual camera for Linux.
 * Copyright (C) 2026  Gonzalo Exequiel Pedone
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

#ifndef AKVCAM_COLOR_CONVERT_H
#define AKVCAM_COLOR_CONVERT_H

#include "color_convert_types.h"
#include "format_specs_types.h"
#include "utils.h"

// public
akvcam_color_convert_t akvcam_color_convert_new(void);
akvcam_color_convert_t akvcam_color_convert_new_copy(akvcam_color_convert_ct other);
void akvcam_color_convert_delete(akvcam_color_convert_t self);
akvcam_color_convert_t akvcam_color_convert_ref(akvcam_color_convert_t self);

void akvcam_color_convert_copy(akvcam_color_convert_t self, akvcam_color_convert_ct other);
AKVCAM_YUV_COLOR_SPACE akvcam_color_convert_yuv_color_space(akvcam_color_convert_ct self);
void akvcam_color_convert_set_yuv_color_space(akvcam_color_convert_t self,
                                              AKVCAM_YUV_COLOR_SPACE yuv_color_space);
AKVCAM_YUV_COLOR_SPACE_TYPE akvcam_color_convert_yuv_color_space_type(akvcam_color_convert_ct self);
void akvcam_color_convert_set_yuv_color_space_type(akvcam_color_convert_t self,
                                                   AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type);
void akvcam_color_convert_load_color_matrix(akvcam_color_convert_t self,
                                            AKVCAM_COLOR_MATRIX color_matrix,
                                            int ibitsa,
                                            int ibitsb,
                                            int ibitsc,
                                            int obitsx,
                                            int obitsy,
                                            int obitsz);
void akvcam_color_convert_load_alpha_matrix(akvcam_color_convert_t self,
                                            AKVCAM_VIDEO_FORMAT_TYPE format_type,
                                            int ibits_alpha,
                                            int obitsx,
                                            int obitsy,
                                            int obitsz);
void akvcam_color_convert_load_matrix(akvcam_color_convert_t self,
                                      akvcam_format_specs_ct from,
                                      akvcam_format_specs_ct to);
void akvcam_color_convert_load_matrix_from_fixel_formats(akvcam_color_convert_t self,
                                                         __u32 from,
                                                         __u32 to);

static inline void akvcam_color_convert_apply_matrix(akvcam_color_convert_ct self,
                                                     int64_t a, int64_t b, int64_t c,
                                                     int64_t *x, int64_t *y, int64_t *z)
{
    *x = akvcam_bound(self->xmin, (a * self->m00 + b * self->m01 + c * self->m02 + self->m03) >> self->color_shift, self->xmax);
    *y = akvcam_bound(self->ymin, (a * self->m10 + b * self->m11 + c * self->m12 + self->m13) >> self->color_shift, self->ymax);
    *z = akvcam_bound(self->zmin, (a * self->m20 + b * self->m21 + c * self->m22 + self->m23) >> self->color_shift, self->zmax);
}

static inline void akvcam_color_convert_apply_vector(akvcam_color_convert_ct self,
                                                     int64_t a, int64_t b, int64_t c,
                                                     int64_t *x, int64_t *y, int64_t *z)
{
    *x = (a * self->m00 + self->m03) >> self->color_shift;
    *y = (b * self->m11 + self->m13) >> self->color_shift;
    *z = (c * self->m22 + self->m23) >> self->color_shift;
}

static inline void akvcam_color_convert_apply_point_1_3(akvcam_color_convert_ct self,
                                                        int64_t p,
                                                        int64_t *x, int64_t *y, int64_t *z)
{
    *x = (p * self->m00 + self->m03) >> self->color_shift;
    *y = (p * self->m10 + self->m13) >> self->color_shift;
    *z = (p * self->m20 + self->m23) >> self->color_shift;
}

static inline void akvcam_color_convert_apply_point_3_1(akvcam_color_convert_ct self,
                                                        int64_t a, int64_t b, int64_t c,
                                                        int64_t *p)
{
    *p = akvcam_bound(self->xmin, (a * self->m00 + b * self->m01 + c * self->m02 + self->m03) >> self->color_shift, self->xmax);
}

static inline void akvcam_color_convert_apply_point_1_1(akvcam_color_convert_ct self,
                                                        int64_t p, int64_t *q)
{
    *q = (p * self->m00 + self->m03) >> self->color_shift;
}

static inline void akvcam_color_convert_apply_alpha_3_3(akvcam_color_convert_ct self,
                                                        int64_t x, int64_t y, int64_t z, int64_t a,
                                                        int64_t *xa, int64_t *ya, int64_t *za)
{
    *xa = akvcam_bound(self->xmin, (a * (x * self->a00 + self->a01) + self->a02) >> self->alpha_shift, self->xmax);
    *ya = akvcam_bound(self->ymin, (a * (y * self->a10 + self->a11) + self->a12) >> self->alpha_shift, self->ymax);
    *za = akvcam_bound(self->zmin, (a * (z * self->a20 + self->a21) + self->a22) >> self->alpha_shift, self->zmax);
}

static inline void akvcam_color_convert_apply_alpha_1_3(akvcam_color_convert_ct self,
                                                        int64_t a,
                                                        int64_t *x, int64_t *y, int64_t *z)
{
    akvcam_color_convert_apply_alpha_3_3(self, *x, *y, *z, a, x, y, z);
}

static inline void akvcam_color_convert_apply_alpha_1_1(akvcam_color_convert_ct self,
                                                        int64_t p, int64_t a, int64_t *pa)
{
    *pa = akvcam_bound(self->ymin, (a * (p * self->a00 + self->a01) + self->a02) >> self->alpha_shift, self->ymax);
}

static inline void akvcam_color_convert_apply_alpha_1(akvcam_color_convert_ct self,
                                                      int64_t a, int64_t *p)
{
    akvcam_color_convert_apply_alpha_1_1(self, *p, a, p);
}

static inline void akvcam_color_convert_read_matrix(akvcam_color_convert_ct self,
                                                    int64_t *color_matrix,
                                                    int64_t *alpha_matrix,
                                                    int64_t *min_values,
                                                    int64_t *max_values,
                                                    int64_t *color_shift,
                                                    int64_t *alpha_shift)
{
    // Copy the color matrix (3x4, including offsets)

    if (color_matrix) {
        color_matrix[0]  = self->m00;
        color_matrix[1]  = self->m01;
        color_matrix[2]  = self->m02;
        color_matrix[3]  = self->m03;
        color_matrix[4]  = self->m10;
        color_matrix[5]  = self->m11;
        color_matrix[6]  = self->m12;
        color_matrix[7]  = self->m13;
        color_matrix[8]  = self->m20;
        color_matrix[9]  = self->m21;
        color_matrix[10] = self->m22;
        color_matrix[11] = self->m23;
    }

    // Copy the alpha matrix (3x3)

    if (alpha_matrix) {
        alpha_matrix[0] = self->a00;
        alpha_matrix[1] = self->a01;
        alpha_matrix[2] = self->a02;
        alpha_matrix[3] = self->a10;
        alpha_matrix[4] = self->a11;
        alpha_matrix[5] = self->a12;
        alpha_matrix[6] = self->a20;
        alpha_matrix[7] = self->a21;
        alpha_matrix[8] = self->a22;
    }

    // Copy limits

    if (min_values) {
        min_values[0] = self->xmin;
        min_values[1] = self->ymin;
        min_values[2] = self->zmin;
    }

    if (max_values) {
        max_values[0] = self->xmax;
        max_values[1] = self->ymax;
        max_values[2] = self->zmax;
    }

    // Copy shifts

    if (color_shift)
        *color_shift = self->color_shift;

    if (alpha_shift)
        *alpha_shift = self->alpha_shift;
}

#endif // AKVCAM_COLOR_CONVERT_H
