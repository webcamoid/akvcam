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

#ifndef AKVCAM_COLOR_CONVERT_TYPES_H
#define AKVCAM_COLOR_CONVERT_TYPES_H

#include <linux/types.h>

typedef enum
{
    AKVCAM_YUV_COLOR_SPACE_AVG,
    AKVCAM_YUV_COLOR_SPACE_ITUR_BT601,
    AKVCAM_YUV_COLOR_SPACE_ITUR_BT709,
    AKVCAM_YUV_COLOR_SPACE_ITUR_BT2020,
    AKVCAM_YUV_COLOR_SPACE_SMPTE_240M
} AKVCAM_YUV_COLOR_SPACE;

typedef enum
{
    AKVCAM_YUV_COLOR_SPACE_TYPE_STUDIO_SWING,
    AKVCAM_YUV_COLOR_SPACE_TYPE_FULL_SWING
} AKVCAM_YUV_COLOR_SPACE_TYPE;

typedef enum
{
    AKVCAM_COLOR_MATRIX_ABC2XYZ,
    AKVCAM_COLOR_MATRIX_RGB2YUV,
    AKVCAM_COLOR_MATRIX_YUV2RGB,
    AKVCAM_COLOR_MATRIX_RGB2GRAY,
    AKVCAM_COLOR_MATRIX_GRAY2RGB,
    AKVCAM_COLOR_MATRIX_YUV2GRAY,
    AKVCAM_COLOR_MATRIX_GRAY2YUV
} AKVCAM_COLOR_MATRIX;

struct akvcam_color_convert_private;
typedef struct akvcam_color_convert_private *akvcam_color_convert_private_t;
typedef const struct akvcam_color_convert_private *akvcam_color_convert_private_ct;

struct akvcam_color_convert
{
    // Color matrix
    int64_t m00, m01, m02, m03;
    int64_t m10, m11, m12, m13;
    int64_t m20, m21, m22, m23;

    // Alpha matrix
    int64_t a00, a01, a02;
    int64_t a10, a11, a12;
    int64_t a20, a21, a22;

    int64_t xmin, xmax;
    int64_t ymin, ymax;
    int64_t zmin, zmax;

    int64_t color_shift;
    int64_t alpha_shift;

    akvcam_color_convert_private_t priv;
};

typedef struct akvcam_color_convert *akvcam_color_convert_t;
typedef const struct akvcam_color_convert *akvcam_color_convert_ct;

#endif // AKVCAM_COLOR_CONVERT_TYPES_H
