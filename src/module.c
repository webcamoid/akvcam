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

#include "driver.h"
#include "log.h"
#include "settings.h"

#define AKVCAM_DRIVER_NAME        "akvcam"
#define AKVCAM_DRIVER_DESCRIPTION "AkVCam Virtual Camera"

static int loglevel = 0;
module_param(loglevel, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(loglevel, "Debug verbosity (-2 to 7)");

static char config_file[4096] = "/etc/akvcam/config.ini";
module_param_string(config_file, config_file, 4096, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(config_file, "Full path to virtual cameras config file");

static int __init akvcam_init(void)
{
    akvcam_log_set_level(loglevel);
    akvcam_settings_set_file(config_file);

    return akvcam_driver_init(AKVCAM_DRIVER_NAME, AKVCAM_DRIVER_DESCRIPTION);
}

static void __exit akvcam_uninit(void)
{
    akvcam_driver_uninit();
}

module_init(akvcam_init)
module_exit(akvcam_uninit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gonzalo Exequiel Pedone");
MODULE_DESCRIPTION(AKVCAM_DRIVER_DESCRIPTION);
MODULE_VERSION("1.2.0");
