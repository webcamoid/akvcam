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

#define akvcam_callback(name, ...) \
    typedef void (*akvcam_##name##_proc)(void *user_data, __VA_ARGS__); \
    \
    typedef struct \
    { \
        void *user_data; \
        akvcam_##name##_proc callback; \
    } akvcam_##name##_callback, *akvcam_##name##_callback_t;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
    #define __poll_t       unsigned int
    #define AK_EPOLLIN     POLLIN
    #define AK_EPOLLPRI    POLLPRI
    #define AK_EPOLLOUT    POLLOUT
    #define AK_EPOLLRDNORM POLLRDNORM
    #define AK_EPOLLWRNORM POLLWRNORM
#else
    #define AK_EPOLLIN     EPOLLIN
    #define AK_EPOLLPRI    EPOLLPRI
    #define AK_EPOLLOUT    EPOLLOUT
    #define AK_EPOLLRDNORM EPOLLRDNORM
    #define AK_EPOLLWRNORM EPOLLWRNORM
#endif

typedef enum
{
    AKVCAM_MEMORY_TYPE_KMALLOC,
    AKVCAM_MEMORY_TYPE_VMALLOC,
} AKVCAM_MEMORY_TYPE;

typedef bool (*akvcam_are_equals_t)(const void *element_data,
                                    const void *data,
                                    size_t size);

uint64_t akvcam_id(void);
int akvcam_get_last_error(void);
int akvcam_set_last_error(int error);
const char *akvcam_string_from_ioctl(uint cmd);
size_t akvcam_line_size(const char *buffer, size_t size, bool *found);
char *akvcam_strdup(const char *str, AKVCAM_MEMORY_TYPE type);
char *akvcam_strip_str(const char *str, AKVCAM_MEMORY_TYPE type);
char *akvcam_strip_str_sub(const char *str,
                           size_t from,
                           size_t size,
                           AKVCAM_MEMORY_TYPE type);
char *akvcam_strip_move_str(char *str, AKVCAM_MEMORY_TYPE type);
size_t akvcam_str_count(const char *str, char c);
void akvcam_replace(char *str, char from, char to);

#endif // AKVCAM_UTILS_H
