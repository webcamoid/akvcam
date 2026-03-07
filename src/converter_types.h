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

#ifndef AKVCAM_CONVERTER_TYPES_H
#define AKVCAM_CONVERTER_TYPES_H

typedef enum
{
    AKVCAM_SCALING_MODE_FAST,
    AKVCAM_SCALING_MODE_LINEAR
} AKVCAM_SCALING_MODE;

typedef enum
{
    AKVCAM_ASPECT_RATIO_MODE_IGNORE,
    AKVCAM_ASPECT_RATIO_MODE_KEEP,
    AKVCAM_ASPECT_RATIO_MODE_EXPANDING,
    AKVCAM_ASPECT_RATIO_MODE_FIT,
} AKVCAM_ASPECT_RATIO_MODE;

struct akvcam_converter;
typedef struct akvcam_converter *akvcam_converter_t;
typedef const struct akvcam_converter *akvcam_converter_ct;

#endif // AKVCAM_CONVERTER_TYPES_H
