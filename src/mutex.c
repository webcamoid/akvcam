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

#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "mutex.h"
#include "object.h"
#include "utils.h"

struct akvcam_mutex
{
    akvcam_object_t self;
    struct mutex mutex;
    spinlock_t slock;
    AKVCAM_MUTEX_MODE mode;
};

akvcam_mutex_t akvcam_mutex_new(AKVCAM_MUTEX_MODE mode)
{
    akvcam_mutex_t self = kzalloc(sizeof(struct akvcam_mutex), GFP_KERNEL);

    if (!self) {
        akvcam_set_last_error(-ENOMEM);

        goto akvcam_mutex_new_failed;
    }

    self->self =
            akvcam_object_new(self, (akvcam_deleter_t) akvcam_mutex_delete);

    if (!self->self)
        goto akvcam_mutex_new_failed;

    self->mode = mode;

    if (mode == AKVCAM_MUTEX_MODE_CONSERVATIVE)
        mutex_init(&self->mutex);
    else
        spin_lock_init(&self->slock);

    akvcam_set_last_error(0);

    return self;

akvcam_mutex_new_failed:
    if (self) {
        akvcam_object_free(&AKVCAM_TO_OBJECT(self));
        kfree(self);
    }

    return NULL;
}

void akvcam_mutex_delete(akvcam_mutex_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

void akvcam_mutex_lock(akvcam_mutex_t self)
{
    if (self->mode == AKVCAM_MUTEX_MODE_CONSERVATIVE)
        mutex_lock(&self->mutex);
    else
        spin_lock_bh(&self->slock);
}

void akvcam_mutex_unlock(akvcam_mutex_t self)
{
    if (self->mode == AKVCAM_MUTEX_MODE_CONSERVATIVE)
        mutex_unlock(&self->mutex);
    else
        spin_unlock_bh(&self->slock);
}
