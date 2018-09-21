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

#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "frame.h"
#include "format.h"
#include "object.h"

struct akvcam_frame
{
    akvcam_object_t self;
    akvcam_format_t format;
    void *data;
    size_t size;
};

akvcam_frame_t akvcam_frame_new(struct akvcam_format *format,
                                const void *data,
                                size_t size)
{
    akvcam_frame_t self = kzalloc(sizeof(struct akvcam_frame), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_frame_delete);
    self->format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(self->format, format);

    if (size < 1)
        size = akvcam_format_size(format);

    self->size = size;

    if (size > 0) {
        self->data = vzalloc(size);

        if (data)
            memcpy(self->data, data, size);
    }

    return self;
}

void akvcam_frame_delete(akvcam_frame_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    if ((*self)->data)
        vfree((*self)->data);

    akvcam_format_delete(&((*self)->format));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

void akvcam_frame_copy(akvcam_frame_t self, const akvcam_frame_t other)
{
    akvcam_format_copy(self->format, other->format);
    self->size = other->size;

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    if (self->size > 0) {
        self->data = vzalloc(self->size);

        if (other->data)
            memcpy(self->data, other->data, other->size);
    }
}

struct akvcam_format *akvcam_frame_format_nr(const akvcam_frame_t self)
{
    return self->format;
}

struct akvcam_format *akvcam_frame_format(const akvcam_frame_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->format));

    return self->format;
}

void *akvcam_frame_data(const akvcam_frame_t self)
{
    return self->data;
}

size_t akvcam_frame_size(const akvcam_frame_t self)
{
    return self->size;
}

void akvcam_frame_resize(akvcam_frame_t self, size_t size)
{
    if (size < 1)
        size = akvcam_format_size(self->format);

    self->size = size;

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    if (size > 0)
        self->data = vzalloc(size);
}

void akvcam_frame_clear(akvcam_frame_t self)
{
    akvcam_format_clear(self->format);

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    self->size = 0;
}

