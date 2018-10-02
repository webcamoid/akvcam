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

#include "map.h"
#include "list.h"
#include "object.h"

typedef struct akvcam_map_element
{
    char *key;
    void *value;
    size_t size;
    akvcam_deleter_t deleter;
    akvcam_list_element_t it;
} akvcam_map_element, *akvcam_map_element_t;

struct akvcam_map
{
    akvcam_object_t self;
    akvcam_list_tt(akvcam_map_element_t) elements;
};

void akvcam_map_element_delete(akvcam_map_element_t *element);
void akvcam_map_str_delete(char **element);

akvcam_map_t akvcam_map_new(void)
{
    akvcam_map_t self = kzalloc(sizeof(struct akvcam_map), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_map_delete);
    self->elements = akvcam_list_new();

    return self;
}

void akvcam_map_delete(akvcam_map_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_map_clear(*self);
    akvcam_list_delete(&((*self)->elements));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

size_t akvcam_map_size(const akvcam_map_t self)
{
    return akvcam_list_size(self->elements);
}

bool akvcam_map_empty(const akvcam_map_t self)
{
    return akvcam_list_empty(self->elements);
}

void *akvcam_map_value(const akvcam_map_t self, const char *key)
{
    akvcam_map_element_t element = akvcam_map_it(self, key);

    if (!element)
        return NULL;

    return  element->value;
}

static bool akvcam_map_equals_keys(const akvcam_map_element_t element,
                                   const char *key,
                                   size_t size)
{
    return strcmp(element->key, key) == 0;
}

akvcam_map_element_t akvcam_map_set_value(akvcam_map_t self,
                                          const char *key,
                                          void *value,
                                          size_t value_size,
                                          const akvcam_deleter_t deleter,
                                          bool copy)
{
    akvcam_map_element_t element;
    akvcam_list_element_t it =
            akvcam_list_find(self->elements,
                             key,
                             0,
                             (akvcam_are_equals_t) akvcam_map_equals_keys);

    if (it)
        akvcam_list_erase(self->elements, it);

    element = kzalloc(sizeof(akvcam_map_element), GFP_KERNEL);

    if (!element)
        return NULL;

    element->key = akvcam_strdup(key, AKVCAM_MEMORY_TYPE_KMALLOC);

    if (copy)
        element->value = kmemdup(value, value_size, GFP_KERNEL);
    else
        element->value = value;

    element->size = value_size;
    element->deleter = deleter;
    element->it =
            akvcam_list_push_back(self->elements,
                                  element,
                                  sizeof(akvcam_map_element),
                                  (akvcam_deleter_t) akvcam_map_element_delete,
                                  false);

    return element;
}

bool akvcam_map_contains(const akvcam_map_t self, const char *key)
{
    return akvcam_map_value(self, key) != NULL;
}

struct akvcam_list *akvcam_map_keys(const akvcam_map_t self)
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
                              strlen(map_element->key) + 1,
                              (akvcam_deleter_t) akvcam_map_str_delete,
                              true);
    }

    return keys;
}

struct akvcam_list *akvcam_map_values(const akvcam_map_t self)
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
                              map_element->size,
                              NULL,
                              false);
    }

    return values;
}

akvcam_map_element_t akvcam_map_it(akvcam_map_t self, const char *key)
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

void akvcam_map_erase(akvcam_map_t self, const akvcam_map_element_t element)
{
    akvcam_list_erase(self->elements, element->it);
}

void akvcam_map_clear(akvcam_map_t self)
{
    akvcam_list_clear(self->elements);
}

bool akvcam_map_next(const akvcam_map_t self,
                     akvcam_map_element_t *element)
{
    akvcam_list_element_t it = *element? (*element)->it: NULL;
    akvcam_map_element_t next = akvcam_list_next(self->elements, &it);

    if (!it)
        return false;

    *element = next;

    return true;
}

char *akvcam_map_element_key(const akvcam_map_element_t element)
{
    return element->key;
}

void *akvcam_map_element_value(const akvcam_map_element_t element)
{
    return element->value;
}

size_t akvcam_map_element_size(const akvcam_map_element_t element)
{
    return element->size;
}

akvcam_deleter_t akvcam_map_element_deleter(const akvcam_map_element_t element)
{
    return element->deleter;
}

size_t akvcam_map_sizeof(void)
{
    return sizeof(struct akvcam_map);
}

void akvcam_map_element_delete(akvcam_map_element_t *element)
{
    if ((*element)->key)
        kfree((*element)->key);

    if ((*element)->deleter && (*element)->value)
        (*element)->deleter(&((*element)->value));

    kfree(*element);
    *element = NULL;
}

void akvcam_map_str_delete(char **str)
{
    if (*str)
        kfree(*str);

    *str = NULL;
}
