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
#include "settings.h"

typedef akvcam_list_tt(akvcam_format_t) akvcam_formats_list_t;
typedef akvcam_list_tt(akvcam_device_t) akvcam_devices_list_t;

typedef struct
{
    akvcam_object_t self;
    char name[AKVCAM_MAX_STRING_SIZE];
    char description[AKVCAM_MAX_STRING_SIZE];
    akvcam_devices_list_t devices;
} akvcam_driver, *akvcam_driver_t;

static akvcam_driver_t akvcam_driver_global = NULL;

bool akvcam_driver_register(void);
void akvcam_driver_unregister(void);
akvcam_formats_list_t akvcam_driver_read_formats(akvcam_settings_t settings);
akvcam_format_t akvcam_driver_read_format(akvcam_settings_t settings);
akvcam_devices_list_t akvcam_driver_read_devices(akvcam_settings_t settings,
                                                 akvcam_formats_list_t available_formats);
akvcam_device_t akvcam_driver_read_device(akvcam_settings_t settings,
                                          akvcam_formats_list_t available_formats);
akvcam_formats_list_t akvcam_driver_read_device_formats(akvcam_settings_t settings,
                                                        akvcam_formats_list_t available_formats);
void akvcam_driver_copy_formats(akvcam_formats_list_t to,
                                akvcam_formats_list_t from);

static void akvcam_driver_delete(void *dummy)
{
    UNUSED(dummy);
    akvcam_driver_uninit();
}

