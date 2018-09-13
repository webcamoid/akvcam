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

#include "utils.h"

#define AKVCAM_BUFFERS_MIN 4

struct akvcam_buffers;
typedef struct akvcam_buffers *akvcam_buffers_t;
struct akvcam_device;
struct akvcam_frame;
struct akvcam_node;
struct v4l2_buffer;
struct v4l2_requestbuffers;
struct v4l2_create_buffers;
struct v4l2_event;

akvcam_buffers_t akvcam_buffers_new(struct akvcam_device *device);
void akvcam_buffers_delete(akvcam_buffers_t *self);

int akvcam_buffers_allocate(akvcam_buffers_t self,
                            struct akvcam_node *node,
                            struct v4l2_requestbuffers *params);
void akvcam_buffers_deallocate(akvcam_buffers_t self, struct akvcam_node *node);
int akvcam_buffers_create(akvcam_buffers_t self,
                          struct akvcam_node *node,
                          struct v4l2_create_buffers *buffers);
bool akvcam_buffers_fill(akvcam_buffers_t self, struct v4l2_buffer *buffer);
int akvcam_buffers_queue(akvcam_buffers_t self, struct v4l2_buffer *buffer);
int akvcam_buffers_dequeue(akvcam_buffers_t self, struct v4l2_buffer *buffer);
void *akvcam_buffers_data(akvcam_buffers_t self, __u32 offset);
bool akvcam_buffers_allocated(akvcam_buffers_t self);
size_t akvcam_buffers_size(akvcam_buffers_t self);
bool akvcam_buffers_resize_rw(akvcam_buffers_t self, size_t size);
ssize_t akvcam_buffers_read_rw(akvcam_buffers_t self, void *data, size_t size);
bool akvcam_buffers_write_frame(akvcam_buffers_t self,
                                struct akvcam_frame *frame);
void akvcam_buffers_notify_frame(akvcam_buffers_t self);
void akvcam_buffers_reset_sequence(akvcam_buffers_t self);

// signals
AKVCAM_CALLBACK(frame_ready, struct v4l2_event *event)
void akvcam_buffers_set_frame_ready_callback(akvcam_buffers_t self,
                                             akvcam_frame_ready_callback callback);

#endif // AKVCAM_BUFFERS_H
