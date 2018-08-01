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

#include "driver.h"
#include "device.h"
#include "list.h"
#include "utils.h"

typedef struct
{
    akvcam_object_t self;
    char name[AKVCAM_MAX_STRING_SIZE];
    char description[AKVCAM_MAX_STRING_SIZE];
    akvcam_list_t devices;
} akvcam_driver, *akvcam_driver_t;

static akvcam_driver_t akvcam_driver_global = NULL;

void akvcam_driver_delete(void *dummy)
{
    UNUSED(dummy);
    akvcam_driver_uninit();
}

bool akvcam_driver_init(const char *name, const char *description)
{
    akvcam_driver_t self;

    if (akvcam_driver_global)
        return false;

    self = kzalloc(sizeof(akvcam_driver), GFP_KERNEL);

    if (!self) {
        akvcam_set_last_error(-ENOMEM);

        goto akvcam_driver_new_failed;
    }

    self->self =
            akvcam_object_new(self, (akvcam_deleter_t) akvcam_driver_delete);

    if (!self->self)
        goto akvcam_driver_new_failed;

    snprintf(self->name, AKVCAM_MAX_STRING_SIZE, "%s", name);
    snprintf(self->description, AKVCAM_MAX_STRING_SIZE, "%s", description);
    self->devices = akvcam_list_new();

    if (!self->devices)
        goto akvcam_driver_new_failed;

    akvcam_driver_global = self;
    akvcam_set_last_error(0);

    return true;

akvcam_driver_new_failed:
    if (self) {
        akvcam_object_free(&AKVCAM_TO_OBJECT(self));
        kfree(self);
    }

    return false;
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

bool akvcam_driver_add_device(struct akvcam_device *device)
{
    if (!akvcam_driver_global)
        return false;

    akvcam_object_ref(AKVCAM_TO_OBJECT(device));

    return akvcam_list_push_back(akvcam_driver_global->devices,
                                 device,
                                 (akvcam_deleter_t) akvcam_device_delete);
}

bool akvcam_driver_add_device_nr(struct akvcam_device *device)
{
    if (!akvcam_driver_global)
        return false;

    return akvcam_list_push_back(akvcam_driver_global->devices, device, NULL);
}

bool akvcam_driver_add_device_own(struct akvcam_device *device)
{
    if (!akvcam_driver_global)
        return false;

    return akvcam_list_push_back(akvcam_driver_global->devices,
                                 device,
                                 (akvcam_deleter_t) akvcam_device_delete);
}

bool akvcam_driver_remove_device(struct akvcam_device *device)
{
    akvcam_list_element_t element;

    if (!akvcam_driver_global)
        return false;

    element = akvcam_list_find(akvcam_driver_global->devices,
                               device,
                               akvcam_device_sizeof(),
                               NULL);

    if (!element)
        return false;

    akvcam_list_erase(akvcam_driver_global->devices, element);

    return true;
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

bool akvcam_driver_register_devices()
{
    akvcam_list_element_t element = NULL;
    akvcam_device_t device;

    for (;;) {
        device = akvcam_list_next(akvcam_driver_global->devices, &element);

        if (!element)
            break;

        if (!akvcam_device_register(device)) {
            akvcam_driver_unregister_devices();

            return false;
        }
    }

    return true;
}

void akvcam_driver_unregister_devices()
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
