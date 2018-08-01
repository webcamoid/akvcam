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

#ifndef AKVCAM_QUEUE_H
#define AKVCAM_QUEUE_H

struct akvcam_queue;
struct akvcam_device;
typedef struct akvcam_queue *akvcam_queue_t;

// public
akvcam_queue_t akvcam_queue_new(struct akvcam_device *device);
void akvcam_queue_delete(akvcam_queue_t *self);
struct vb2_queue *akvcam_queue_vb2_queue(akvcam_queue_t self);
struct mutex *akvcam_queue_mutex(akvcam_queue_t self);

#endif // AKVCAM_QUEUE_H
