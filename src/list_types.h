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

#ifndef AKVCAM_LIST_TYPES_H
#define AKVCAM_LIST_TYPES_H

#define akvcam_list_tt(type) akvcam_list_t
#define akvcam_list_ctt(type) akvcam_list_ct

struct akvcam_list;
typedef struct akvcam_list *akvcam_list_t;
typedef const struct akvcam_list *akvcam_list_ct;
struct akvcam_list_element;
typedef struct akvcam_list_element *akvcam_list_element_t;
typedef const struct akvcam_list_element *akvcam_list_element_ct;
typedef akvcam_list_tt(char *) akvcam_string_list_t;
typedef akvcam_list_ctt(char *) akvcam_string_list_ct;
typedef akvcam_list_tt(akvcam_list_t) akvcam_matrix_t;
typedef akvcam_list_ctt(akvcam_list_t) akvcam_matrix_ct;
typedef akvcam_list_tt(akvcam_string_list_t) akvcam_string_matrix_t;
typedef akvcam_list_ctt(akvcam_string_list_t) akvcam_string_matrix_ct;

#endif // AKVCAM_LIST_TYPES_H
