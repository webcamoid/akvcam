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

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/videodev2.h>

#include "settings.h"
#include "list.h"
#include "map.h"
#include "object.h"
#include "rbuffer.h"
#include "utils.h"

#define akvcam_settings_element_empty(element) \
    (!(element)->group && !(element)->key && !(element)->value)

typedef struct
{
    char *group;
    char *key;
    char *value;
} akvcam_settings_element, *akvcam_settings_element_t;

struct akvcam_settings
{
    akvcam_object_t self;
    akvcam_map_tt(akvcam_string_map_t) configs;
    size_t max_line_size;
    size_t max_file_size;
    char *current_group;
    char *current_array;
    size_t array_index;
};

bool akvcam_settings_parse(const char *line, akvcam_settings_element_t element);
char *akvcam_settings_parse_string(char *str, bool move);
void akvcam_settings_element_free(akvcam_settings_element_t element);
akvcam_string_map_t akvcam_settings_group_configs(const akvcam_settings_t self);

akvcam_settings_t akvcam_settings_new(void)
{
    akvcam_settings_t self = kzalloc(sizeof(struct akvcam_settings), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_settings_delete);
    self->configs = akvcam_map_new();
    self->max_line_size = AKVCAM_SETTINGS_PREFERRED_MAX_LINE_SIZE;
    self->max_file_size = AKVCAM_SETTINGS_PREFERRED_MAX_FILE_SIZE;

    return self;
}

