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
typedef const struct akvcam_buffer *akvcam_buffer_ct;
struct v4l2_buffer;
struct vm_area_struct;

// public
akvcam_buffer_t akvcam_buffer_new(size_t size);
void akvcam_buffer_delete(akvcam_buffer_t self);
akvcam_buffer_t akvcam_buffer_ref(akvcam_buffer_t self);

int akvcam_buffer_read(akvcam_buffer_t self, struct v4l2_buffer *v4l2_buff);
int akvcam_buffer_read_userptr(akvcam_buffer_t self,
                               struct v4l2_buffer *v4l2_buff);
int akvcam_buffer_write(akvcam_buffer_t self,
                        const struct v4l2_buffer *v4l2_buff);
int akvcam_buffer_write_userptr(akvcam_buffer_t self,
                                const struct v4l2_buffer *v4l2_buff);
bool akvcam_buffer_read_data(akvcam_buffer_t self, void *data, size_t size);
bool akvcam_buffer_write_data(akvcam_buffer_t self,
                              const void *data,
                              size_t size);
int akvcam_buffer_map_data(akvcam_buffer_t self, struct vm_area_struct *vma);
bool akvcam_buffer_mapped(akvcam_buffer_t self);

#endif // AKVCAM_BUFFER_H
