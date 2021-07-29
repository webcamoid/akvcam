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

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "utils.h"

static struct akvcam_utils
{
    uint64_t id;
    int last_error;
} akvcam_utils_private;

typedef struct
{
    __u32 cmd;
    char str[32];
} akvcam_utils_ioctl_strings, *akvcam_utils_ioctl_strings_t;

typedef struct
{
    int error;
    char str[32];
    char description[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_error_strings, *akvcam_utils_error_strings_t;

typedef struct
{
    enum v4l2_buf_type type;
    char  str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_buf_type_strings, *akvcam_utils_buf_type_strings_t;

typedef struct
{
    enum v4l2_field field;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_field_strings, *akvcam_utils_field_strings_t;

typedef struct
{
    enum v4l2_frmsizetypes type;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_frmsize_type_strings, *akvcam_utils_frmsize_type_strings_t;

typedef struct
{
    __u32 pixelformat;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_pixelformat_strings, *akvcam_utils_pixelformat_strings_t;

typedef struct
{
    enum v4l2_memory memory;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_v4l2_memory_strings, *akvcam_utils_v4l2_memory_strings_t;

typedef struct
{
    enum v4l2_colorspace colorspace;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_v4l2_colorspace_strings, *akvcam_utils_v4l2_colorspace_strings_t;

typedef struct
{
    AKVCAM_RW_MODE rw_mode;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_rw_mode_strings, *akvcam_utils_rw_mode_strings_t;

typedef struct
{
    __u32 flag;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_buffer_flags_strings, *akvcam_utils_buffer_flags_strings_t;

typedef struct
{
    __u32 flag;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_buffer_capabilities_strings, *akvcam_utils_buffer_capabilities_strings_t;

typedef struct
{
    __s32 ctrl_which;
    char str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_ctrl_which_class_strings, *akvcam_utils_ctrl_which_class_strings_t;

uint64_t akvcam_id(void)
{
    return akvcam_utils_private.id++;
}

int akvcam_get_last_error(void)
{
    return akvcam_utils_private.last_error;
}

int akvcam_set_last_error(int error)
{
    akvcam_utils_private.last_error = error;

    return error;
}

void akvcam_string_from_error(int error, char *str, size_t len)
{
    size_t i;
    static const akvcam_utils_error_strings error_strings[] = {
        {EPERM	, "EPERM"  , "Operation not permitted"            },
        {ENOENT	, "ENOENT" , "No such file or directory"          },
        {ESRCH	, "ESRCH"  , "No such process"                    },
        {EINTR	, "EINTR"  , "Interrupted system call"            },
        {EIO	, "EIO"    , "I/O error"                          },
        {ENXIO	, "ENXIO"  , "No such device or address"          },
        {E2BIG	, "E2BIG"  , "Argument list too long"             },
        {ENOEXEC, "ENOEXEC", "Exec format error"                  },
        {EBADF	, "EBADF"  , "Bad file number"                    },
        {ECHILD	, "ECHILD" , "No child processes"                 },
        {EAGAIN	, "EAGAIN" , "Try again"                          },
        {ENOMEM	, "ENOMEM" , "Out of memory"                      },
        {EACCES	, "EACCES" , "Permission denied"                  },
        {EFAULT	, "EFAULT" , "Bad address"                        },
        {ENOTBLK, "ENOTBLK", "Block device required"              },
        {EBUSY	, "EBUSY"  , "Device or resource busy"            },
        {EEXIST	, "EEXIST" , "File exists"                        },
        {EXDEV	, "EXDEV"  , "Cross-device link"                  },
        {ENODEV	, "ENODEV" , "No such device"                     },
        {ENOTDIR, "ENOTDIR", "Not a directory"                    },
        {EISDIR	, "EISDIR" , "Is a directory"                     },
        {EINVAL	, "EINVAL" , "Invalid argument"                   },
        {ENFILE	, "ENFILE" , "File table overflow"                },
        {EMFILE	, "EMFILE" , "Too many open files"                },
        {ENOTTY	, "ENOTTY" , "Not a typewriter"                   },
        {ETXTBSY, "ETXTBSY", "Text file busy"                     },
        {EFBIG	, "EFBIG"  , "File too large"                     },
        {ENOSPC	, "ENOSPC" , "No space left on device"            },
        {ESPIPE	, "ESPIPE" , "Illegal seek"                       },
        {EROFS	, "EROFS"  , "Read-only file system "             },
        {EMLINK	, "EMLINK" , "Too many links"                     },
        {EPIPE	, "EPIPE"  , "Broken pipe"                        },
        {EDOM	, "EDOM"   , "Math argument out of domain of func"},
        {ERANGE	, "ERANGE" , "Math result not representable"      },
        {0      , ""       , ""                                   },
    };

    memset(str, 0, len);

    for (i = 0; akvcam_strlen(error_strings[i].str) > 0; i++)
        if (error_strings[i].error == -error) {
            snprintf(str,
                     len,
                     "%s (%s)",
                     error_strings[i].description,
                     error_strings[i].str);

            return;
        }

    snprintf(str, len, "Unknown error (%d)", error);
}

void akvcam_string_from_rw_mode(AKVCAM_RW_MODE rw_mode, char *str, size_t len)
{
    size_t i = 0;
    size_t j = 0;
    size_t n;
    static const akvcam_utils_rw_mode_strings rw_mode_strings[] = {
        {AKVCAM_RW_MODE_READWRITE, "rw"     },
        {AKVCAM_RW_MODE_MMAP     , "mmap"   },
        {AKVCAM_RW_MODE_USERPTR  , "userptr"},
        {AKVCAM_RW_MODE_DMABUF   , "dmabuf" },
        {0                       , ""       },
    };

    memset(str, 0, len);
    n = snprintf(str, len, "AKVCAM_RW_MODE(");

    for (i = 0; akvcam_strlen(rw_mode_strings[i].str) > 0; i++)
        if (rw_mode_strings[i].rw_mode & rw_mode) {
            if (j > 0)
                n += snprintf(str + n, len - n, ", ");

            n += snprintf(str + n, len - n, "%s", rw_mode_strings[i].str);
            j++;
        }

    snprintf(str + n, len - n, ")");
}

char *akvcam_strdup(const char *str, AKVCAM_MEMORY_TYPE type)
{
    char *str_dup;
    size_t len = akvcam_strlen(str);

    if (type == AKVCAM_MEMORY_TYPE_KMALLOC)
        str_dup = kmalloc(len + 1, GFP_KERNEL);
    else
        str_dup = vmalloc(len + 1);

    str_dup[len] = 0;

    if (str)
        memcpy(str_dup, str, len + 1);

    return str_dup;
}

char *akvcam_strip_str(const char *str, AKVCAM_MEMORY_TYPE type)
{
    if (!str)
        return NULL;

    return akvcam_strip_str_sub(str,
                                0,
                                akvcam_strlen(str),
                                type);
}

char *akvcam_strip_str_sub(const char *str,
                           size_t from,
                           size_t size,
                           AKVCAM_MEMORY_TYPE type)
{
    char *stripped_str;
    ssize_t i;
    size_t len;
    size_t left;
    size_t stripped_len;

    len = akvcam_min(akvcam_strlen(str), from + size);

    for (i = (ssize_t) from; i < (ssize_t) len; i++)
        if (!isspace(str[i]))
            break;

    left = (size_t) i;

    if (left == len) {
        stripped_len = 0;
    } else {
        size_t right;

        for (i = (ssize_t) len - 1; i >= (ssize_t) from; i--)
            if (!isspace(str[i]))
                break;

        right = (size_t) i;
        stripped_len = right - left + 1;
    }

    if (type == AKVCAM_MEMORY_TYPE_KMALLOC)
        stripped_str = kmalloc(stripped_len + 1, GFP_KERNEL);
    else
        stripped_str = vmalloc(stripped_len + 1);

    stripped_str[stripped_len] = 0;

    if (stripped_len > 0)
        memcpy(stripped_str, str + left, stripped_len);

    return stripped_str;
}

void akvcam_replace(char *str, char from, char to)
{
    if (!str)
        return;

    for (; *str; str++)
        if (*str == from)
            *str = to;
}
