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

#include "buffers.h"
#include "object.h"

struct akvcam_buffers
{
    akvcam_object_t self;
    struct v4l2_requestbuffers params;
    enum v4l2_buf_type type;
};

akvcam_buffers_t akvcam_buffers_new(enum v4l2_buf_type type)
{
    akvcam_buffers_t self = kzalloc(sizeof(struct akvcam_buffers), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_buffers_delete);
    self->type = type;

    return self;
}

void akvcam_buffers_delete(akvcam_buffers_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

bool akvcam_buffers_allocate(akvcam_buffers_t self,
                             struct v4l2_requestbuffers *params)
{
    if (params->memory != V4L2_MEMORY_MMAP
        && params->memory != V4L2_MEMORY_USERPTR)
        return false;

    if (params->count < 1)
        params->count = 1;

    params->type = self->type;
    memset(params->reserved, 0, 2 * sizeof(__u32));
    memcpy(&self->params, params, sizeof(struct v4l2_requestbuffers));

    return params;
}
