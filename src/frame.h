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

#ifndef AKVCAM_FRAME_H
#define AKVCAM_FRAME_H

#include <linux/types.h>

struct akvcam_frame;
typedef struct akvcam_frame *akvcam_frame_t;
struct akvcam_format;

akvcam_frame_t akvcam_frame_new(struct akvcam_format *format,
                                const void *data,
                                size_t size);
void akvcam_frame_delete(akvcam_frame_t *self);

void akvcam_frame_copy(akvcam_frame_t self, akvcam_frame_t other);
struct akvcam_format *akvcam_frame_format_nr(akvcam_frame_t self);
struct akvcam_format *akvcam_frame_format(akvcam_frame_t self);
void *akvcam_frame_data(akvcam_frame_t self);
size_t akvcam_frame_size(akvcam_frame_t self);
void akvcam_frame_resize(akvcam_frame_t self, size_t size);
void akvcam_frame_clear(akvcam_frame_t self);

#endif // AKVCAM_FRAME_H
