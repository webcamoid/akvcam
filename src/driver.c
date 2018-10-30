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
#include "log.h"
#include "settings.h"

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
akvcam_matrix_t akvcam_driver_read_formats(akvcam_settings_t settings);
akvcam_formats_list_t akvcam_driver_read_format(akvcam_settings_t settings);
akvcam_devices_list_t akvcam_driver_read_devices(akvcam_settings_t settings,
                                                 akvcam_matrix_t available_formats);
akvcam_device_t akvcam_driver_read_device(akvcam_settings_t settings,
                                          akvcam_matrix_t available_formats);
akvcam_formats_list_t akvcam_driver_read_device_formats(akvcam_settings_t settings,
                                                        akvcam_matrix_t available_formats);
void akvcam_driver_connect_devices(akvcam_settings_t settings,
                                   akvcam_devices_list_t devices);
bool akvcam_driver_contains_node(const u32 *connections,
                                 size_t n_connectios,
                                 u32 node);
void akvcam_driver_print_devices(void);
void akvcam_driver_print_formats(const akvcam_device_t device);
void akvcam_driver_print_connections(const akvcam_device_t device);

static void akvcam_driver_delete(void *dummy)
{
    UNUSED(dummy);
    akvcam_driver_uninit();
}

int akvcam_driver_init(const char *name, const char *description)
{
    akvcam_settings_t settings;
    akvcam_matrix_t available_formats;

    if (akvcam_driver_global)
        return -EINVAL;

    akvcam_driver_global = kzalloc(sizeof(akvcam_driver), GFP_KERNEL);
    akvcam_driver_global->self =
            akvcam_object_new(akvcam_driver_global,
                              (akvcam_deleter_t) akvcam_driver_delete);
    snprintf(akvcam_driver_global->name, AKVCAM_MAX_STRING_SIZE, "%s", name);
    snprintf(akvcam_driver_global->description, AKVCAM_MAX_STRING_SIZE, "%s", description);
    settings = akvcam_settings_new();

    if (akvcam_settings_load(settings, akvcam_settings_file())) {
        available_formats = akvcam_driver_read_formats(settings);
        akvcam_driver_global->devices =
                akvcam_driver_read_devices(settings, available_formats);
        akvcam_list_delete(&available_formats);
        akvcam_driver_connect_devices(settings, akvcam_driver_global->devices);
    } else {
        akvcam_driver_global->devices = akvcam_list_new();
    }

    akvcam_settings_delete(&settings);
    akvcam_driver_register();
    akvcam_driver_print_devices();

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

akvcam_devices_list_t akvcam_driver_devices_nr(void)
{
    if (!akvcam_driver_global)
        return NULL;

    return akvcam_driver_global->devices;
}

akvcam_devices_list_t akvcam_driver_devices(void)
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

akvcam_matrix_t akvcam_driver_read_formats(akvcam_settings_t settings)
{
    akvcam_matrix_t formats_matrix = akvcam_list_new();
    akvcam_formats_list_t formats_list = NULL;
    size_t n_formats;
    size_t i;

    akvcam_settings_begin_group(settings, "Formats");
    n_formats = akvcam_settings_begin_array(settings, "formats");

    for (i = 0; i < n_formats; i++) {
        akvcam_settings_set_array_index(settings, i);
        formats_list = akvcam_driver_read_format(settings);
        akvcam_list_push_back(formats_matrix,
                              formats_list,
                              akvcam_list_sizeof(),
                              (akvcam_deleter_t) akvcam_list_delete,
                              true);
        akvcam_list_delete(&formats_list);
    }

    akvcam_settings_end_array(settings);
    akvcam_settings_end_group(settings);

    return formats_matrix;
}

akvcam_formats_list_t akvcam_driver_read_format(akvcam_settings_t settings)
{
    __u32 pix_format;
    uint32_t width;
    uint32_t height;
    struct v4l2_fract frame_rate;
    akvcam_format_t format;
    akvcam_formats_list_t formats;
    akvcam_string_matrix_t format_matrix;
    akvcam_string_matrix_t combined_formats = NULL;
    akvcam_string_list_t pix_formats;
    akvcam_string_list_t widths;
    akvcam_string_list_t heights;
    akvcam_string_list_t frame_rates;
    akvcam_string_list_t format_list;
    akvcam_list_element_t it = NULL;

    formats = akvcam_list_new();
    format_matrix = akvcam_list_new();

    pix_formats = akvcam_settings_value_list(settings, "format", ",");
    widths = akvcam_settings_value_list(settings, "width", ",");
    heights = akvcam_settings_value_list(settings, "height", ",");
    frame_rates = akvcam_settings_value_list(settings, "fps", ",");

    if (akvcam_list_empty(pix_formats)
        || akvcam_list_empty(widths)
        || akvcam_list_empty(heights)
        || akvcam_list_empty(frame_rates)) {
        akpr_err("Error reading formats\n");

        goto akvcam_driver_read_format_failed;
    }

    akvcam_list_push_back(format_matrix,
                          pix_formats,
                          akvcam_list_sizeof(),
                          (akvcam_deleter_t) akvcam_list_delete,
                          true);
    akvcam_list_push_back(format_matrix,
                          widths,
                          akvcam_list_sizeof(),
                          (akvcam_deleter_t) akvcam_list_delete,
                          true);
    akvcam_list_push_back(format_matrix,
                          heights,
                          akvcam_list_sizeof(),
                          (akvcam_deleter_t) akvcam_list_delete,
                          true);
    akvcam_list_push_back(format_matrix,
                          frame_rates,
                          akvcam_list_sizeof(),
                          (akvcam_deleter_t) akvcam_list_delete,
                          true);

    combined_formats = akvcam_matrix_combine(format_matrix);

    for (;;) {
        format_list = akvcam_list_next(combined_formats, &it);

        if (!it)
            break;

        pix_format = akvcam_format_fourcc_from_string(akvcam_list_at(format_list, 0));
        width = akvcam_settings_to_uint32(akvcam_list_at(format_list, 1));
        height = akvcam_settings_to_uint32(akvcam_list_at(format_list, 2));
        frame_rate = akvcam_settings_to_frac(akvcam_list_at(format_list, 3));
        format = akvcam_format_new(pix_format, width, height, &frame_rate);

        if (akvcam_format_is_valid(format))
            akvcam_list_push_back(formats,
                                  format,
                                  akvcam_format_sizeof(),
                                  (akvcam_deleter_t) akvcam_format_delete,
                                  true);

        akvcam_format_delete(&format);
    }

akvcam_driver_read_format_failed:
    akvcam_list_delete(&combined_formats);
    akvcam_list_delete(&frame_rates);
    akvcam_list_delete(&heights);
    akvcam_list_delete(&widths);
    akvcam_list_delete(&pix_formats);
    akvcam_list_delete(&format_matrix);

    return formats;
}

akvcam_devices_list_t akvcam_driver_read_devices(akvcam_settings_t settings,
                                                 akvcam_matrix_t available_formats)
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

        if (device) {
            akvcam_list_push_back(devices,
                                  device,
                                  akvcam_device_sizeof(),
                                  (akvcam_deleter_t) akvcam_device_delete,
                                  true);
            akvcam_device_delete(&device);
        }
    }

    akvcam_settings_end_array(settings);
    akvcam_settings_end_group(settings);

    return devices;
}

