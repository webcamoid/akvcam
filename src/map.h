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

#ifndef AKVCAM_MAP_H
#define AKVCAM_MAP_H

#include <linux/types.h>

#include "object.h"

#define akvcam_map_tt(value_type) akvcam_map_t

struct akvcam_map;
typedef struct akvcam_map *akvcam_map_t;
struct akvcam_map_element;
typedef struct akvcam_map_element *akvcam_map_element_t;
struct akvcam_list;
typedef akvcam_map_tt(char *) akvcam_string_map_t;

// public
akvcam_map_t akvcam_map_new(void);
void akvcam_map_delete(akvcam_map_t *self);

size_t akvcam_map_size(const akvcam_map_t self);
bool akvcam_map_empty(const akvcam_map_t self);
void *akvcam_map_value(const akvcam_map_t self, const char *key);
akvcam_map_element_t akvcam_map_set_value(akvcam_map_t self,
                                          const char *key,
                                          void *value,
                                          size_t value_size,
                                          const akvcam_deleter_t deleter,
                                          bool is_object);
bool akvcam_map_contains(const akvcam_map_t self, const char *key);
struct akvcam_list *akvcam_map_keys(const akvcam_map_t self);
struct akvcam_list *akvcam_map_values(const akvcam_map_t self);
akvcam_map_element_t akvcam_map_it(akvcam_map_t self, const char *key);
void akvcam_map_erase(akvcam_map_t self, const akvcam_map_element_t element);
void akvcam_map_clear(akvcam_map_t self);
bool akvcam_map_next(const akvcam_map_t self,
                     akvcam_map_element_t *element);
char *akvcam_map_element_key(const akvcam_map_element_t element);
void *akvcam_map_element_value(const akvcam_map_element_t element);
size_t akvcam_map_element_size(const akvcam_map_element_t element);
akvcam_deleter_t akvcam_map_element_deleter(const akvcam_map_element_t element);

// static
size_t akvcam_map_sizeof(void);

#endif // AKVCAM_MAP_H
