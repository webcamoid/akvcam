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
#define AKVCAM_WAIT_TIMEOUT_MSECS 1000

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
    #define AK_EPOLLIN     EPOLLIN
    #define AK_EPOLLPRI    EPOLLPRI
    #define AK_EPOLLOUT    EPOLLOUT
    #define AK_EPOLLRDNORM EPOLLRDNORM
    #define AK_EPOLLWRNORM EPOLLWRNORM
#else
    #define __poll_t       unsigned int
    #define AK_EPOLLIN     POLLIN
    #define AK_EPOLLPRI    POLLPRI
    #define AK_EPOLLOUT    POLLOUT
    #define AK_EPOLLRDNORM POLLRDNORM
    #define AK_EPOLLWRNORM POLLWRNORM
#endif

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    #define akvcam_userptr_is_valid(ptr, len) \
    ({ \
        uint64_t d = 0; \
        bool result = access_ok((ptr), (len)) && !get_user(d, (ptr)); \
        \
        result; \
    })
#else
    #define akvcam_userptr_is_valid(ptr, len) \
    ({ \
    uint64_t d = 0; \
    bool result = access_ok(VERIFY_WRITE, (ptr), (len)) \
                  && !get_user(d, (ptr)); \
    \
    result; \
    })
#endif

typedef enum
{
    AKVCAM_MEMORY_TYPE_KMALLOC,
    AKVCAM_MEMORY_TYPE_VMALLOC,
} AKVCAM_MEMORY_TYPE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
struct __kernel_timespec;
struct __kernel_v4l2_timeval;
#else
struct timespec;
struct timeval;
#endif

enum v4l2_buf_type;
enum v4l2_memory;
enum v4l2_field;
enum v4l2_colorspace;
enum v4l2_frmsizetypes;
struct v4l2_ext_controls;
struct v4l2_format;
struct v4l2_buffer;
struct v4l2_requestbuffers;
struct v4l2_create_buffers;
struct v4l2_frmsizeenum;

typedef bool (*akvcam_are_equals_t)(const void *element_data, const void *data);
typedef void *(*akvcam_copy_t)(void *data);
typedef void (*akvcam_delete_t)(void *data);

uint64_t akvcam_id(void);
int akvcam_get_last_error(void);
int akvcam_set_last_error(int error);
const char *akvcam_string_from_ioctl(uint cmd);
const char *akvcam_string_from_error(int error);
const char *akvcam_string_from_ioctl_error(uint cmd, int error);
const char *akvcam_string_from_v4l2_buf_type(enum v4l2_buf_type type);
const char *akvcam_string_from_rw_mode(AKVCAM_RW_MODE rw_mode);
const char *akvcam_string_from_v4l2_memory(enum v4l2_memory memory);
const char *akvcam_string_from_v4l2_format(const struct v4l2_format *format);
const char *akvcam_string_from_v4l2_buffer(const struct v4l2_buffer *buffer);
const char *akvcam_string_from_v4l2_buffer_flags(__u32 flags);
const char *akvcam_string_from_v4l2_field(enum v4l2_field field);
const char *akvcam_string_from_v4l2_requestbuffers(const struct v4l2_requestbuffers *reqbuffs);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
const char *akvcam_string_from_v4l2_buffer_capabilities(__u32 flags);
#endif
const char *akvcam_string_from_v4l2_create_buffers(const struct v4l2_create_buffers *buffers);
const char *akvcam_string_from_v4l2_pixelformat(__u32 pixelformat);
const char *akvcam_string_from_v4l2_colorspace(enum v4l2_colorspace colorspace);
const char *akvcam_string_from_v4l2_frmsizeenum(const struct v4l2_frmsizeenum *frame_sizes);
const char *akvcam_string_from_v4l2_frmsizetypes(enum v4l2_frmsizetypes type);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
const char *akvcam_string_from_ext_controls(const struct v4l2_ext_controls *ext_controls);
const char *akvcam_string_from_v4l2_ctrl_which(__s32 ctrl_which);
#endif
bool akvcam_v4l2_buf_type_is_mutiplanar(enum v4l2_buf_type type);
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
void akvcam_get_timespec(struct __kernel_timespec *tv);
void akvcam_get_timestamp(struct __kernel_v4l2_timeval *tv);
#else
void akvcam_get_timespec(struct timespec *tv);
void akvcam_get_timestamp(struct timeval *tv);
#endif

#endif // AKVCAM_UTILS_H
