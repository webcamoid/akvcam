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

#ifndef AKVCAM_DRIVER_H
#define AKVCAM_DRIVER_H

#include <linux/types.h>

#include "device_types.h"

// public static
int akvcam_driver_init(const char *name, const char *description);
void akvcam_driver_uninit(void);

const char *akvcam_driver_name(void);
const char *akvcam_driver_description(void);
uint akvcam_driver_version(void);
akvcam_devices_list_t akvcam_driver_devices_nr(void);
akvcam_devices_list_t akvcam_driver_devices(void);
akvcam_device_t akvcam_driver_device_from_num_nr(int32_t num);
akvcam_device_t akvcam_driver_device_from_num(int32_t num);

#endif // AKVCAM_DRIVER_H
