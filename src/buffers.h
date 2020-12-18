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

#include <linux/types.h>

#include "buffers_types.h"
#include "device_types.h"
#include "format_types.h"
#include "frame_types.h"
#include "node_types.h"

#define AKVCAM_BUFFERS_MIN 4

enum v4l2_buf_type;
struct v4l2_buffer;
struct v4l2_requestbuffers;
struct v4l2_create_buffers;
struct v4l2_event;
struct vm_area_struct;

akvcam_buffers_t akvcam_buffers_new(AKVCAM_RW_MODE rw_mode,
                                    enum v4l2_buf_type type,
                                    bool multiplanar);
void akvcam_buffers_delete(akvcam_buffers_t self);
akvcam_buffers_t akvcam_buffers_ref(akvcam_buffers_t self);

akvcam_format_t akvcam_buffers_format(akvcam_buffers_t self);
void akvcam_buffers_set_format(akvcam_buffers_t self, akvcam_format_t format);
int akvcam_buffers_allocate(akvcam_buffers_t self,
                            akvcam_node_t node,
                            struct v4l2_requestbuffers *params);
void akvcam_buffers_deallocate(akvcam_buffers_t self, akvcam_node_t node);
int akvcam_buffers_create(akvcam_buffers_t self,
                          akvcam_node_t node,
                          struct v4l2_create_buffers *buffers,
                          akvcam_format_t format);
bool akvcam_buffers_fill(const akvcam_buffers_t self,
                         struct v4l2_buffer *buffer);
int akvcam_buffers_queue(akvcam_buffers_t self, struct v4l2_buffer *buffer);
int akvcam_buffers_dequeue(akvcam_buffers_t self, struct v4l2_buffer *buffer);
int akvcam_buffers_data_map(const akvcam_buffers_t self,
                            __u32 offset,
                            struct vm_area_struct *vma);
bool akvcam_buffers_allocated(const akvcam_buffers_t self);
size_t akvcam_buffers_size_rw(const akvcam_buffers_t self);
bool akvcam_buffers_resize_rw(akvcam_buffers_t self, size_t size);
ssize_t akvcam_buffers_read(akvcam_buffers_t self,
                            akvcam_node_t node,
                            void __user *data,
                            size_t size);
ssize_t akvcam_buffers_write(akvcam_buffers_t self,
                             akvcam_node_t node,
                             const void __user *data,
                             size_t size);
akvcam_frame_t akvcam_buffers_read_frame(akvcam_buffers_t self);
bool akvcam_buffers_write_frame(akvcam_buffers_t self, akvcam_frame_t frame);
__u32 akvcam_buffers_sequence(akvcam_buffers_t self);
void akvcam_buffers_reset_sequence(akvcam_buffers_t self);

#endif // AKVCAM_BUFFERS_H
