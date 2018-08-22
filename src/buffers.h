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

#ifndef AKVCAM_BUFFERS_H
#define AKVCAM_BUFFERS_H

#include <linux/videodev2.h>

struct akvcam_buffers;
typedef struct akvcam_buffers *akvcam_buffers_t;
struct v4l2_requestbuffers;

akvcam_buffers_t akvcam_buffers_new(enum v4l2_buf_type type);
void akvcam_buffers_delete(akvcam_buffers_t *self);

bool akvcam_buffers_allocate(akvcam_buffers_t self,
                             struct v4l2_requestbuffers *params);

#endif // AKVCAM_BUFFERS_H
