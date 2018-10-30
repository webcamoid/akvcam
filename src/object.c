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

#include "object.h"
#include "log.h"
#include "utils.h"

struct akvcam_object
{
    akvcam_object_t self;
    void *parent;
    akvcam_deleter_t deleter;
    char name[1024];
    int64_t ref;
};

void akvcam_delete_data(void **data)
{
    kfree(*data);
    *data = NULL;
}

akvcam_object_t akvcam_object_new(const char *name,
                                  void *parent,
                                  akvcam_deleter_t deleter)
{
    akvcam_object_t self = kzalloc(sizeof(struct akvcam_object), GFP_KERNEL);

    if (!self) {
        akvcam_set_last_error(-ENOMEM);

        goto akvcam_object_new_failed;
    }

    self->self = self;
    self->parent = parent;
    self->deleter = deleter;
    snprintf(self->name, 1024, "%s", name);
    self->ref = 1;
    akvcam_set_last_error(0);

    return self;

akvcam_object_new_failed:
    if (self)
        kfree(self);

    return NULL;
}

void akvcam_object_delete(akvcam_object_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref(*self) > 0)
        return;

    if ((*self)->parent && (*self)->deleter)
        (*self)->deleter((*self)->parent);

    kfree(*self);
    *self = NULL;
}

void akvcam_object_free(akvcam_object_t *self)
{
    if (!self || !*self)
        return;

    kfree(*self);
    *self = NULL;
}

int64_t akvcam_object_ref(akvcam_object_t self)
{
    return ++self->ref;
}

int64_t akvcam_object_unref(akvcam_object_t self)
{
    return --self->ref;
}

const char *akvcam_object_name(akvcam_object_t self)
{
    return self->name;
}
