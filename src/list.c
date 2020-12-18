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

#include "list.h"

typedef struct akvcam_list_element
{
    void *data;
    akvcam_copy_t copier;
    akvcam_delete_t deleter;
    struct akvcam_list_element *prev;
    struct akvcam_list_element *next;
} akvcam_list_element, *akvcam_list_element_t;

struct akvcam_list
{
    struct kref ref;
    size_t size;
    akvcam_list_element_t head;
    akvcam_list_element_t tail;
};

void akvcam_matrix_combine_p(akvcam_matrix_t matrix,
                             size_t index,
                             akvcam_list_t combined,
                             akvcam_matrix_t combinations);

akvcam_list_t akvcam_list_new(void)
{
    akvcam_list_t self = kzalloc(sizeof(struct akvcam_list), GFP_KERNEL);
    kref_init(&self->ref);

    return self;
}

akvcam_list_t akvcam_list_new_copy(akvcam_list_t other)
{
    akvcam_list_t self = kzalloc(sizeof(struct akvcam_list), GFP_KERNEL);
    kref_init(&self->ref);
    akvcam_list_append(self, other);

    return self;
}

void akvcam_list_free(struct kref *ref)
{
    akvcam_list_t self = container_of(ref, struct akvcam_list, ref);
    akvcam_list_clear(self);
    kfree(self);
}

void akvcam_list_delete(akvcam_list_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_list_free);
}

akvcam_list_t akvcam_list_ref(akvcam_list_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_list_copy(akvcam_list_t self, const akvcam_list_t other)
{
    akvcam_list_clear(self);
    akvcam_list_append(self, other);
}

void akvcam_list_append(akvcam_list_t self, const akvcam_list_t other)
{
    akvcam_list_element_t it = NULL;
    void *data;

    for (;;) {
        data = akvcam_list_next(other, &it);

        if (!it)
            break;

        akvcam_list_push_back(self,
                              data,
                              it->copier,
                              it->deleter);
    }
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
                                            const akvcam_copy_t copier,
                                            const akvcam_delete_t deleter)
{
    akvcam_list_element_t element;

    if (!self)
        return NULL;

    element = kzalloc(sizeof(struct akvcam_list_element), GFP_KERNEL);

    if (!element) {
        akvcam_set_last_error(-ENOMEM);

        return NULL;
    }

    element->data = copier? copier(data): data;
    element->copier = copier;
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

    if (!self)
        return;

    for (it = self->head; it != NULL; it = it->next)
        if (it == element) {
            if (it->data && it->deleter)
                it->deleter(it->data);

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
            element->deleter(element->data);

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
                                       const akvcam_are_equals_t equals)
{
    akvcam_list_element_t it = NULL;
    void *element_data;

    if (!equals)
        return NULL;

    for (;;) {
        element_data = akvcam_list_next(self, &it);

        if (!it)
            break;

        if (equals(element_data, data))
            return it;
    }

    return NULL;
}

ssize_t akvcam_list_index_of(const akvcam_list_t self,
                             const void *data,
                             const akvcam_are_equals_t equals)
{
    akvcam_list_element_t it = NULL;
    void *element_data;
    ssize_t i;

    if (!equals)
        return -1;

    for (i = 0;; i++) {
        element_data = akvcam_list_next(self, &it);

        if (!it)
            break;

        if (equals(element_data, data))
            return i;
    }

    return -1;
}

bool akvcam_list_contains(const akvcam_list_t self,
                          const void *data,
                          const akvcam_are_equals_t equals)
{
    return akvcam_list_find(self, data, equals) != NULL;
}

void *akvcam_list_next(const akvcam_list_t self,
                       akvcam_list_element_t *element)
{
    if (!element)
        return NULL;

    if (!self) {
        *element = NULL;

        return NULL;
    }

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

akvcam_copy_t akvcam_list_element_copier(const akvcam_list_element_t element)
{
    if (!element)
        return NULL;

    return element->copier;
}

akvcam_delete_t akvcam_list_element_deleter(const akvcam_list_element_t element)
{
    if (!element)
        return NULL;

    return element->deleter;
}

akvcam_matrix_t akvcam_matrix_combine(const akvcam_matrix_t matrix)
{
    akvcam_list_t combined;
    akvcam_matrix_t combinations;

    combined = akvcam_list_new();
    combinations = akvcam_list_new();
    akvcam_matrix_combine_p(matrix, 0, combined, combinations);
    akvcam_list_delete(combined);

    return combinations;
}

/* A matrix is a list of lists where each element in the main list is a row,
 * and each element in a row is a column. We combine each element in a row with
 * each element in the next rows.
 */
void akvcam_matrix_combine_p(akvcam_matrix_t matrix,
                             size_t index,
                             akvcam_list_t combined,
                             akvcam_matrix_t combinations)
{
    akvcam_list_t combined_p1;
    akvcam_list_t row;
    akvcam_list_element_t it = NULL;
    void *data;

    if (index >= akvcam_list_size(matrix)) {
        akvcam_list_push_back(combinations,
                              combined,
                              (akvcam_copy_t) akvcam_list_ref,
                              (akvcam_delete_t) akvcam_list_delete);

        return;
    }

    row = akvcam_list_at(matrix, index);

    for (;;) {
        data = akvcam_list_next(row, &it);

        if (!it)
            break;

        combined_p1 = akvcam_list_new_copy(combined);
        akvcam_list_push_back(combined_p1,
                              data,
                              it->copier,
                              it->deleter);
        akvcam_matrix_combine_p(matrix,
                                index + 1,
                                combined_p1,
                                combinations);
        akvcam_list_delete(combined_p1);
    }
}