int akvcam_driver_init(const char *name, const char *description)
{
    akvcam_settings_t settings;
    akvcam_formats_list_t available_formats;

    if (akvcam_driver_global)
        return -EINVAL;

    akvcam_driver_global = kzalloc(sizeof(akvcam_driver), GFP_KERNEL);
    akvcam_driver_global->self =
            akvcam_object_new(akvcam_driver_global,
                              (akvcam_deleter_t) akvcam_driver_delete);
    snprintf(akvcam_driver_global->name, AKVCAM_MAX_STRING_SIZE, "%s", name);
    snprintf(akvcam_driver_global->description, AKVCAM_MAX_STRING_SIZE, "%s", description);
    settings = akvcam_settings_new();

    if (akvcam_settings_load(settings, "/etc/akvcam/config.ini")) {
        available_formats = akvcam_driver_read_formats(settings);
        akvcam_driver_global->devices =
                akvcam_driver_read_devices(settings, available_formats);
        akvcam_list_delete(&available_formats);
    } else {
        akvcam_driver_global->devices = akvcam_list_new();
    }

    akvcam_settings_delete(&settings);
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

akvcam_formats_list_t akvcam_driver_read_formats(akvcam_settings_t settings)
{
    akvcam_formats_list_t formats = akvcam_list_new();
    akvcam_format_t format;
    size_t n_formats;
    size_t i;

    akvcam_settings_begin_group(settings, "Formats");
    n_formats = akvcam_settings_begin_array(settings, "formats");

    for (i = 0; i < n_formats; i++) {
        akvcam_settings_set_array_index(settings, i);
        format = akvcam_driver_read_format(settings);

        if (format)
            akvcam_list_push_back(formats,
                                  format,
                                  akvcam_format_sizeof(),
                                  (akvcam_deleter_t) akvcam_format_delete,
                                  false);
    }

    akvcam_settings_end_array(settings);
    akvcam_settings_end_group(settings);

    return formats;
}

akvcam_format_t akvcam_driver_read_format(akvcam_settings_t settings)
{
    char *fourcc_str;
    __u32 pix_format;
    __u32 width;
    __u32 height;
    struct v4l2_fract frame_rate = {0, 1};
    akvcam_format_t format;

    fourcc_str = akvcam_settings_value(settings, "format");
    pix_format = akvcam_format_fourcc_from_string(fourcc_str);
    width = akvcam_settings_value_uint32(settings, "width");
    height = akvcam_settings_value_uint32(settings, "height");
    frame_rate.numerator = akvcam_settings_value_uint32(settings, "fps");
    format = akvcam_format_new(pix_format, width, height, &frame_rate);

    if (!akvcam_format_is_valid(format)) {
        akvcam_format_delete(&format);

        return NULL;
    }

    return format;
}

akvcam_devices_list_t akvcam_driver_read_devices(akvcam_settings_t settings,
                                                 akvcam_formats_list_t available_formats)
{
    akvcam_devices_list_t devices = akvcam_list_new();
    akvcam_device_t device;
    size_t n_cameras;
    size_t i;

    akvcam_settings_begin_group(settings, "Cameras");
    n_cameras = akvcam_settings_begin_array(settings, "cameras");

    for (i = 0; i < n_cameras; i++) {
        akvcam_settings_set_array_index(settings, i);
        device = akvcam_driver_read_device(settings, available_formats);

        if (device)
            akvcam_list_push_back(devices,
                                  device,
                                  akvcam_format_sizeof(),
                                  (akvcam_deleter_t) akvcam_device_delete,
                                  false);
    }

    akvcam_settings_end_array(settings);
    akvcam_settings_end_group(settings);

    return devices;
}

akvcam_device_t akvcam_driver_read_device(akvcam_settings_t settings,
                                          akvcam_formats_list_t available_formats)
{
    akvcam_device_t device;
    AKVCAM_DEVICE_TYPE type;
    akvcam_list_tt(char *) modes;
    AKVCAM_RW_MODE mode;
    char *description;
    akvcam_formats_list_t formats;
    akvcam_buffers_t buffers;

    type = strcmp(akvcam_settings_value(settings, "type"),
                  "output") == 0? AKVCAM_DEVICE_TYPE_OUTPUT:
                                  AKVCAM_DEVICE_TYPE_CAPTURE;
    description = akvcam_settings_value(settings, "description");

    if (strlen(description) < 1)
        return NULL;

    modes = akvcam_settings_value_list(settings, "mode", ",");
    mode = 0;

    if (akvcam_list_contains(modes,
                             "mmap",
                             strlen("mmap"),
                             NULL)) {
        mode |= AKVCAM_RW_MODE_MMAP;
    }

    if (akvcam_list_contains(modes,
                             "userptr",
                             strlen("userptr"),
                             NULL)) {
        mode |= AKVCAM_RW_MODE_USERPTR;
    }

    if (akvcam_list_contains(modes,
                             "rw",
                             strlen("rw"),
                             NULL)) {
        mode |= AKVCAM_RW_MODE_READWRITE;
    }

    akvcam_list_delete(&modes);

    if (!modes)
        mode |= AKVCAM_RW_MODE_MMAP | AKVCAM_RW_MODE_USERPTR;

    formats = akvcam_driver_read_device_formats(settings, available_formats);

    if (akvcam_list_empty(formats)) {
        akvcam_list_delete(&formats);

        return NULL;
    }

    device = akvcam_device_new("akvcam-device", description, type, mode);
    akvcam_driver_copy_formats(akvcam_device_formats_nr(device), formats);
    akvcam_format_copy(akvcam_device_format_nr(device),
                       akvcam_list_front(formats));
    buffers = akvcam_device_buffers_nr(device);
    akvcam_buffers_resize_rw(buffers, AKVCAM_BUFFERS_MIN);
    akvcam_list_delete(&formats);

    return device;
}

akvcam_formats_list_t akvcam_driver_read_device_formats(akvcam_settings_t settings,
                                                        akvcam_formats_list_t available_formats)
{
    akvcam_list_tt(char *) formats_index;
    akvcam_formats_list_t formats = akvcam_list_new();
    akvcam_format_t format;
    akvcam_format_t format_tmp;
    akvcam_list_element_t it = NULL;
    char *index_str;
    u32 index;

    formats_index = akvcam_settings_value_list(settings, "formats", ",");

    for (;;) {
        index_str = akvcam_list_next(formats_index, &it);

        if (!it)
            break;

        index = 0;

        if (kstrtou32(index_str, 10, (u32 *) &index) != 0)
            continue;

        format = akvcam_list_at(available_formats, index - 1);

        if (!format)
            continue;

        format_tmp = akvcam_format_new(0, 0, 0, NULL);
        akvcam_format_copy(format_tmp, format);
        akvcam_list_push_back(formats,
                              format_tmp,
                              akvcam_format_sizeof(),
                              (akvcam_deleter_t) akvcam_format_delete,
                              false);
    }

    akvcam_list_delete(&formats_index);

    return formats;
}

void akvcam_driver_copy_formats(akvcam_formats_list_t to,
                                akvcam_formats_list_t from)
{
    akvcam_format_t format;
    akvcam_list_element_t it = NULL;

    for (;;) {
        format = akvcam_list_next(from, &it);

        if (!it)
            break;

        akvcam_object_ref(AKVCAM_TO_OBJECT(format));
        akvcam_list_push_back(to,
                              format,
                              akvcam_format_sizeof(),
                              (akvcam_deleter_t) akvcam_format_delete,
                              false);
    }
}
