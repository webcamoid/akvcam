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
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "rbuffer.h"
#include "object.h"
#include "utils.h"

struct akvcam_rbuffer
{
    akvcam_object_t self;
    char *data;
    size_t size;
    size_t data_size;
    size_t read;
    size_t write;
    size_t step;
    AKVCAM_RBUFFER_MEMORY_TYPE memory_type;
};

void *akvcam_rbuffer_alloc(AKVCAM_RBUFFER_MEMORY_TYPE memory_type, size_t size);
void akvcam_rbuffer_free(AKVCAM_RBUFFER_MEMORY_TYPE memory_type, void *data);

akvcam_rbuffer_t akvcam_rbuffer_new(void)
{
    akvcam_rbuffer_t self = kzalloc(sizeof(struct akvcam_rbuffer), GFP_KERNEL);
    self->self = akvcam_object_new(self,
                                   (akvcam_deleter_t) akvcam_rbuffer_delete);
    self->memory_type = AKVCAM_RBUFFER_MEMORY_TYPE_KMALLOC;

    return self;
}

void akvcam_rbuffer_delete(akvcam_rbuffer_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    if ((*self)->data)
        akvcam_rbuffer_free((*self)->memory_type, (*self)->data);

    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

void akvcam_rbuffer_copy(akvcam_rbuffer_t self, akvcam_rbuffer_t other)
{
    if (self->data)
        akvcam_rbuffer_free(self->memory_type, self->data);

    self->data = kzalloc(other->size, GFP_KERNEL);
    memcpy(self->data, other->data, other->size);
    self->size = other->size;
    self->data_size = other->data_size;
    self->read = other->read;
    self->write = other->write;
    self->step = other->step;
}

void akvcam_rbuffer_resize(akvcam_rbuffer_t self,
                           size_t n_elements,
                           size_t element_size,
                           AKVCAM_RBUFFER_MEMORY_TYPE memory_type)
{
    char *new_data;
    size_t new_size = n_elements * element_size;
    size_t data_size = akvcam_min(self->data_size, new_size);
    size_t left_size;

    if (new_size == self->size)
        return;

    if (new_size < 1) {
        if (self->data) {
            akvcam_rbuffer_free(self->memory_type, self->data);
            self->data = NULL;
        }

        self->size = 0;
        self->read = 0;
        self->write = 0;
        self->step = 0;
        self->memory_type = memory_type;

        return;
    }

    new_data = akvcam_rbuffer_alloc(memory_type, new_size);

    if (self->data) {
        left_size = akvcam_min(self->size - self->read, data_size);

        if (left_size > 0)
            memcpy(new_data, self->data + self->read, left_size);

        if (data_size > left_size)
            memcpy(new_data + left_size, self->data, data_size - left_size);

        akvcam_rbuffer_free(self->memory_type, self->data);
    }

    self->data = new_data;
    self->size = new_size;
    self->data_size = data_size;
    self->step = element_size;
    self->read = 0;
    self->write = data_size % new_size;
    self->memory_type = memory_type;
}

size_t akvcam_rbuffer_size(akvcam_rbuffer_t self)
{
    return self->size;
}

size_t akvcam_rbuffer_data_size(akvcam_rbuffer_t self)
{
    return self->data_size;
}

size_t akvcam_rbuffer_n_elements(akvcam_rbuffer_t self)
{
    if (self->step < 1)
        return 0;

    return self->size / self->step;
}

size_t akvcam_rbuffer_element_size(akvcam_rbuffer_t self)
{
    return self->step;
}

size_t akvcam_rbuffer_n_data(akvcam_rbuffer_t self)
{
    if (self->step < 1)
        return 0;

    return self->data_size / self->step;
}

void *akvcam_rbuffer_queue(akvcam_rbuffer_t self, const void *data)
{
    return akvcam_rbuffer_queue_bytes(self, data, self->step);
}

void *akvcam_rbuffer_queue_bytes(akvcam_rbuffer_t self,
                                 const void *data,
                                 size_t size)
{
    size_t right_size;
    void *output_data;
    bool move_read;

    if (self->size < 1)
        return NULL;

    size = akvcam_min(size, self->size);
    output_data = self->data + self->write;

    if (data) {
        right_size = akvcam_min(self->size - self->write, size);

        if (right_size > 0)
            memcpy(output_data, data, right_size);

        if (size > right_size)
            memcpy(self->data,
                   (const char *) data + right_size,
                   size - right_size);
    }

    move_read =
            akvcam_between(self->write, self->read, self->write + size - 1)
            && self->data_size > 0;
    self->write = (self->write + size) % self->size;
    self->data_size = akvcam_min(self->data_size + size, self->size);

    if (move_read)
        self->read = self->write;

    return output_data;
}

void *akvcam_rbuffer_dequeue(akvcam_rbuffer_t self,
                             void *data,
                             bool keep)
{
    size_t size = self->step;

    return akvcam_rbuffer_dequeue_bytes(self, data, &size, keep);
}

void *akvcam_rbuffer_dequeue_bytes(akvcam_rbuffer_t self,
                                   void *data,
                                   size_t *size,
                                   bool keep)
{
    void *input_data;
    size_t left_size;

    if (self->data_size < 1)
        return NULL;

    *size = akvcam_min(*size, self->data_size);
    input_data = self->data + self->read;

    if (data) {
        left_size = akvcam_min(self->size - self->read, *size);

        if (left_size > 0)
            memcpy(data, input_data, left_size);

        if (*size > left_size)
            memcpy((char *) data + left_size,
                   self->data,
                   *size - left_size);
    }

    self->read = (self->read + *size) % self->size;

    if (!keep)
        self->data_size -= *size;

    return input_data;
}

void akvcam_rbuffer_clear(akvcam_rbuffer_t self)
{
    self->data_size = 0;
    self->read = 0;
    self->write = 0;
}

void *akvcam_rbuffer_ptr_at(akvcam_rbuffer_t self, size_t i)
{
    size_t offset = i * self->step;

    if (!self->data || offset >= self->size)
        return NULL;

    offset = (self->read + offset) % self->size;

    return self->data + offset;
}

void *akvcam_rbuffer_ptr_front(akvcam_rbuffer_t self)
{
    if (!self->data || self->data_size < 1)
        return NULL;

    return self->data + self->read;
}

void *akvcam_rbuffer_ptr_back(akvcam_rbuffer_t self)
{
    size_t offset;

    if (!self->data || self->size < 1)
        return NULL;

    offset = (self->read + self->size - self->step) % self->size;

    return self->data + offset;
}

void *akvcam_rbuffer_find(akvcam_rbuffer_t self,
                          const void *data,
                          size_t size,
                          akvcam_are_equals_t equals)
{
    size_t i;
    size_t offset;

    for (i = 0; i < self->data_size; i += self->step) {
        offset = (self->read + i) % self->size;

        if (equals) {
            if (equals(self->data + offset, data, size))
                return self->data + offset;
        } else if (size) {
            if (memcmp(self->data + offset, data, size) == 0)
                return self->data + offset;
        } else {
            if (self->data + offset == data)
                return self->data + offset;
        }
    }

    return NULL;
}

void *akvcam_rbuffer_alloc(AKVCAM_RBUFFER_MEMORY_TYPE memory_type, size_t size)
{
    if (memory_type == AKVCAM_RBUFFER_MEMORY_TYPE_KMALLOC)
        return kzalloc(size, GFP_KERNEL);

    return vzalloc(size);
}

void akvcam_rbuffer_free(AKVCAM_RBUFFER_MEMORY_TYPE memory_type, void *data)
{
    if (memory_type == AKVCAM_RBUFFER_MEMORY_TYPE_KMALLOC)
        kfree(data);
    else
        vfree(data);
}
