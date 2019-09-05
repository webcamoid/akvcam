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

#ifndef AKVCAM_LOG_H
#define AKVCAM_LOG_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
    #define LOGLEVEL_ERR	 3
    #define LOGLEVEL_WARNING 4
    #define LOGLEVEL_INFO	 6
    #define LOGLEVEL_DEBUG	 7
#endif

#define akpr_file_name (strrchr(__FILE__, '/') + 1)
#define akpr_log_format "[akvcam] %s(%d): "

#define akpr_err(fmt, ...) \
    if (akvcam_log_level() >= LOGLEVEL_ERR) { \
        printk(KERN_ERR akpr_log_format fmt, \
               akpr_file_name, __LINE__, ##__VA_ARGS__); \
    }

#define akpr_warning(fmt, ...) \
    if (akvcam_log_level() >= LOGLEVEL_WARNING) { \
        printk(KERN_WARNING akpr_log_format fmt, \
               akpr_file_name, __LINE__, ##__VA_ARGS__); \
    }

#define akpr_info(fmt, ...) \
    if (akvcam_log_level() >= LOGLEVEL_INFO) { \
        printk(KERN_INFO akpr_log_format fmt, \
               akpr_file_name, __LINE__, ##__VA_ARGS__); \
    }

#define akpr_debug(fmt, ...) \
    if (akvcam_log_level() >= LOGLEVEL_DEBUG) { \
        printk(KERN_DEBUG akpr_log_format fmt, \
               akpr_file_name, __LINE__, ##__VA_ARGS__); \
    }

#define akpr_function() \
    akpr_debug("%s()\n", __FUNCTION__)

int akvcam_log_level(void);
void akvcam_log_set_level(int level);

#endif // AKVCAM_LOG_H
