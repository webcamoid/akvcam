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

#ifndef AKVCAM_DEVICE_TYPES_H
#define AKVCAM_DEVICE_TYPES_H

#include <linux/types.h>

#include "list_types.h"

#define AKVCAM_RW_MODE_READWRITE 0x1
#define AKVCAM_RW_MODE_MMAP      0x2
#define AKVCAM_RW_MODE_USERPTR   0x4

struct akvcam_device;
typedef struct akvcam_device *akvcam_device_t;
typedef akvcam_list_tt(akvcam_device_t) akvcam_devices_list_t;
typedef __u32 AKVCAM_RW_MODE;

typedef enum
{
    AKVCAM_DEVICE_TYPE_CAPTURE,
    AKVCAM_DEVICE_TYPE_OUTPUT,
} AKVCAM_DEVICE_TYPE;

#endif // AKVCAM_DEVICE_TYPES_H