akvcam_device_t akvcam_driver_read_device(akvcam_settings_t settings,
                                          akvcam_matrix_t available_formats)
{
    akvcam_device_t device;
    AKVCAM_DEVICE_TYPE type;
    akvcam_string_list_t modes;
    AKVCAM_RW_MODE mode;
    char *description;
    akvcam_formats_list_t formats;
    akvcam_buffers_t buffers;
    bool multiplanar;

    type = strcmp(akvcam_settings_value(settings, "type"),
                  "output") == 0? AKVCAM_DEVICE_TYPE_OUTPUT:
                                  AKVCAM_DEVICE_TYPE_CAPTURE;
    description = akvcam_settings_value(settings, "description");

    if (strlen(description) < 1) {
        pr_err("Device description is empty\n");

        return NULL;
    }

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
        pr_err("Can't read device formats\n");
        akvcam_list_delete(&formats);

        return NULL;
    }

    multiplanar = akvcam_format_have_multiplanar(formats);
    device = akvcam_device_new("akvcam-device", description, type, mode);
    akvcam_list_append(akvcam_device_formats_nr(device), formats);
    akvcam_format_copy(akvcam_device_format_nr(device),
                       akvcam_list_front(formats));
    buffers = akvcam_device_buffers_nr(device);
    akvcam_buffers_resize_rw(buffers, AKVCAM_BUFFERS_MIN);
    akvcam_list_delete(&formats);
    akvcam_device_set_multiplanar(device, multiplanar);

    if (!akvcam_device_v4l2_type(device))
        akvcam_device_delete(&device);

    return device;
}

