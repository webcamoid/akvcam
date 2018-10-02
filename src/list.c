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

#include "list.h"

typedef struct akvcam_list_element
{
    void *data;
    size_t size;
    akvcam_deleter_t deleter;
    struct akvcam_list_element *prev;
    struct akvcam_list_element *next;
} akvcam_list_element, *akvcam_list_element_t;

struct akvcam_list
{
    akvcam_object_t self;
    size_t size;
    akvcam_list_element_t head;
    akvcam_list_element_t tail;
};

akvcam_list_t akvcam_list_new(void)
{
    akvcam_list_t self = kzalloc(sizeof(struct akvcam_list), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_list_delete);

    return self;
}

void akvcam_list_delete(akvcam_list_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_list_clear(*self);
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

size_t akvcam_list_size(const akvcam_list_t self)
{
    if (!self)
        return 0;

    return self->size;
}

bool akvcam_list_empty(const akvcam_list_t self)
{
    if (!self)
        return true;

    return self->size < 1;
}

void *akvcam_list_at(const akvcam_list_t self, size_t i)
{
    akvcam_list_element_t element;
    size_t e;

    if (!self)
        return NULL;

    if (i >= self->size || self->size < 1)
        return NULL;

    if (i == 0) {
        element = self->head;
    } else if (i == self->size - 1) {
        element = self->tail;
    } else {
        element = self->head;

        for (e = 0; e < i; e++)
            element = element->next;
    }

    return element->data;
}

void *akvcam_list_front(const akvcam_list_t self)
{
    if (!self || self->size < 1)
        return NULL;

    return self->head->data;
}

void *akvcam_list_back(const akvcam_list_t self)
{
    if (!self || self->size < 1)
        return NULL;

    return self->tail->data;
}

akvcam_list_element_t akvcam_list_push_back(akvcam_list_t self,
                                            void *data,
                                            size_t data_size,
                                            const akvcam_deleter_t deleter,
                                            bool copy)
{
    akvcam_list_element_t element;

    if (!self)
        return NULL;

    element = kzalloc(sizeof(struct akvcam_list_element), GFP_KERNEL);

    if (!element) {
        akvcam_set_last_error(-ENOMEM);

        return NULL;
    }

    if (copy && data_size > 1)
        element->data = kmemdup(data, data_size, GFP_KERNEL);
    else
        element->data = data;

    element->size = data_size;
    element->deleter = deleter;
    element->prev = self->tail;
    self->size++;

    if (self->tail) {
        self->tail->next = element;
        self->tail = element;
    } else {
        self->head = element;
        self->tail = element;
    }

    akvcam_set_last_error(0);

    return element;
}

akvcam_list_element_t akvcam_list_it(akvcam_list_t self, size_t i)
{
    akvcam_list_element_t element;
    size_t e;

    if (!self)
        return NULL;

    if (i >= self->size || self->size < 1)
        return NULL;

    if (i == 0)
        return self->head;

    if (i == self->size - 1)
        return self->tail;

    element = self->head;

    for (e = 0; e < i; e++)
        element = element->next;

    return element;
}

void akvcam_list_erase(akvcam_list_t self, const akvcam_list_element_t element)
{
    akvcam_list_element_t it;

    for (it = self->head; it != NULL; it = it->next)
        if (it == element) {
            if (it->data && it->deleter)
                it->deleter(&it->data);

            if (it->prev)
                it->prev->next = it->next;
            else
                self->head = it->next;

            if (it->next)
                it->next->prev = it->prev;
            else
                self->tail = it->prev;

            kfree(it);
            self->size--;

            break;
        }
}

void akvcam_list_clear(akvcam_list_t self)
{
    akvcam_list_element_t element;
    akvcam_list_element_t next;

    if (!self)
        return;

    element = self->head;

    while (element) {
        if (element->data && element->deleter)
            element->deleter(&element->data);

        next = element->next;
        kfree(element);
        element = next;
    }

    self->size = 0;
    self->head = NULL;
    self->tail = NULL;
}

akvcam_list_element_t akvcam_list_find(const akvcam_list_t self,
                                       const void *data,
                                       size_t size,
                                       const akvcam_are_equals_t equals)
{
    akvcam_list_element_t element;
    ssize_t i;

    if (!self || self->size < 1 || !(data || size || equals))
        return NULL;

    if (equals) {
        if (equals(self->tail->data, data, size))
            return self->tail;
    } else if (size) {
        if (memcmp(self->tail->data, data, size) == 0)
            return self->tail;
    } else {
        if (self->tail->data == data)
            return self->tail;
    }

    element = self->head;

    for (i = 0; i < (ssize_t) self->size - 1; i++) {
        if (equals) {
            if (equals(element->data, data, size))
                return element;
        } else if (size) {
            if (memcmp(element->data, data, size) == 0)
                return element;
        } else {
            if (element->data == data)
                return element;
        }

        element = element->next;
    }

    return NULL;
}

ssize_t akvcam_list_index_of(const akvcam_list_t self,
                             const void *data,
                             size_t size,
                             const akvcam_are_equals_t equals)
{
    akvcam_list_element_t element;
    ssize_t i;

    if (!self || self->size < 1 || !(data || size || equals))
        return -1;

    if (equals) {
        if (equals(self->tail->data, data, size))
            return (ssize_t) self->size - 1;
    } else if (size) {
        if (memcmp(self->tail->data, data, size) == 0)
            return (ssize_t) self->size - 1;
    } else {
        if (self->tail->data == data)
            return (ssize_t) self->size - 1;
    }

    element = self->head;

    for (i = 0; i < (ssize_t) self->size - 1; i++) {
        if (equals) {
            if (equals(element->data, data, size))
                return i;
        } else if (size) {
            if (memcmp(element->data, data, size) == 0)
                return i;
        } else {
            if (element->data == data)
                return i;
        }

        element = element->next;
    }

    return -1;
}

bool akvcam_list_contains(const akvcam_list_t self,
                          const void *data,
                          size_t size,
                          const akvcam_are_equals_t equals)
{
    return akvcam_list_index_of(self, data, size, equals) >= 0;
}

void *akvcam_list_next(const akvcam_list_t self,
                       akvcam_list_element_t *element)
{
    if (!self || !element)
        return NULL;

    if (*element) {
        *element = (*element)->next;

        return *element? (*element)->data: NULL;
    }

    *element = self->head;

    return self->head? self->head->data: NULL;
}

void *akvcam_list_element_data(const akvcam_list_element_t element)
{
    if (!element)
        return NULL;

    return element->data;
}

size_t akvcam_list_element_size(const akvcam_list_element_t element)
{
    if (!element)
        return 0;

    return element->size;
}

akvcam_deleter_t akvcam_list_element_deleter(const akvcam_list_element_t element)
{
    if (!element)
        return NULL;

    return element->deleter;
}
