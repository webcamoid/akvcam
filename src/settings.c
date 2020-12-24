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
#include <linux/vmalloc.h>
#include <linux/videodev2.h>

#include "settings.h"
#include "file_read.h"
#include "list.h"
#include "log.h"
#include "map.h"
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
typedef const akvcam_settings_element *akvcam_settings_element_ct;

struct akvcam_settings
{
    struct kref ref;
    akvcam_map_tt(akvcam_string_map_t) configs;
    char *current_group;
    char *current_array;
    size_t array_index;
};

static struct akvcam_log
{
    char file_name[4096];
} akvcam_settings_private;

bool akvcam_settings_parse(const char *line, akvcam_settings_element_t element);
char *akvcam_settings_parse_string(char *str, bool move);
void akvcam_settings_element_free(akvcam_settings_element_t element);
akvcam_string_map_t akvcam_settings_group_configs(akvcam_settings_ct self);
char *akvcam_settings_string_copy(const char *str);

akvcam_settings_t akvcam_settings_new(void)
{
    akvcam_settings_t self = kzalloc(sizeof(struct akvcam_settings), GFP_KERNEL);
    kref_init(&self->ref);
    self->configs = akvcam_map_new();

    return self;
}

static void akvcam_settings_free(struct kref *ref)
{
    akvcam_settings_t self = container_of(ref, struct akvcam_settings, ref);
    akvcam_settings_end_array(self);
    akvcam_settings_end_group(self);
    akvcam_map_delete(self->configs);
    kfree(self);
}

void akvcam_settings_delete(akvcam_settings_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_settings_free);
}