akvcam_formats_list_t akvcam_driver_read_device_formats(akvcam_settings_t settings,
                                                        akvcam_matrix_t available_formats)
{
    akvcam_string_list_t formats_index;
    akvcam_formats_list_t formats = akvcam_list_new();
    akvcam_formats_list_t format_list;
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

        format_list = akvcam_list_at(available_formats, index - 1);

        if (!format_list)
            continue;

        akvcam_list_append(formats, format_list);
    }

    akvcam_list_delete(&formats_index);

    return formats;
}

void akvcam_driver_connect_devices(akvcam_settings_t settings,
                                   akvcam_devices_list_t devices)
{
    akvcam_string_list_t connections;
    akvcam_devices_list_t connected_outputs;
    akvcam_list_element_t it = NULL;
    akvcam_device_t device;
    akvcam_device_t output;
    size_t n_connections;
    size_t n_nodes;
    u32 *connections_index;
    char *index_str;
    u32 index;
    size_t i;
    size_t j;

    akvcam_settings_begin_group(settings, "Connections");
    n_connections = akvcam_settings_begin_array(settings, "connections");

    for (i = 0; i < n_connections; i++) {
        akvcam_settings_set_array_index(settings, i);
        connections = akvcam_settings_value_list(settings, "connection", ":");
        n_nodes = akvcam_list_size(connections);

        if (n_nodes < 2) {
            akpr_warning("No valid connection defined\n");
            akvcam_list_delete(&connections);

            continue;
        }

        connections_index = kzalloc(sizeof(u32) * n_nodes, GFP_KERNEL);

        for (j = 0;; j++) {
            index_str = akvcam_list_next(connections, &it);

            if (!it)
                break;

            index = 0;

            if (kstrtou32(index_str, 10, (u32 *) &index) != 0) {
                akpr_err("No valid connection with: %s\n", index_str);

                break;
            }

            if (index < 1 || index > akvcam_list_size(devices)) {
                akpr_err("Out of range connection index: %u\n", index);

                break;
            }

            device = akvcam_list_at(devices, index - 1);

            if (j == 0
                && akvcam_device_type(device) != AKVCAM_DEVICE_TYPE_OUTPUT) {
                akpr_err("Index %u is not a output device\n", index);

                break;
            }

            if (j != 0
                && akvcam_device_type(device) != AKVCAM_DEVICE_TYPE_CAPTURE) {
                akpr_err("Index %u is not a capture device\n", index);

                break;
            }

            connections_index[j] =
                    akvcam_driver_contains_node(connections_index,
                                                n_nodes,
                                                index)?
                        0: index;
        }

        akvcam_list_delete(&connections);

        if (j == n_nodes)
            for (j = 0; j < n_nodes; j++) {
                if (j == 0) {
                    output = akvcam_list_at(devices, connections_index[j] - 1);
                } else {
                    if (!connections_index[j])
                        continue;

                    device = akvcam_list_at(devices, connections_index[j] - 1);
                    connected_outputs = akvcam_device_connected_devices_nr(device);

                    if (akvcam_list_empty(connected_outputs)) {
                        akvcam_list_push_back(connected_outputs,
                                              output,
                                              akvcam_device_sizeof(),
                                              (akvcam_deleter_t) akvcam_device_delete,
                                              true);
                        akvcam_list_push_back(akvcam_device_connected_devices_nr(output),
                                              device,
                                              akvcam_device_sizeof(),
                                              (akvcam_deleter_t) akvcam_device_delete,
                                              true);
                    } else {
                        akpr_warning("Connection between %u and %u rejected, "
                                     "because %u was already connected\n",
                                     connections_index[0] - 1,
                                     connections_index[j] - 1,
                                     connections_index[j] - 1);
                    }
                }
            }

        kfree(connections_index);
    }

    akvcam_settings_end_array(settings);
    akvcam_settings_end_group(settings);
}

