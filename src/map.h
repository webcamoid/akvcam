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

#include "list_types.h"
#include "utils.h"

#define akvcam_map_tt(value_type) akvcam_map_t
#define akvcam_map_ctt(value_type) akvcam_map_ct

struct akvcam_map;
typedef struct akvcam_map *akvcam_map_t;
typedef const struct akvcam_map *akvcam_map_ct;
struct akvcam_map_element;
typedef struct akvcam_map_element *akvcam_map_element_t;
typedef const struct akvcam_map_element *akvcam_map_element_ct;
typedef akvcam_map_tt(char *) akvcam_string_map_t;
typedef akvcam_map_ctt(char *) akvcam_string_map_ct;

// public
akvcam_map_t akvcam_map_new(void);
akvcam_map_t akvcam_map_new_copy(akvcam_map_ct other);
void akvcam_map_delete(akvcam_map_t self);
akvcam_map_t akvcam_map_ref(akvcam_map_t self);

void akvcam_map_copy(akvcam_map_t self, const akvcam_map_t other);
void akvcam_map_update(akvcam_map_t self, akvcam_map_ct other);
size_t akvcam_map_size(akvcam_map_ct self);
bool akvcam_map_empty(akvcam_map_ct self);
void *akvcam_map_value(akvcam_map_ct self, const char *key);
akvcam_map_element_t akvcam_map_set_value(akvcam_map_t self,
                                          const char *key,
                                          void *value,
                                          akvcam_copy_t copier,
                                          akvcam_delete_t deleter);
bool akvcam_map_contains(akvcam_map_ct self, const char *key);
akvcam_list_t akvcam_map_keys(akvcam_map_ct self);
akvcam_list_t akvcam_map_values(akvcam_map_ct self);
akvcam_map_element_t akvcam_map_it(akvcam_map_ct self, const char *key);
void akvcam_map_erase(akvcam_map_t self, akvcam_map_element_ct element);
void akvcam_map_clear(akvcam_map_t self);
bool akvcam_map_next(akvcam_map_ct self, akvcam_map_element_t *element);
char *akvcam_map_element_key(akvcam_map_element_ct element);
void *akvcam_map_element_value(akvcam_map_element_ct element);
akvcam_copy_t akvcam_map_element_copier(akvcam_map_element_ct element);
akvcam_delete_t akvcam_map_element_deleter(akvcam_map_element_ct element);

#endif // AKVCAM_MAP_H
