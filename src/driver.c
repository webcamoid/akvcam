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

#include <linux/slab.h>
#include <linux/version.h>
#include <linux/videodev2.h>

#include "driver.h"
#include "buffers.h"
#include "device.h"
#include "format.h"
#include "list.h"

typedef struct
{
    akvcam_object_t self;
    char name[AKVCAM_MAX_STRING_SIZE];
    char description[AKVCAM_MAX_STRING_SIZE];
    akvcam_list_tt(akvcam_device_t) devices;
} akvcam_driver, *akvcam_driver_t;

static akvcam_driver_t akvcam_driver_global = NULL;

bool akvcam_driver_register(void);
void akvcam_driver_unregister(void);

static void akvcam_driver_delete(void *dummy)
{
    UNUSED(dummy);
    akvcam_driver_uninit();
}

int akvcam_driver_init(const char *name, const char *description)
{
    akvcam_device_t device;
    akvcam_list_tt(akvcam_format_t) formats;
    akvcam_buffers_t buffers;
    struct v4l2_fract frame_rate = {30, 1};
    akvcam_format_t format;
    AKVCAM_RW_MODE mode = 0;

    if (akvcam_driver_global)
        return -EINVAL;

    akvcam_driver_global = kzalloc(sizeof(akvcam_driver), GFP_KERNEL);
    akvcam_driver_global->self = akvcam_object_new(akvcam_driver_global, (akvcam_deleter_t) akvcam_driver_delete);
    snprintf(akvcam_driver_global->name, AKVCAM_MAX_STRING_SIZE, "%s", name);
    snprintf(akvcam_driver_global->description, AKVCAM_MAX_STRING_SIZE, "%s", description);
    akvcam_driver_global->devices = akvcam_list_new();

//    mode |= AKVCAM_RW_MODE_READWRITE;
    mode |= AKVCAM_RW_MODE_MMAP;
    mode |= AKVCAM_RW_MODE_USERPTR;

    device = akvcam_device_new("akvcam-device",
                               AKVCAM_DEVICE_TYPE_CAPTURE,
                               mode);
    akvcam_list_push_back(akvcam_driver_global->devices,
                          device,
                          (akvcam_deleter_t) akvcam_device_delete);

    formats = akvcam_device_formats_nr(device);
    format = akvcam_format_new(V4L2_PIX_FMT_RGB24, 640, 480, &frame_rate);
    akvcam_list_push_back(formats,
                          format,
                          (akvcam_deleter_t) akvcam_format_delete);

    akvcam_format_copy(akvcam_device_format_nr(device), format);

    buffers = akvcam_device_buffers_nr(device);
    akvcam_buffers_resize_rw(buffers, AKVCAM_BUFFERS_MIN);

    akvcam_driver_register();

    return 0;
}

void akvcam_driver_uninit(void)
{
    if (!akvcam_driver_global)
        return;

    if (akvcam_object_unref(akvcam_driver_global->self) > 0)
        return;

    akvcam_list_delete(&akvcam_driver_global->devices);
    akvcam_object_free(&akvcam_driver_global->self);
    kfree(akvcam_driver_global);
    akvcam_driver_global = NULL;
}

const char *akvcam_driver_name(void)
{
    if (!akvcam_driver_global)
        return NULL;

    return akvcam_driver_global->name;
}

const char *akvcam_driver_description(void)
{
    if (!akvcam_driver_global)
        return NULL;

    return akvcam_driver_global->description;
}

uint akvcam_driver_version(void)
{
    return LINUX_VERSION_CODE;
}

struct akvcam_list *akvcam_driver_devices_nr(void)
{
    if (!akvcam_driver_global)
        return NULL;

    return akvcam_driver_global->devices;
}

struct akvcam_list *akvcam_driver_devices(void)
{
    if (!akvcam_driver_global)
        return NULL;

    akvcam_object_ref(AKVCAM_TO_OBJECT(akvcam_driver_global->devices));

    return akvcam_driver_global->devices;
}

bool akvcam_driver_register(void)
{
    akvcam_list_element_t element = NULL;
    akvcam_device_t device;

    for (;;) {
        device = akvcam_list_next(akvcam_driver_global->devices, &element);

        if (!element)
            break;

        if (!akvcam_device_register(device)) {
            akvcam_driver_unregister();

            return false;
        }
    }

    return true;
}

void akvcam_driver_unregister(void)
{
    akvcam_list_element_t element = NULL;
    akvcam_device_t device;

    for (;;) {
        device = akvcam_list_next(akvcam_driver_global->devices, &element);

        if (!element)
            break;

        akvcam_device_unregister(device);
    }
}
