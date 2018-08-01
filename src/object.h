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

#ifndef AKVCAM_OBJECT_H
#define AKVCAM_OBJECT_H

#include <linux/types.h>

#define AKVCAM_TO_OBJECT(obj) (((akvcam_object_private_t) obj)->self)

typedef void (*akvcam_deleter_t)(void **object);
struct akvcam_object;
typedef struct akvcam_object *akvcam_object_t;

typedef struct
{
    akvcam_object_t self;
} *akvcam_object_private_t;

// public
void akvcam_delete_data(void **data);
akvcam_object_t akvcam_object_new(void *parent, akvcam_deleter_t deleter);
void akvcam_object_delete(akvcam_object_t *self);
void akvcam_object_free(akvcam_object_t *self);
int64_t akvcam_object_ref(akvcam_object_t self);
int64_t akvcam_object_unref(akvcam_object_t self);

#endif // AKVCAM_OBJECT_H
