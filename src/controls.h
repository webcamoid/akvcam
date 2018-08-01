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

#ifndef AKVCAM_CONTROLS_H
#define AKVCAM_CONTROLS_H

#include <linux/types.h>

struct akvcam_controls;
struct v4l2_ctrl_handler;
typedef struct akvcam_controls *akvcam_controls_t;

// public
akvcam_controls_t akvcam_controls_new(void);
void akvcam_controls_delete(akvcam_controls_t *self);
size_t akvcam_controls_count(akvcam_controls_t self);
struct v4l2_ctrl_handler *akvcam_controls_handler(akvcam_controls_t self);

#endif // AKVCAM_CONTROLS_H
