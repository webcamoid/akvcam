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
#include <linux/limits.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "list.h"

#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t) (SIZE_MAX >> 1))
#endif

typedef struct {
    size_t index;
    akvcam_list_t combined;
} akvcam_matrix_combine_frame;

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

akvcam_list_t akvcam_list_new(void)
{
    akvcam_list_t self = kzalloc(sizeof(struct akvcam_list), GFP_KERNEL);
    kref_init(&self->ref);

    return self;
}

akvcam_list_t akvcam_list_new_copy(akvcam_list_ct other)
{
    akvcam_list_t self = kzalloc(sizeof(struct akvcam_list), GFP_KERNEL);
    kref_init(&self->ref);
    akvcam_list_append(self, other);

    return self;
}

static void akvcam_list_free(struct kref *ref)
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

void akvcam_list_copy(akvcam_list_t self, akvcam_list_ct other)
{
    akvcam_list_clear(self);
    akvcam_list_append(self, other);
}

void akvcam_list_append(akvcam_list_t self, akvcam_list_ct other)
{
    akvcam_list_element_t it = NULL;

    for (;;) {
        void *data = akvcam_list_next(other, &it);

        if (!it)
            break;

        akvcam_list_push_back(self,
                              data,
                              it->copier,
                              it->deleter);
    }
}

size_t akvcam_list_size(akvcam_list_ct self)
{
    if (!self)
        return 0;

    return self->size;
}

bool akvcam_list_empty(akvcam_list_ct self)
{
    if (!self)
        return true;

    return self->size < 1;
}

void *akvcam_list_at(akvcam_list_ct self, size_t i)
{
    akvcam_list_element_t it = NULL;
    size_t j;

    for (j = 0;; j++) {
        void *element_data = akvcam_list_next(self, &it);

        if (!it)
            break;

        if (i == j)
            return element_data;
    }

    return NULL;
}

void *akvcam_list_front(akvcam_list_ct self)
{
    if (!self || self->size < 1)
        return NULL;

    return self->head->data;
}

void *akvcam_list_back(akvcam_list_ct self)
{
    if (!self || self->size < 1)
        return NULL;

    return self->tail->data;
}

akvcam_list_element_t akvcam_list_push_back(akvcam_list_t self,
                                            void *data,
                                            akvcam_copy_t copier,
                                            akvcam_delete_t deleter)
{
    akvcam_list_element_t element;

    if (!self)
        return NULL;

    element = kzalloc(sizeof(struct akvcam_list_element), GFP_KERNEL);

    if (!element) {
        akvcam_set_last_error(-ENOMEM);

        return NULL;
    }

    if (copier) {
        element->data = copier(data);

        if (!element->data) {
            kfree(element);
            akvcam_set_last_error(-ENOMEM);

            return NULL;
        }
    } else {
        element->data = data;
    }

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

akvcam_list_element_t akvcam_list_it(akvcam_list_ct self, size_t i)
{
    akvcam_list_element_t it = NULL;
    size_t j;

    for (j = 0;; j++) {
        akvcam_list_next(self, &it);

        if (!it)
            break;

        if (i == j)
            return it;
    }

    return NULL;
}

void akvcam_list_erase(akvcam_list_t self, akvcam_list_element_ct element)
{
    akvcam_list_element_t it = (akvcam_list_element_t) element;

    if (!self || !it)
        return;

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

akvcam_list_element_t akvcam_list_find(akvcam_list_ct self,
                                       const void *data,
                                       akvcam_are_equals_t equals)
{
    akvcam_list_element_t it = NULL;

    if (!equals)
        return NULL;

    for (;;) {
        void *element_data = akvcam_list_next(self, &it);

        if (!it)
            break;

        if (equals(element_data, data))
            return it;
    }

    return NULL;
}

ssize_t akvcam_list_index_of(akvcam_list_ct self,
                             const void *data,
                             akvcam_are_equals_t equals)
{
    akvcam_list_element_t it = NULL;
    ssize_t i;

    if (!equals)
        return -1;

    for (i = 0;; i++) {
        void *element_data = akvcam_list_next(self, &it);

        if (!it)
            break;

        if (equals(element_data, data)) {
            if (i > (size_t) SSIZE_MAX)
                return -1;

            return i;
        }
    }

    return -1;
}

bool akvcam_list_contains(akvcam_list_ct self,
                          const void *data,
                          akvcam_are_equals_t equals)
{
    return akvcam_list_find(self, data, equals) != NULL;
}

void *akvcam_list_next(akvcam_list_ct self,
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

void *akvcam_list_element_data(akvcam_list_element_ct element)
{
    if (!element)
        return NULL;

    return element->data;
}

akvcam_copy_t akvcam_list_element_copier(akvcam_list_element_ct element)
{
    if (!element)
        return NULL;

    return element->copier;
}

akvcam_delete_t akvcam_list_element_deleter(akvcam_list_element_ct element)
{
    if (!element)
        return NULL;

    return element->deleter;
}

akvcam_matrix_t akvcam_matrix_combine(akvcam_matrix_ct matrix)
{
    akvcam_matrix_t combinations = akvcam_list_new();
    size_t matrix_size = akvcam_list_size(matrix);
    size_t max_frames;
    akvcam_matrix_combine_frame *stack;
    size_t stack_top = 0;

    if (!combinations || matrix_size < 1)
        return combinations;

    /* Worst case: one branch per element in each row.
     * In practice, the stack grows linearly with depth,
     * but we reserve a safe limit.
     */
    max_frames = matrix_size * 64 + 1;
    stack = vmalloc(max_frames * sizeof(akvcam_matrix_combine_frame));

    if (!stack)
        return combinations;

    // Push the initial frame
    stack[0].index = 0;
    stack[0].combined = akvcam_list_new();

    if (!stack[0].combined) {
        vfree(stack);

        return combinations;
    }

    stack_top = 1;

    while (stack_top > 0) {
        akvcam_matrix_combine_frame frame;
        akvcam_list_t row;
        akvcam_list_element_t it = NULL;
        void *data;

        // Pop
        stack_top--;
        frame = stack[stack_top];

        if (frame.index >= matrix_size) {
            // Leaf: save the complete combination
            akvcam_list_push_back(combinations,
                                  frame.combined,
                                  (akvcam_copy_t) akvcam_list_ref,
                                  (akvcam_delete_t) akvcam_list_delete);
            akvcam_list_delete(frame.combined);

            continue;
        }

        row = akvcam_list_at(matrix, frame.index);

        while ((data = akvcam_list_next(row, &it)) || it) {
            akvcam_list_t combined_next;

            if (stack_top >= max_frames) {
                // Sercure limit reached
                break;
            }

            combined_next = akvcam_list_new_copy(frame.combined);

            if (!combined_next)
                break;

            akvcam_list_push_back(combined_next,
                                  data,
                                  it->copier,
                                  it->deleter);

            stack[stack_top].index = frame.index + 1;
            stack[stack_top].combined = combined_next;
            stack_top++;
        }

        akvcam_list_delete(frame.combined);
    }

    vfree(stack);

    return combinations;
}
