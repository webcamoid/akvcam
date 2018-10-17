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

#ifndef AKVCAM_SETTINGS_H
#define AKVCAM_SETTINGS_H

#include <linux/types.h>

#include "list_types.h"

#define AKVCAM_SETTINGS_PREFERRED_MAX_LINE_SIZE 4096
#define AKVCAM_SETTINGS_PREFERRED_MAX_FILE_SIZE (4 * 1024 * 1024)

struct akvcam_settings;
typedef struct akvcam_settings *akvcam_settings_t;
struct v4l2_fract;

// public
akvcam_settings_t akvcam_settings_new(void);
void akvcam_settings_delete(akvcam_settings_t *self);

bool akvcam_settings_load(akvcam_settings_t self, const char *file_name);
size_t akvcam_settings_max_line_size(akvcam_settings_t self);
void akvcam_settings_set_max_line_size(akvcam_settings_t self,
                                       size_t line_size);
size_t akvcam_settings_max_file_size(akvcam_settings_t self);
void akvcam_settings_set_max_file_size(akvcam_settings_t self,
                                       size_t file_size);
void akvcam_settings_begin_group(akvcam_settings_t self, const char *prefix);
void akvcam_settings_end_group(akvcam_settings_t self);
size_t akvcam_settings_begin_array(akvcam_settings_t self, const char *prefix);
void akvcam_settings_set_array_index(akvcam_settings_t self, size_t i);
void akvcam_settings_end_array(akvcam_settings_t self);
akvcam_string_list_t akvcam_settings_groups(const akvcam_settings_t self);
akvcam_string_list_t akvcam_settings_keys(const akvcam_settings_t self);
void akvcam_settings_clear(akvcam_settings_t self);
bool akvcam_settings_contains(const akvcam_settings_t self, const char *key);
char *akvcam_settings_value(const akvcam_settings_t self, const char *key);
bool akvcam_settings_value_bool(const akvcam_settings_t self, const char *key);
int32_t akvcam_settings_value_int32(const akvcam_settings_t self,
                                    const char *key);
uint32_t akvcam_settings_value_uint32(const akvcam_settings_t self,
                                      const char *key);
akvcam_string_list_t akvcam_settings_value_list(const akvcam_settings_t self,
                                                const char *key,
                                                const char *separators);
struct v4l2_fract akvcam_settings_value_frac(const akvcam_settings_t self,
                                             const char *key);

// public static
bool akvcam_settings_to_bool(const char *value);
int32_t akvcam_settings_to_int32(const char *value);
uint32_t akvcam_settings_to_uint32(const char *value);
akvcam_string_list_t akvcam_settings_to_list(const char *value,
                                             const char *separators);
struct v4l2_fract akvcam_settings_to_frac(const char *value);

#endif // AKVCAM_SETTINGS_H
