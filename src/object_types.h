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

#ifndef AKVCAM_OBJECT_TYPES_H
#define AKVCAM_OBJECT_TYPES_H

#define AKVCAM_TO_OBJECT(obj) (((akvcam_object_private_t) obj)->self)

typedef void (*akvcam_deleter_t)(void **object);
struct akvcam_object;
typedef struct akvcam_object *akvcam_object_t;

typedef struct
{
    akvcam_object_t self;
} *akvcam_object_private_t;

#endif // AKVCAM_OBJECT_TYPES_H