akvcam_settings_t akvcam_settings_ref(akvcam_settings_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

bool akvcam_settings_load(akvcam_settings_t self, const char *file_name)
{
    akvcam_file_t config_file;
    akvcam_string_map_t group_configs;
    akvcam_settings_element element;
    char *current_group = NULL;

    memset(&element, 0, sizeof(akvcam_settings_element));
    akvcam_settings_clear(self);

    if (akvcam_strlen(file_name) < 1) {
        akpr_err("Settings file name not valid\n");

        return false;
    }

    config_file = akvcam_file_new(file_name);

    if (!akvcam_file_open(config_file)) {
        akpr_err("Can't open settings file: %s\n", file_name);

        goto akvcam_settings_load_failed;
    }

    while (!akvcam_file_eof(config_file)) {
        char *line = akvcam_file_read_line(config_file);

        if (!akvcam_settings_parse(line, &element)) {
            akpr_err("Error parsing settings file: %s\n", file_name);
            akpr_err("Line: %s\n", line);
            vfree(line);

            goto akvcam_settings_load_failed;
        }

        if (!akvcam_settings_element_empty(&element)) {
            if (akvcam_strlen(element.group) > 0) {
                if (current_group)
                    vfree(current_group);

                current_group = akvcam_strdup(element.group,
                                              AKVCAM_MEMORY_TYPE_VMALLOC);

                if (!akvcam_map_contains(self->configs, current_group)) {
                    akvcam_map_t tmp_map = akvcam_map_new();
                    akvcam_map_set_value(self->configs,
                                         current_group,
                                         tmp_map,
                                         (akvcam_copy_t) akvcam_map_ref,
                                         (akvcam_delete_t) akvcam_map_delete);
                    akvcam_map_delete(tmp_map);
                }
            } else if (akvcam_strlen(element.key) > 0
                       && akvcam_strlen(element.value) > 0) {
                if (!current_group) {
                    akvcam_map_t tmp_map = akvcam_map_new();
                    current_group = akvcam_strdup("General",
                                                  AKVCAM_MEMORY_TYPE_VMALLOC);
                    akvcam_map_set_value(self->configs,
                                         current_group,
                                         tmp_map,
                                         (akvcam_copy_t) akvcam_map_ref,
                                         (akvcam_delete_t) akvcam_map_delete);
                    akvcam_map_delete(tmp_map);
                }

                group_configs = akvcam_map_value(self->configs, current_group);
                akvcam_map_set_value(group_configs,
                                     element.key,
                                     element.value,
                                     (akvcam_copy_t) akvcam_settings_string_copy,
                                     (akvcam_delete_t) kfree);
            }

            akvcam_settings_element_free(&element);
        }

        vfree(line);
    }

    akvcam_file_delete(config_file);

    if (current_group)
        vfree(current_group);

    return true;

akvcam_settings_load_failed:
    akvcam_file_delete(config_file);
    akvcam_settings_clear(self);

    if (current_group)
        vfree(current_group);

    return false;
}

void akvcam_settings_begin_group(akvcam_settings_t self, const char *prefix)
{
    size_t len = akvcam_strlen(prefix);

    akvcam_settings_end_group(self);

    if (len > 0) {
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
    size_t len;
    char *array_key;
    char *array_size_str;
    size_t array_key_size;
    size_t array_size = 0;

    akvcam_settings_end_array(self);
    len = akvcam_strlen(prefix);

    if (!group_configs || len < 1)
        return 0;

    // Read array size.
    array_key_size = len + akvcam_strlen("/size") + 1;
    array_key = vmalloc(array_key_size);
    snprintf(array_key, array_key_size, "%s/size", prefix);
    array_size_str = akvcam_map_value(group_configs, array_key);
    vfree(array_key);
    kstrtou32(array_size_str, 10, (u32 *) &array_size);
    self->current_array = vmalloc(len + 1);
    self->current_array[len] = 0;
    memcpy(self->current_array, prefix, len);

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

akvcam_string_list_t akvcam_settings_groups(akvcam_settings_ct self)
{
    akvcam_map_element_t element = NULL;
    akvcam_string_list_t groups = akvcam_list_new();

    while (akvcam_map_next(self->configs, &element)) {
        char *group = akvcam_map_element_key(element);
        akvcam_list_push_back(groups,
                              group,
                              (akvcam_copy_t) akvcam_settings_string_copy,
                              (akvcam_delete_t) kfree);
    }

    return groups;
}

akvcam_string_list_t akvcam_settings_keys(akvcam_settings_ct self)
{
    akvcam_map_element_t element = NULL;
    akvcam_string_list_t keys = akvcam_list_new();
    akvcam_string_map_t group_configs = akvcam_settings_group_configs(self);

    if (!group_configs)
        return keys;

    while (akvcam_map_next(group_configs, &element)) {
        char *key = akvcam_map_element_key(element);
        akvcam_list_push_back(keys,
                              key,
                              (akvcam_copy_t) akvcam_settings_string_copy,
                              (akvcam_delete_t) kfree);
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

bool akvcam_settings_contains(akvcam_settings_ct self, const char *key)
{
    akvcam_string_map_t group_configs = akvcam_settings_group_configs(self);
    bool contains;

    if (!group_configs || akvcam_strlen(key) < 1)
        return false;

    if (self->current_array) {
        size_t array_key_size = akvcam_strlen(self->current_array)
                              + akvcam_strlen(key) + 23;
        char *array_key = vzalloc(array_key_size);
        snprintf(array_key,
                 array_key_size,
                 "%s/%zu/%s", self->current_array, self->array_index + 1, key);
        contains = akvcam_map_contains(group_configs, array_key);
    } else {
        contains = akvcam_map_contains(group_configs, key);
    }

    return contains;
}

char *akvcam_settings_value(akvcam_settings_ct self, const char *key)
{
    akvcam_string_map_t group_configs = akvcam_settings_group_configs(self);
    char *value;

    if (!group_configs || akvcam_strlen(key) < 1)
        return NULL;

    if (self->current_array) {
        size_t array_key_size = akvcam_strlen(self->current_array)
                              + akvcam_strlen(key) + 23;
        char *array_key = vzalloc(array_key_size);
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

bool akvcam_settings_value_bool(akvcam_settings_ct self, const char *key)
{
    return akvcam_settings_to_bool(akvcam_settings_value(self, key));
}

int32_t akvcam_settings_value_int32(akvcam_settings_ct self, const char *key)
{
    return akvcam_settings_to_int32(akvcam_settings_value(self, key));
}

uint32_t akvcam_settings_value_uint32(akvcam_settings_ct self, const char *key)
{
    return akvcam_settings_to_uint32(akvcam_settings_value(self, key));
}

akvcam_string_list_t akvcam_settings_value_list(akvcam_settings_ct self,
                                                const char *key,
                                                const char *separators)
{
    return akvcam_settings_to_list(akvcam_settings_value(self, key),
                                   separators);
}

struct v4l2_fract akvcam_settings_value_frac(akvcam_settings_ct self,
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
    char *stripped_element;
    akvcam_string_list_t result = akvcam_list_new();

    if (!value)
        return result;

    value_tmp_ptr = value_tmp =
            akvcam_strdup(value, AKVCAM_MEMORY_TYPE_VMALLOC);

    for (;;) {
        char *element = strsep(&value_tmp_ptr, separators);

        if (!element)
            break;

        stripped_element = akvcam_strip_str(element,
                                            AKVCAM_MEMORY_TYPE_KMALLOC);
        akvcam_list_push_back(result,
                              stripped_element,
                              (akvcam_copy_t) akvcam_settings_string_copy,
                              (akvcam_delete_t) kfree);
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

    akvcam_list_delete(frac_list);

    return frac;
}

const char *akvcam_settings_file(void)
{
    return akvcam_settings_private.file_name;
}

void akvcam_settings_set_file(const char *file_name)
{
    snprintf(akvcam_settings_private.file_name, 4096, "%s", file_name);
}

bool akvcam_settings_parse(const char *line, akvcam_settings_element_t element)
{
    char *pair_sep;
    size_t len = akvcam_strlen(line);
    size_t offset;

    memset(element, 0, sizeof(akvcam_settings_element));

    if (len < 1 || line[0] == '#' || line[0] == ';')
        return true;

    if (line[0] == '[') {
        if (line[len - 1] != ']' || akvcam_strlen(line) < 3)
            return false;

        element->group =
                akvcam_strip_str_sub(line,
                                     1,
                                     akvcam_strlen(line) - 2,
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
    akvcam_replace(element->key, '\\', '/');

    if (akvcam_strlen(element->key) < 1) {
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
    size_t len = akvcam_strlen(str);
    size_t start;
    size_t end;
    static const char escape_k[] = "'\"?\\abfnrtv0";
    static const char escape_v[] = "'\"?\\\a\b\f\n\r\t\v\0";

    if (len < 2) {
        if (move)
            return str;

        return akvcam_strdup(str, AKVCAM_MEMORY_TYPE_VMALLOC);
    }

    c = str[0];

    if ((c == '"' || c == '\'') && str[len - 1] == c) {
        start = 1;
        end  = len - 1;
    } else {
        start = 0;
        end  = len;
    }

    str_tmp = vzalloc(end - start + 1);
    hex[2] = 0;
    j = 0;

    for (i = start; i < end; i++) {
        if (i < len - 2 && str[i] == '\\') {
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

    len = akvcam_strlen(str_tmp);
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

akvcam_string_map_t akvcam_settings_group_configs(akvcam_settings_ct self)
{
    akvcam_string_map_t group_configs;

    if (self->current_group)
        group_configs = akvcam_map_value(self->configs, self->current_group);
    else
        group_configs = akvcam_map_value(self->configs, "General");

    return group_configs;
}

char *akvcam_settings_string_copy(const char *str)
{
    return kstrdup(str, GFP_KERNEL);
}
