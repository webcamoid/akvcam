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

#ifndef AKVCAM_BUFFER_H
#define AKVCAM_BUFFER_H

#include <linux/types.h>

struct akvcam_buffer;
typedef struct akvcam_buffer *akvcam_buffer_t;
struct v4l2_buffer;

// public
akvcam_buffer_t akvcam_buffer_new(size_t size);
void akvcam_buffer_delete(akvcam_buffer_t self);
akvcam_buffer_t akvcam_buffer_ref(akvcam_buffer_t self);

struct v4l2_buffer *akvcam_buffer_get(akvcam_buffer_t self);
void *akvcam_buffer_data(akvcam_buffer_t self);
size_t akvcam_buffer_size(akvcam_buffer_t self);
__u32 akvcam_buffer_offset(akvcam_buffer_t self);
void akvcam_buffer_set_offset(akvcam_buffer_t self, __u32 offset);

#endif // AKVCAM_BUFFER_H
