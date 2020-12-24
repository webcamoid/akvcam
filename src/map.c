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

#include "map.h"
#include "list.h"

typedef struct akvcam_map_element
{
    char *key;
    void *value;
    akvcam_copy_t copier;
    akvcam_delete_t deleter;
    akvcam_list_element_t it;
} akvcam_map_element, *akvcam_map_element_t;
typedef const akvcam_map_element *akvcam_map_element_ct;

struct akvcam_map
{
    struct kref ref;
    akvcam_list_tt(akvcam_map_element_t) elements;
};

akvcam_map_t akvcam_map_new(void)
{
    akvcam_map_t self = kzalloc(sizeof(struct akvcam_map), GFP_KERNEL);
    kref_init(&self->ref);
    self->elements = akvcam_list_new();

    return self;
}

akvcam_map_t akvcam_map_new_copy(akvcam_map_ct other)
{
    akvcam_map_t self = kzalloc(sizeof(struct akvcam_map), GFP_KERNEL);
    kref_init(&self->ref);
    self->elements = akvcam_list_new();
    akvcam_map_update(self, other);

    return self;
}

static void akvcam_map_free(struct kref *ref)
{
    akvcam_map_t self = container_of(ref, struct akvcam_map, ref);
    akvcam_map_clear(self);
    akvcam_list_delete(self->elements);
    kfree(self);
}

void akvcam_map_delete(akvcam_map_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_map_free);
}

akvcam_map_t akvcam_map_ref(akvcam_map_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_map_copy(akvcam_map_t self, const akvcam_map_t other)
{
    akvcam_map_clear(self);
    akvcam_map_update(self, other);
}

void akvcam_map_update(akvcam_map_t self, akvcam_map_ct other)
{
    akvcam_map_element_t it = NULL;

    while (akvcam_map_next(other, &it)) {
        akvcam_map_set_value(self,
                             it->key,
                             it->value,
                             it->copier,
                             it->deleter);
    }
}

size_t akvcam_map_size(akvcam_map_ct self)
{
    return akvcam_list_size(self->elements);
}

bool akvcam_map_empty(akvcam_map_ct self)
{
    return akvcam_list_empty(self->elements);
}

void *akvcam_map_value(akvcam_map_ct self, const char *key)
{
    akvcam_map_element_t element = akvcam_map_it(self, key);

    if (!element)
        return NULL;

    return  element->value;
}

static bool akvcam_map_equals_keys(akvcam_map_element_ct element,
                                   const char *key)
{
    return strcmp(element->key, key) == 0;
}

static akvcam_map_element_t akvcam_map_element_copy(akvcam_map_element_ct element)
{
    return kmemdup(element, sizeof(akvcam_map_element), GFP_KERNEL);
}

akvcam_map_element_t akvcam_map_set_value(akvcam_map_t self,
                                          const char *key,
                                          void *value,
                                          akvcam_copy_t copier,
                                          akvcam_delete_t deleter)
{
    akvcam_map_element element;
    akvcam_list_element_t it =
            akvcam_list_find(self->elements,
                             key,
                             (akvcam_are_equals_t) akvcam_map_equals_keys);

    if (it)
        akvcam_list_erase(self->elements, it);

    element.key = akvcam_strdup(key, AKVCAM_MEMORY_TYPE_KMALLOC);
    element.value = copier? copier(value): value;
    element.copier = copier;
    element.deleter = deleter;
    element.it = akvcam_list_push_back(self->elements,
                                       &element,
                                       (akvcam_copy_t) akvcam_map_element_copy,
                                       (akvcam_delete_t) kfree);

    return akvcam_list_back(self->elements);
}

bool akvcam_map_contains(akvcam_map_ct self, const char *key)
{
    return akvcam_map_value(self, key) != NULL;
}

static char *akvcam_map_key_copy(const char *str)
{
    return kstrdup(str, GFP_KERNEL);
}

akvcam_list_t akvcam_map_keys(akvcam_map_ct self)
{
    akvcam_list_element_t element = NULL;
    akvcam_map_element_t map_element;
    akvcam_list_t keys = akvcam_list_new();

    for (;;) {
        map_element = akvcam_list_next(self->elements, &element);

        if (!element)
            break;

        akvcam_list_push_back(keys,
                              map_element->key,
                              (akvcam_copy_t) akvcam_map_key_copy,
                              (akvcam_delete_t) kfree);
    }

    return keys;
}

akvcam_list_t akvcam_map_values(akvcam_map_ct self)
{
    akvcam_list_element_t element = NULL;
    akvcam_map_element_t map_element;
    akvcam_list_t values = akvcam_list_new();

    for (;;) {
        map_element = akvcam_list_next(self->elements, &element);

        if (!element)
            break;

        akvcam_list_push_back(values,
                              map_element->value,
                              map_element->copier,
                              map_element->deleter);
    }

    return values;
}

akvcam_map_element_t akvcam_map_it(akvcam_map_ct self, const char *key)
{
    akvcam_list_element_t element = NULL;
    akvcam_map_element_t map_element;

    for (;;) {
        map_element = akvcam_list_next(self->elements, &element);

        if (!element)
            break;

        if (strcmp(map_element->key, key) == 0)
            return map_element;
    }

    return NULL;
}

void akvcam_map_erase(akvcam_map_t self, akvcam_map_element_ct element)
{
    if (element->key)
        kfree(element->key);

    if (element->value && element->deleter)
        element->deleter(element->value);

    akvcam_list_erase(self->elements, element->it);
}

void akvcam_map_clear(akvcam_map_t self)
{
    akvcam_list_element_t it = NULL;

    for (;;) {
        akvcam_map_element_t element = akvcam_list_next(self->elements, &it);

        if (!it)
            break;

        if (element->key)
            kfree(element->key);

        if (element->value && element->deleter)
            element->deleter(element->value);
    }

    akvcam_list_clear(self->elements);
}

bool akvcam_map_next(akvcam_map_ct self, akvcam_map_element_t *element)
{
    akvcam_list_element_t it = *element? (*element)->it: NULL;
    akvcam_map_element_t next = akvcam_list_next(self->elements, &it);

    if (!it)
        return false;

    *element = next;

    return true;
}

char *akvcam_map_element_key(akvcam_map_element_ct element)
{
    return element->key;
}

void *akvcam_map_element_value(akvcam_map_element_ct element)
{
    return element->value;
}

akvcam_copy_t akvcam_map_element_copier(akvcam_map_element_ct element)
{
    return element->copier;
}

akvcam_delete_t akvcam_map_element_deleter(akvcam_map_element_ct element)
{
    return element->deleter;
}
