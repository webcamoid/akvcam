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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <media/v4l2-dev.h>

#include "driver.h"
#include "device.h"
#include "format.h"
#include "list.h"
#include "utils.h"

#define AKVCAM_DRIVER_NAME        "akvcam"
#define AKVCAM_DRIVER_DESCRIPTION "AkVCam Virtual Camera"

static int __init akvcam_init(void)
{
    akvcam_device_t device;
    akvcam_format_t format;
    akvcam_list_t formats;
    struct v4l2_fract frame_rate = {30, 1};

    if (!akvcam_driver_init(AKVCAM_DRIVER_NAME, AKVCAM_DRIVER_DESCRIPTION))
        goto akvcam_init_failed;

    formats = akvcam_list_new();
    format = akvcam_format_new(V4L2_PIX_FMT_RGB24, 640, 480, &frame_rate);
    akvcam_list_push_back(formats,
                          format,
                          (akvcam_deleter_t) akvcam_format_delete);
    device = akvcam_device_new("akvcamtest",
                               AKVCAM_DEVICE_TYPE_CAPTURE,
                               formats);
    akvcam_list_delete(&formats);

    if (!device)
        goto akvcam_init_failed;

    if (!akvcam_driver_add_device_own(device))
        goto akvcam_init_failed;

    if (!akvcam_driver_register_devices())
        goto akvcam_init_failed;

    return 0;

akvcam_init_failed:
    akvcam_device_delete(&device);

    return akvcam_get_last_error();
}

static void __exit akvcam_uninit(void)
{
    akvcam_driver_uninit();
    printk(KERN_INFO "%s driver unloaded", AKVCAM_DRIVER_NAME);
}

module_init(akvcam_init)
module_exit(akvcam_uninit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gonzalo Exequiel Pedone");
MODULE_DESCRIPTION(AKVCAM_DRIVER_DESCRIPTION);
MODULE_VERSION("1.0.0");
