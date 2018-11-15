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

#ifndef AKVCAM_ATTRIBUTES_H
#define AKVCAM_ATTRIBUTES_H

#include "device_types.h"

struct akvcam_attributes;
typedef struct akvcam_attributes *akvcam_attributes_t;
struct device;

// public
akvcam_attributes_t akvcam_attributes_new(akvcam_device_t device);
void akvcam_attributes_delete(akvcam_attributes_t *self);

void akvcam_attributes_set(akvcam_attributes_t self, struct device *dev);

#endif // AKVCAM_ATTRIBUTES_H
