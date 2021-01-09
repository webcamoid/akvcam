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

#include <linux/videodev2.h>

#include "controls_types.h"
#include "device_types.h"
#include "utils.h"

// public
akvcam_controls_t akvcam_controls_new(AKVCAM_DEVICE_TYPE device_type);
void akvcam_controls_delete(akvcam_controls_t self);
akvcam_controls_t akvcam_controls_ref(akvcam_controls_t self);

__s32 akvcam_controls_value(akvcam_controls_t self, __u32 id);
const char *akvcam_controls_string_value(akvcam_controls_t self, __u32 id);
int akvcam_controls_set_value(akvcam_controls_t self, __u32 id, __s32 value);
int akvcam_controls_set_string_value(akvcam_controls_t self, __u32 id, const char *value);
struct v4l2_ctrl_handler *akvcam_controls_handler(akvcam_controls_t self);

// signals
akvcam_signal(controls, updated, __u32 id, __s32 value);

#endif // AKVCAM_CONTROLS_H
