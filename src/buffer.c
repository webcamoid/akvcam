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

#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "buffer.h"

struct akvcam_buffer
{
    struct kref ref;
    struct v4l2_buffer buffer;
    void *data;
    size_t size;
    __u32 offset;
};

akvcam_buffer_t akvcam_buffer_new(size_t size)
{
    akvcam_buffer_t self = kzalloc(sizeof(struct akvcam_buffer), GFP_KERNEL);
    kref_init(&self->ref);
    self->data = vzalloc(size);
    self->size = size;

    return self;
}

void akvcam_buffer_free(struct kref *ref)
{
    akvcam_buffer_t self = container_of(ref, struct akvcam_buffer, ref);
    vfree(self->data);
    kfree(self);
}

void akvcam_buffer_delete(akvcam_buffer_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_buffer_free);
}

akvcam_buffer_t akvcam_buffer_ref(akvcam_buffer_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

struct v4l2_buffer *akvcam_buffer_get(akvcam_buffer_t self)
{
    return &self->buffer;
}

void *akvcam_buffer_data(akvcam_buffer_t self)
{
    if (!self)
        return NULL;

    return self->data;
}

size_t akvcam_buffer_size(akvcam_buffer_t self)
{
    return self->size;
}

__u32 akvcam_buffer_offset(akvcam_buffer_t self)
{
    return self->offset;
}

void akvcam_buffer_set_offset(akvcam_buffer_t self, __u32 offset)
{
    self->offset = offset;
}
