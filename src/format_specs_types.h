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

#ifndef AKVCAM_FORMAT_SPEC_TYPES_H
#define AKVCAM_FORMAT_SPEC_TYPES_H

#include <linux/types.h>

typedef enum
{
    AKVCAM_VIDEO_FORMAT_TYPE_UNKNOWN,
    AKVCAM_VIDEO_FORMAT_TYPE_RGB,
    AKVCAM_VIDEO_FORMAT_TYPE_YUV,
    AKVCAM_VIDEO_FORMAT_TYPE_GRAY
} AKVCAM_VIDEO_FORMAT_TYPE;

typedef enum
{
    AKVCAM_COMPONENT_TYPE_UNKNOWN,
    AKVCAM_COMPONENT_TYPE_R,
    AKVCAM_COMPONENT_TYPE_G,
    AKVCAM_COMPONENT_TYPE_B,
    AKVCAM_COMPONENT_TYPE_Y,
    AKVCAM_COMPONENT_TYPE_U,
    AKVCAM_COMPONENT_TYPE_V,
    AKVCAM_COMPONENT_TYPE_A
} AKVCAM_COMPONENT_TYPE;

#define MAX_PLANES 4
#define MAX_COMPONENTS 4

typedef struct
{
    AKVCAM_COMPONENT_TYPE type;
    size_t step;
    size_t offset;
    size_t shift;
    size_t byte_depth;
    size_t depth;
    size_t width_div;
    size_t height_div;
} akvcam_color_component;

typedef akvcam_color_component *akvcam_color_component_t;
typedef const akvcam_color_component *akvcam_color_component_ct;

typedef struct
{
    size_t ncomponents;
    akvcam_color_component components[MAX_COMPONENTS];
    size_t bits_size;
} akvcam_plane;

typedef akvcam_plane *akvcam_plane_t;
typedef const akvcam_plane *akvcam_plane_ct;

typedef struct
{
    __u32 fourcc;
    const char name[32];
    AKVCAM_VIDEO_FORMAT_TYPE type;
    int endianness;
    size_t nplanes;
    akvcam_plane planes[MAX_PLANES];
} akvcam_format_specs;

typedef akvcam_format_specs *akvcam_format_specs_t;
typedef const akvcam_format_specs *akvcam_format_specs_ct;

#endif // AKVCAM_FORMAT_SPEC_TYPES_H