void akvcam_settings_delete(akvcam_settings_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_settings_end_array(*self);
    akvcam_settings_end_group(*self);
    akvcam_map_delete(&((*self)->configs));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

bool akvcam_settings_find_new_line(const char *element,
                                   const char *new_line,
                                   size_t size)
{
    return strncmp(element, new_line, 1) == 0;
}

bool akvcam_settings_load(akvcam_settings_t self, const char *file_name)
{
    struct file *config_file = NULL;
    akvcam_rbuffer_tt(char *) data = akvcam_rbuffer_new();
    char *raw_data = vmalloc((unsigned long) self->max_line_size);
    char *line;
    char *current_group = NULL;
    akvcam_string_map_t group_configs;
    akvcam_settings_element element;
    struct kstat stats;
    mm_segment_t oldfs;
    ssize_t bytes_read;
    size_t file_size;
    size_t line_size;
    size_t totaL_bytes = 0;
    ssize_t new_line = 0;
    loff_t offset;
    akvcam_map_t tmp_map;

    memset(&element, 0, sizeof(akvcam_settings_element));
    file_size = self->max_file_size > 0?
                    self->max_file_size:
                    AKVCAM_SETTINGS_PREFERRED_MAX_FILE_SIZE;
    memset(&stats, 0, sizeof(struct kstat));
    oldfs = get_fs();
    set_fs(KERNEL_DS);

    if (vfs_stat((const char __user *) file_name, &stats)
        || (size_t) stats.size > file_size) {
        set_fs(oldfs);

        goto akvcam_settings_load_failed;
    }

    set_fs(oldfs);

    file_size = (size_t) stats.size;
    akvcam_rbuffer_resize(data,
                          file_size,
                          sizeof(char),
                          AKVCAM_MEMORY_TYPE_VMALLOC);

    config_file = filp_open(file_name, O_RDONLY, 0);

    if (IS_ERR(config_file))
        goto akvcam_settings_load_failed;

    akvcam_settings_clear(self);
    line_size = self->max_line_size > 0?
                    self->max_line_size:
                    AKVCAM_SETTINGS_PREFERRED_MAX_LINE_SIZE;

    for (;;) {
        if (akvcam_rbuffer_empty(data)) {
            if (totaL_bytes >= file_size)
                break;

            offset = 0;
            bytes_read = akvcam_file_read(config_file,
                                          raw_data,
                                          line_size,
                                          offset);

            if (bytes_read < 1)
                break;

            akvcam_rbuffer_queue_bytes(data, raw_data, (size_t) bytes_read);
            totaL_bytes += (size_t) bytes_read;
        }

        akvcam_rbuffer_find(data,
                            "\n",
                            1,
                            (akvcam_are_equals_t) akvcam_settings_find_new_line,
                            &new_line);

        if (new_line < 0)
            continue;

        if ((size_t) new_line >= line_size)
            goto akvcam_settings_load_failed;

        // line_size doesn't count \n so increase cur_line_size to dequeue the
        // \n too, and make last byte NULL to identify it as a string.
        line = vmalloc((size_t) new_line + 2);
        line[(size_t) new_line + 1] = 0;
        new_line++;
        akvcam_rbuffer_dequeue_bytes(data, line, (size_t *) &new_line, false);
        line = akvcam_strip_move_str(line, AKVCAM_MEMORY_TYPE_VMALLOC);

        if (!akvcam_settings_parse(line, &element)) {
            vfree(line);

            goto akvcam_settings_load_failed;
        }

        if (!akvcam_settings_element_empty(&element)) {
            if (element.group && strlen(element.group) > 0) {
                if (current_group)
                    vfree(current_group);

                current_group = akvcam_strdup(element.group,
                                              AKVCAM_MEMORY_TYPE_VMALLOC);

                if (!akvcam_map_contains(self->configs, current_group)) {
                    tmp_map = akvcam_map_new();
                    akvcam_map_set_value(self->configs,
                                         current_group,
                                         tmp_map,
                                         akvcam_map_sizeof(),
                                         (akvcam_deleter_t) akvcam_map_delete,
                                         true);
                    akvcam_map_delete(&tmp_map);
                }
            } else if (element.key
                       && element.value
                       && strlen(element.key) > 0
                       && strlen(element.value) > 0) {
                if (!current_group) {
                    current_group = akvcam_strdup("*",
                                                  AKVCAM_MEMORY_TYPE_VMALLOC);
                    tmp_map = akvcam_map_new();
                    akvcam_map_set_value(self->configs,
                                         current_group,
                                         tmp_map,
                                         akvcam_map_sizeof(),
                                         (akvcam_deleter_t) akvcam_map_delete,
                                         true);
                    akvcam_map_delete(&tmp_map);
                }

                group_configs = akvcam_map_value(self->configs, current_group);
                akvcam_map_set_value(group_configs,
                                     element.key,
                                     element.value,
                                     strlen(element.value) + 1,
                                     NULL,
                                     false);
            }

            akvcam_settings_element_free(&element);
        }

        vfree(line);
    }

    filp_close(config_file, NULL);
    akvcam_rbuffer_delete(&data);
    vfree(raw_data);

    if (current_group)
        vfree(current_group);

    return true;

akvcam_settings_load_failed:
    if (config_file)
        filp_close(config_file, NULL);

    akvcam_settings_clear(self);
    akvcam_rbuffer_delete(&data);
    vfree(raw_data);

    if (current_group)
        vfree(current_group);

    return false;
}

size_t akvcam_settings_max_line_size(akvcam_settings_t self)
{
    return self->max_line_size;
}

void akvcam_settings_set_max_line_size(akvcam_settings_t self,
                                       size_t line_size)
{
    self->max_line_size = line_size;
}

size_t akvcam_settings_max_file_size(akvcam_settings_t self)
{
    return self->max_file_size;
}

void akvcam_settings_set_max_file_size(akvcam_settings_t self,
                                       size_t file_size)
{
    self->max_file_size = file_size;
}

void akvcam_settings_begin_group(akvcam_settings_t self, const char *prefix)
{
    size_t len = strlen(prefix);

    akvcam_settings_end_group(self);

    if (prefix) {
        self->current_group = vmalloc(len + 1);
        self->current_group[len] = 0;
        memcpy(self->current_group, prefix, len);
    }
}

void akvcam_settings_end_group(akvcam_settings_t self)
{
    if (self->current_group) {
        vfree(self->current_group);
        self->current_group = NULL;
    }
}

size_t akvcam_settings_begin_array(akvcam_settings_t self, const char *prefix)
{
    akvcam_string_map_t group_configs = akvcam_settings_group_configs(self);
    size_t len = strlen(prefix);
    char *array_key;
    char *array_size_str;
    size_t array_key_size;
    size_t array_size = 0;

    akvcam_settings_end_array(self);

    if (!group_configs)
        return 0;

    // Read array size.
    array_key_size = strlen(prefix) + strlen("/size") + 1;
    array_key = vmalloc(array_key_size);
    snprintf(array_key, array_key_size, "%s/size", prefix);
    array_size_str = akvcam_map_value(group_configs, array_key);
    vfree(array_key);
    kstrtou32(array_size_str, 10, (u32 *) &array_size);

    if (prefix) {
        self->current_array = vmalloc(len + 1);
        self->current_array[len] = 0;
        memcpy(self->current_array, prefix, len);
    }

    return array_size;
}

void akvcam_settings_set_array_index(akvcam_settings_t self, size_t i)
{
    self->array_index = i;
}

void akvcam_settings_end_array(akvcam_settings_t self)
{
    if (self->current_array) {
        vfree(self->current_array);
        self->current_array = NULL;
    }
}

akvcam_string_list_t akvcam_settings_groups(const akvcam_settings_t self)
{
    akvcam_map_element_t element = NULL;
    akvcam_string_list_t groups = akvcam_list_new();
    char *group;

    while (akvcam_map_next(self->configs, &element)) {
        group = akvcam_map_element_key(element);
        akvcam_list_push_back(groups, group, strlen(group) + 1, NULL, false);
    }

    return groups;
}

akvcam_string_list_t akvcam_settings_keys(const akvcam_settings_t self)
{
    akvcam_map_element_t element = NULL;
    akvcam_string_list_t keys = akvcam_list_new();
    akvcam_string_map_t group_configs = akvcam_settings_group_configs(self);
    char *key;

    if (!group_configs)
        return keys;

    while (akvcam_map_next(group_configs, &element)) {
        key = akvcam_map_element_key(element);
        akvcam_list_push_back(keys, key, strlen(key) + 1, NULL, false);
    }

    return keys;
}

void akvcam_settings_clear(akvcam_settings_t self)
{
    akvcam_map_clear(self->configs);
    akvcam_settings_end_array(self);
    akvcam_settings_end_group(self);
    self->array_index = 0;
}

bool akvcam_settings_contains(const akvcam_settings_t self, const char *key)
{
    akvcam_string_map_t group_configs = akvcam_settings_group_configs(self);

    if (!group_configs)
        return false;

    return akvcam_map_contains(group_configs, key);
}

char *akvcam_settings_value(const akvcam_settings_t self, const char *key)
{
    akvcam_string_map_t group_configs = akvcam_settings_group_configs(self);
    char *array_key;
    char *value;
    size_t array_key_size;

    if (!group_configs || !key || strlen(key) < 1)
        return NULL;

    if (self->current_array) {
        array_key_size = strlen(self->current_array)
                       + strlen(key) + 23;
        array_key = vzalloc(array_key_size);
        snprintf(array_key,
                 array_key_size,
                 "%s/%zu/%s", self->current_array, self->array_index + 1, key);
        value = akvcam_map_value(group_configs, array_key);
        vfree(array_key);
    } else {
        value = akvcam_map_value(group_configs, key);
    }

    return value;
}

bool akvcam_settings_value_bool(const akvcam_settings_t self, const char *key)
{
    return akvcam_settings_to_bool(akvcam_settings_value(self, key));
}

int32_t akvcam_settings_value_int32(const akvcam_settings_t self,
                                    const char *key)
{
    return akvcam_settings_to_int32(akvcam_settings_value(self, key));
}

uint32_t akvcam_settings_value_uint32(const akvcam_settings_t self,
                                      const char *key)
{
    return akvcam_settings_to_uint32(akvcam_settings_value(self, key));
}

akvcam_string_list_t akvcam_settings_value_list(const akvcam_settings_t self,
                                                const char *key,
                                                const char *separators)
{
    return akvcam_settings_to_list(akvcam_settings_value(self, key),
                                   separators);
}

struct v4l2_fract akvcam_settings_value_frac(const akvcam_settings_t self,
                                             const char *key)
{
    return akvcam_settings_to_frac(akvcam_settings_value(self, key));
}

bool akvcam_settings_to_bool(const char *value)
{
    s32 result = false;

    if (!value)
        return false;

    if (strcasecmp(value, "true") == 0)
        return true;

    if (kstrtos32(value, 10, (s32 *) &result) != 0)
        return false;

    return (bool) result;
}

int32_t akvcam_settings_to_int32(const char *value)
{
    s32 result = 0;

    if (!value)
        return 0;

    if (kstrtos32(value, 10, (s32 *) &result) != 0)
        return 0;

    return result;
}

uint32_t akvcam_settings_to_uint32(const char *value)
{
    u32 result = 0;

    if (!value)
        return 0;

    if (kstrtou32(value, 10, (u32 *) &result) != 0)
        return 0;

    return result;
}

akvcam_string_list_t akvcam_settings_to_list(const char *value,
                                             const char *separators)
{
    char *value_tmp;
    char *value_tmp_ptr;
    char *element;
    char *stripped_element;
    akvcam_string_list_t result = akvcam_list_new();

    if (!value)
        return result;

    value_tmp_ptr = value_tmp =
            akvcam_strdup(value, AKVCAM_MEMORY_TYPE_VMALLOC);

    for (;;) {
        element = strsep(&value_tmp_ptr, separators);

        if (!element)
            break;

        stripped_element = akvcam_strip_str(element,
                                            AKVCAM_MEMORY_TYPE_KMALLOC);
        akvcam_list_push_back(result,
                              stripped_element,
                              strlen(stripped_element) + 1,
                              NULL,
                              false);
        kfree(stripped_element);
    }

    vfree(value_tmp);

    return result;
}

struct v4l2_fract akvcam_settings_to_frac(const char *value)
{
    struct v4l2_fract frac = {0, 1};
    akvcam_string_list_t frac_list = akvcam_settings_to_list(value, "/");

    if (!value)
        return frac;

    switch (akvcam_list_size(frac_list)) {
    case 1:
        frac.numerator = akvcam_settings_to_uint32(akvcam_list_at(frac_list, 0));

        break;

    case 2:
        frac.numerator = akvcam_settings_to_uint32(akvcam_list_at(frac_list, 0));
        frac.denominator = akvcam_settings_to_uint32(akvcam_list_at(frac_list, 1));

        if (frac.denominator < 1) {
            frac.numerator = 0;
            frac.denominator = 1;
        }

        break;

    default:
        break;
    }

    akvcam_list_delete(&frac_list);

    return frac;
}

bool akvcam_settings_parse(const char *line, akvcam_settings_element_t element)
{
    char *pair_sep;
    size_t len = strlen(line);
    size_t offset;
    memset(element, 0, sizeof(akvcam_settings_element));

    if (len < 1 || line[0] == '#' || line[0] == ';')
        return true;

    if (line[0] == '[') {
        if (line[len - 1] != ']' || strlen(line) < 3)
            return false;

        element->group =
                akvcam_strip_str_sub(line,
                                     1,
                                     strlen(line) - 2,
                                     AKVCAM_MEMORY_TYPE_VMALLOC);

        return true;
    }

    pair_sep = strchr(line, '=');

    if (!pair_sep)
        return false;

    element->key = akvcam_strip_str_sub(line,
                                        0,
                                        (size_t) (pair_sep - line),
                                        AKVCAM_MEMORY_TYPE_VMALLOC);

    if (strlen(element->key) < 1) {
        akvcam_settings_element_free(element);

        return false;
    }

    offset = (size_t) (pair_sep - line + 1);
    element->value = akvcam_strip_str_sub(line,
                                          offset,
                                          len - offset,
                                          AKVCAM_MEMORY_TYPE_VMALLOC);
    element->value = akvcam_settings_parse_string(element->value, true);

    return true;
}

/* Escape sequences taken from:
 *
 * https://en.cppreference.com/w/cpp/language/escape
 *
 * but we ignore octals and universal characters.
 */
char *akvcam_settings_parse_string(char *str, bool move)
{
    char *str_tmp;
    char *str_parsed;
    char *key;
    char hex[3];
    char c;
    size_t i;
    size_t j;
    size_t len = strlen(str);
    char escape_k[] = "'\"?\\abfnrtv";
    char escape_v[] = "'\"?\\\a\b\f\n\r\t\v";

    if (len < 2) {
        if (move)
            return str;

        return akvcam_strdup(str, AKVCAM_MEMORY_TYPE_VMALLOC);
    }

    c = str[0];

    if (c != '"' && c != '\'') {
        if (move)
            return str;

        return akvcam_strdup(str, AKVCAM_MEMORY_TYPE_VMALLOC);
    }

    if (str[len - 1] != c) {
        if (move)
            return str;

        return akvcam_strdup(str, AKVCAM_MEMORY_TYPE_VMALLOC);
    }

    str_tmp = vzalloc(len + 1);
    hex[2] = 0;
    j = 0;

    for (i = 1; i < len - 1; i++) {
        if (str[i] == '\\' && i < len - 2) {
            key = strchr(escape_k, str[i + 1]);

            if (key) {
                str_tmp[j] = escape_v[key - escape_k];
                i++;
            } else if (str[i + 1] == 'x' && i < len - 4) {
                memcpy(hex, str + i + 2, 2);

                if (kstrtos8(hex, 16, (s8 *) &c) == 0) {
                    str_tmp[j] = c;
                    i += 3;
                } else {
                    str_tmp[j] = str[i];
                }
            } else {
                str_tmp[j] = str[i];
            }
        } else {
            str_tmp[j] = str[i];
        }

        j++;
    }

    len = strlen(str_tmp);
    str_parsed = vmalloc(len + 1);
    str_parsed[len] = 0;
    memcpy(str_parsed, str_tmp, len);
    vfree(str_tmp);

    if (move)
        vfree(str);

    return str_parsed;
}

void akvcam_settings_element_free(akvcam_settings_element_t element)
{
    if (element->group) {
        vfree(element->group);
        element->group = NULL;
    }

    if (element->key) {
        vfree(element->key);
        element->key = NULL;
    }

    if (element->value) {
        vfree(element->value);
        element->value = NULL;
    }
}

akvcam_string_map_t akvcam_settings_group_configs(const akvcam_settings_t self)
{
    akvcam_string_map_t group_configs;

    if (self->current_group)
        group_configs = akvcam_map_value(self->configs, self->current_group);
    else
        group_configs = akvcam_map_value(self->configs, "*");

    return group_configs;
}
