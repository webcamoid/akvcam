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

#ifndef AKVCAM_LIST_H
#define AKVCAM_LIST_H

#include <linux/types.h>

#include "object.h"
#include "utils.h"

#define akvcam_list_tt(type) akvcam_list_t

struct akvcam_list;
typedef struct akvcam_list *akvcam_list_t;
struct akvcam_list_element;
typedef struct akvcam_list_element *akvcam_list_element_t;

// public
akvcam_list_t akvcam_list_new(void);
void akvcam_list_delete(akvcam_list_t *self);

size_t akvcam_list_size(const akvcam_list_t self);
bool akvcam_list_empty(const akvcam_list_t self);
void *akvcam_list_at(const akvcam_list_t self, size_t i);
void *akvcam_list_front(const akvcam_list_t self);
void *akvcam_list_back(const akvcam_list_t self);
bool akvcam_list_push_back(akvcam_list_t self,
                           void *data,
                           const akvcam_deleter_t deleter);
bool akvcam_list_push_back_copy(akvcam_list_t self,
                                const void *data,
                                size_t data_size,
                                const akvcam_deleter_t deleter);
bool akvcam_list_pop(akvcam_list_t self,
                     size_t i,
                     void **data,
                     akvcam_deleter_t *deleter);
void akvcam_list_erase(akvcam_list_t self, const akvcam_list_element_t element);
void akvcam_list_clear(akvcam_list_t self);
akvcam_list_element_t akvcam_list_find(const akvcam_list_t self,
                                       const void *data,
                                       size_t size,
                                       const akvcam_are_equals_t equals);
ssize_t akvcam_list_index_of(const akvcam_list_t self,
                             const void *data,
                             size_t size,
                             const akvcam_are_equals_t equals);
void *akvcam_list_next(const akvcam_list_t self,
                       akvcam_list_element_t *element);
void *akvcam_list_element_data(const akvcam_list_element_t element);

#endif // AKVCAM_LIST_H
