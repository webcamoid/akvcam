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

#ifndef AKVCAM_NODE_H
#define AKVCAM_NODE_H

struct akvcam_node;
typedef struct akvcam_node *akvcam_node_t;
struct akvcam_device;
struct akvcam_format;
struct akvcam_buffers;
struct akvcam_events;

// public
akvcam_node_t akvcam_node_new(struct akvcam_device *device);
void akvcam_node_delete(akvcam_node_t *self);

struct akvcam_device *akvcam_node_device_nr(akvcam_node_t self);
struct akvcam_device *akvcam_node_device(akvcam_node_t self);
struct akvcam_format *akvcam_node_format_nr(akvcam_node_t self);
struct akvcam_format *akvcam_node_format(akvcam_node_t self);
struct akvcam_buffers *akvcam_node_buffers_nr(akvcam_node_t self);
struct akvcam_buffers *akvcam_node_buffers(akvcam_node_t self);
struct akvcam_events *akvcam_node_events_nr(akvcam_node_t self);
struct akvcam_events *akvcam_node_events(akvcam_node_t self);

// static
struct v4l2_file_operations *akvcam_node_fops(void);

#endif // AKVCAM_NODE_H
