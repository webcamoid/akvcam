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

#ifndef AKVCAM_CONTROLS_TYPES_H
#define AKVCAM_CONTROLS_TYPES_H

#define AKVCAM_CONTROLS_FLAG_TRY    0x0
#define AKVCAM_CONTROLS_FLAG_GET    0x1
#define AKVCAM_CONTROLS_FLAG_SET    0x2
#define AKVCAM_CONTROLS_FLAG_KERNEL 0x4

struct akvcam_controls;
typedef struct akvcam_controls *akvcam_controls_t;

#endif // AKVCAM_CONTROLS_TYPES_H
