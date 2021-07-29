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

#ifndef AKVCAM_UTILS_H
#define AKVCAM_UTILS_H

#include <linux/types.h>
#include <linux/version.h>

#include "device_types.h"

#define UNUSED(x) (void)(x)
#define AKVCAM_MAX_STRING_SIZE 1024

#define akvcam_min(value1, value2) \
    ((value1) < (value2)? (value1): (value2))

#define akvcam_max(value1, value2) \
    ((value1) > (value2)? (value1): (value2))

#define akvcam_abs(value) \
    ((value) < 0? -(value): (value))

#define akvcam_between(min, value, max) \
    ((value) >= (min) && (value) <= (max))

#define akvcam_bound(min, value, max) \
    ((value) < (min)? (min): (value) > (max)? (max): (value))

#define akvcam_align_up(value, align) \
    (((value) + (align) - 1) & ~((align) - 1))

#define akvcam_align32(value) akvcam_align_up(value, 32)

#define akvcam_mod(value, mod) \
    (((value) % (mod) + (mod)) % (mod))

#define akvcam_signal(class, signal, ...) \
    typedef int (*akvcam_##class##_##signal##_proc)(void *user_data, __VA_ARGS__); \
    \
    typedef struct \
    { \
        void *user_data; \
        akvcam_##class##_##signal##_proc callback; \
    } akvcam_##class##_##signal##_callback, *akvcam_##class##_##signal##_callback_t; \
    \
    void akvcam_##class##_set_##signal##_callback(akvcam_##class##_t self, \
                                                  const akvcam_##class##_##signal##_callback callback)

#define akvcam_signal_no_args(class, signal) \
    typedef int (*akvcam_##class##_##signal##_proc)(void *user_data); \
    \
    typedef struct \
    { \
        void *user_data; \
        akvcam_##class##_##signal##_proc callback; \
    } akvcam_##class##_##signal##_callback, *akvcam_##class##_##signal##_callback_t; \
    \
    void akvcam_##class##_set_##signal##_callback(akvcam_##class##_t self, \
                                                  const akvcam_##class##_##signal##_callback callback)

#define akvcam_signal_callback(class, signal) \
    akvcam_##class##_##signal##_callback signal##_callback

#define akvcam_signal_define(class, signal) \
    void akvcam_##class##_set_##signal##_callback(akvcam_##class##_t self, \
                                                  const akvcam_##class##_##signal##_callback callback) \
    { \
        self->signal##_callback = callback; \
    }

#define akvcam_connect(class, sender, signal, receiver, method) \
    do { \
        akvcam_##class##_##signal##_callback signal_callback; \
        signal_callback.user_data = receiver; \
        signal_callback.callback = (akvcam_##class##_##signal##_proc) method; \
        akvcam_##class##_set_##signal##_callback(sender, signal_callback); \
    } while (false)

#define akvcam_emit(self, signal, ...) \
    do { \
        if ((self)->signal##_callback.callback) \
            (self)->signal##_callback.callback(self->signal##_callback.user_data, \
                                               __VA_ARGS__); \
    } while (false)

#define akvcam_emit_no_args(self, signal) \
    do { \
        if ((self)->signal##_callback.callback) \
            (self)->signal##_callback.callback(self->signal##_callback.user_data); \
    } while (false)

#define akvcam_call(self, signal, ...) \
({ \
    int result = 0; \
    \
    if ((self)->signal##_callback.callback) \
        result = (self)->signal##_callback.callback(self->signal##_callback.user_data, \
                                                    __VA_ARGS__); \
    \
    result; \
})

#define akvcam_call_no_args(self, signal) \
({ \
    int result = 0; \
    \
    if ((self)->signal##_callback.callback) \
        result = (self)->signal##_callback.callback(self->signal##_callback.user_data); \
    \
    result; \
})

#define akvcam_init_field(v4l2_struct, field) \
    memset((v4l2_struct)->field, 0, sizeof((v4l2_struct)->field))

#define akvcam_init_reserved(v4l2_struct) \
    akvcam_init_field(v4l2_struct, reserved)

#define akvcam_wait_condition(wait_queue, condition, mtx, msecs) \
({ \
    int result; \
    int mutex_result; \
    \
    mutex_unlock(mtx); \
    result = wait_event_interruptible_timeout(wait_queue, \
                                              condition, \
                                              msecs_to_jiffies(msecs)); \
    mutex_result = mutex_lock_interruptible(mtx); \
    \
    if (mutex_result) \
        result = mutex_result; \
    \
    result; \
})

#define akvcam_strlen(str) \
({ \
    size_t len = 0; \
    \
    if (str) \
        len = strnlen(str, AKVCAM_MAX_STRING_SIZE); \
    \
    len; \
})

typedef enum
{
    AKVCAM_MEMORY_TYPE_KMALLOC,
    AKVCAM_MEMORY_TYPE_VMALLOC,
} AKVCAM_MEMORY_TYPE;

typedef bool (*akvcam_are_equals_t)(const void *element_data, const void *data);
typedef void *(*akvcam_copy_t)(void *data);
typedef void (*akvcam_delete_t)(void *data);

uint64_t akvcam_id(void);
int akvcam_get_last_error(void);
int akvcam_set_last_error(int error);
void akvcam_string_from_error(int error, char *str, size_t len);
void akvcam_string_from_rw_mode(AKVCAM_RW_MODE rw_mode, char *str, size_t len);
char *akvcam_strdup(const char *str, AKVCAM_MEMORY_TYPE type);
char *akvcam_strip_str(const char *str, AKVCAM_MEMORY_TYPE type);
char *akvcam_strip_str_sub(const char *str,
                           size_t from,
                           size_t size,
                           AKVCAM_MEMORY_TYPE type);
void akvcam_replace(char *str, char from, char to);

#endif // AKVCAM_UTILS_H