bool akvcam_driver_contains_node(const u32 *connections,
                                 size_t n_connectios,
                                 u32 node)
{
    size_t i;

    for (i = 0; i < n_connectios; i++)
        if (connections[i] == node)
            return true;

    return false;
}

void akvcam_driver_print_devices(void)
{
    akvcam_device_t device;
    akvcam_list_element_t it = NULL;
    AKVCAM_RW_MODE mode;

    if (!akvcam_driver_global
        || !akvcam_driver_global->devices
        || akvcam_list_empty(akvcam_driver_global->devices)) {
        akpr_warning("No devices found\n");

        return;
    }

    akpr_info("Virtual Devices:\n");
    akpr_info("\n");

    for (;;) {
        device = akvcam_list_next(akvcam_driver_global->devices, &it);

        if (!it)
            break;

        akpr_info("Device: /dev/video%d\n", akvcam_device_num(device));
        akpr_info("\tDescription: %s\n", akvcam_device_description(device));

        if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT) {
            akpr_info("\tType: Output\n");
        } else {
            akpr_info("\tType: Capture\n");
        }

        akpr_info("\tModes:\n");
        mode = akvcam_device_rw_mode(device);

        if (mode & AKVCAM_RW_MODE_READWRITE)
            akpr_info("\t\tReadWrite\n");

        if (mode & AKVCAM_RW_MODE_MMAP)
            akpr_info("\t\tMMap\n");

        if (mode & AKVCAM_RW_MODE_USERPTR)
            akpr_info("\t\tUserPtr\n");

        if (mode & AKVCAM_RW_MODE_READWRITE) {
            akpr_info("\tUser Controls: No\n");
        } else {
            akpr_info("\tUser Controls: Yes\n");
        }

        akvcam_driver_print_formats(device);
        akvcam_driver_print_connections(device);
        akpr_info("\n");
    }
}

void akvcam_driver_print_formats(const akvcam_device_t device)
{
    akvcam_formats_list_t formats;
    akvcam_format_t format;
    akvcam_list_element_t it = NULL;
    struct v4l2_fract *frame_rate;

    formats = akvcam_device_formats_nr(device);

    if (akvcam_list_empty(formats)) {
        akpr_warning("No formats defined\n");

        return;
    }

    akpr_info("\tFormats:\n");

    for (;;) {
        format = akvcam_list_next(formats, &it);

        if (!it)
            break;

        frame_rate = akvcam_format_frame_rate(format);
        akpr_info("\t\t%s %zux%zu %u/%u Hz\n",
                  akvcam_format_fourcc_str(format),
                  akvcam_format_width(format),
                  akvcam_format_height(format),
                  frame_rate->numerator,
                  frame_rate->denominator);
    }
}

void akvcam_driver_print_connections(const akvcam_device_t device)
{
    akvcam_devices_list_t devices;
    akvcam_device_t connected_device;
    akvcam_list_element_t it = NULL;

    devices = akvcam_device_connected_devices_nr(device);

    if (akvcam_list_empty(devices)) {
        akpr_warning("No devices connected\n");

        return;
    }

    akpr_info("\tConnections:\n");

    for (;;) {
        connected_device = akvcam_list_next(devices, &it);

        if (!it)
            break;

        akpr_info("\t\t/dev/video%u\n", akvcam_device_num(connected_device));
    }
}
