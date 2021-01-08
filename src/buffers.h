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
#include "utils.h"

enum v4l2_buf_type;
struct vb2_queue;

akvcam_buffers_t akvcam_buffers_new(AKVCAM_RW_MODE rw_mode,
                                    enum v4l2_buf_type type);
void akvcam_buffers_delete(akvcam_buffers_t self);
akvcam_buffers_t akvcam_buffers_ref(akvcam_buffers_t self);

akvcam_format_t akvcam_buffers_format(akvcam_buffers_ct self);
void akvcam_buffers_set_format(akvcam_buffers_t self, akvcam_format_ct format);
size_t akvcam_buffers_count(akvcam_buffers_ct self);
void akvcam_buffers_set_count(akvcam_buffers_t self, size_t nbuffers);
akvcam_frame_t akvcam_buffers_read_frame(akvcam_buffers_t self);
int akvcam_buffers_write_frame(akvcam_buffers_t self, akvcam_frame_t frame);
struct vb2_queue *akvcam_buffers_vb2_queue(akvcam_buffers_t self);

// signals
akvcam_signal_no_args(buffers, streaming_started);
akvcam_signal_no_args(buffers, streaming_stopped);

#endif // AKVCAM_BUFFERS_H
