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

#ifndef AKVCAM_FORMAT_TYPES_H
#define AKVCAM_FORMAT_TYPES_H

#include  "list_types.h"

typedef akvcam_list_tt(struct v4l2_frmsize_discrete) akvcam_resolutions_list_t;
typedef akvcam_list_ctt(struct v4l2_frmsize_discrete) akvcam_resolutions_list_ct;
typedef akvcam_list_tt(struct v4l2_fract) akvcam_fps_list_t;
typedef akvcam_list_ctt(struct v4l2_fract) akvcam_fps_list_ct;
typedef akvcam_list_tt(__u32) akvcam_pixel_formats_list_t;
typedef akvcam_list_ctt(__u32) akvcam_pixel_formats_list_ct;

struct akvcam_format;
typedef struct akvcam_format *akvcam_format_t;
typedef const struct akvcam_format *akvcam_format_ct;

typedef akvcam_list_tt(akvcam_format_t) akvcam_formats_list_t;
typedef akvcam_list_ctt(akvcam_format_t) akvcam_formats_list_ct;

#endif // AKVCAM_FORMAT_TYPES_H
