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
#include "frame.h"
#include "frame_filter.h"
#include "list.h"
#include "log.h"
#include "settings.h"

typedef struct
{
    AKVCAM_RW_MODE rw_mode;
    char  str[AKVCAM_MAX_STRING_SIZE];
    char  description[AKVCAM_MAX_STRING_SIZE];
} akvcam_driver_rw_mode_strings, *akvcam_driver_rw_mode_strings_t;
typedef const akvcam_driver_rw_mode_strings *akvcam_driver_rw_mode_strings_ct;

typedef struct
{
    char name[AKVCAM_MAX_STRING_SIZE];
    char description[AKVCAM_MAX_STRING_SIZE];
    akvcam_devices_list_t devices;
    akvcam_frame_t default_frame;
    akvcam_frame_filter_t frame_filter;
} akvcam_driver, *akvcam_driver_t;

static akvcam_driver_t akvcam_driver_global = NULL;

bool akvcam_driver_register(void);
void akvcam_driver_unregister(void);
akvcam_frame_t akvcam_driver_load_default_frame(akvcam_settings_t settings);
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
void akvcam_driver_print_formats(akvcam_device_ct device);
void akvcam_driver_print_connections(akvcam_device_ct device);
akvcam_driver_rw_mode_strings_ct akvcam_driver_rw_mode_strs(void);

int akvcam_driver_init(const char *name, const char *description)
{
    akvcam_settings_t settings;
    akvcam_matrix_t available_formats;

    akpr_function();

    if (akvcam_driver_global)
        return -EINVAL;

    akpr_info("Initializing driver\n");
    akvcam_driver_global = kzalloc(sizeof(akvcam_driver), GFP_KERNEL);
    snprintf(akvcam_driver_global->name, AKVCAM_MAX_STRING_SIZE, "%s", name);
    snprintf(akvcam_driver_global->description, AKVCAM_MAX_STRING_SIZE, "%s", description);
    akvcam_driver_global->default_frame = NULL;
    akvcam_driver_global->devices = NULL;
    akvcam_driver_global->frame_filter = akvcam_frame_filter_new();
    akpr_info("Reading settings\n");
    settings = akvcam_settings_new();

    if (akvcam_settings_load(settings, akvcam_settings_file())) {
        akvcam_driver_global->default_frame =
                akvcam_driver_load_default_frame(settings);
        available_formats = akvcam_driver_read_formats(settings);
        akvcam_driver_global->devices =
                akvcam_driver_read_devices(settings, available_formats);
        akvcam_list_delete(available_formats);
        akvcam_driver_connect_devices(settings, akvcam_driver_global->devices);
    } else {
        akpr_err("Error reading settings\n");
        akvcam_driver_global->default_frame = NULL;
        akvcam_driver_global->devices = akvcam_list_new();
    }

    akvcam_settings_delete(settings);
    akvcam_driver_register();
    akvcam_driver_print_devices();

    return 0;
}

void akvcam_driver_uninit(void)
{
    akpr_function();

    if (!akvcam_driver_global)
        return;

    akvcam_driver_unregister();
    akvcam_list_delete(akvcam_driver_global->devices);
    akvcam_frame_delete(akvcam_driver_global->default_frame);
    akvcam_frame_filter_delete(akvcam_driver_global->frame_filter);
    kfree(akvcam_driver_global);
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

bool akvcam_driver_register(void)
{
    akvcam_list_element_t element = NULL;

    // Register numbered devices first.
    for (;;) {
        akvcam_device_t device =
                akvcam_list_next(akvcam_driver_global->devices, &element);

        if (!element)
            break;

        if (akvcam_device_num(device) >= 0)
            akvcam_device_register(device);
    }

    // Register the remaining devices
    element = NULL;

    for (;;) {
        akvcam_device_t device =
                akvcam_list_next(akvcam_driver_global->devices, &element);

        if (!element)
            break;

        if (akvcam_device_is_registered(device))
            continue;

        akvcam_device_set_num(device, -1);

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

    for (;;) {
        akvcam_device_t device =
                akvcam_list_next(akvcam_driver_global->devices, &element);

        if (!element)
            break;

        akvcam_device_unregister(device);
    }
}

akvcam_frame_t  akvcam_driver_load_default_frame(akvcam_settings_t settings)
{
    char *file_name;
    akvcam_frame_t frame;
    bool loaded;

    akvcam_settings_begin_group(settings, "General");
    file_name = akvcam_settings_value(settings, "default_frame");
    frame = akvcam_frame_new(NULL, NULL, 0);
    loaded = akvcam_frame_load(frame, file_name);
    akvcam_settings_end_group(settings);

    if (!loaded) {
        akvcam_frame_delete(frame);

        return NULL;
    }

    return frame;
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
                              (akvcam_copy_t) akvcam_list_ref,
                              (akvcam_delete_t) akvcam_list_delete);
        akvcam_list_delete(formats_list);
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
                          (akvcam_copy_t) akvcam_list_ref,
                          (akvcam_delete_t) akvcam_list_delete);
    akvcam_list_push_back(format_matrix,
                          widths,
                          (akvcam_copy_t) akvcam_list_ref,
                          (akvcam_delete_t) akvcam_list_delete);
    akvcam_list_push_back(format_matrix,
                          heights,
                          (akvcam_copy_t) akvcam_list_ref,
                          (akvcam_delete_t) akvcam_list_delete);
    akvcam_list_push_back(format_matrix,
                          frame_rates,
                          (akvcam_copy_t) akvcam_list_ref,
                          (akvcam_delete_t) akvcam_list_delete);

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
                                  (akvcam_copy_t) akvcam_format_ref,
                                  (akvcam_delete_t) akvcam_format_delete);

        akvcam_format_delete(format);
    }

akvcam_driver_read_format_failed:
    akvcam_list_delete(combined_formats);
    akvcam_list_delete(frame_rates);
    akvcam_list_delete(heights);
    akvcam_list_delete(widths);
    akvcam_list_delete(pix_formats);
    akvcam_list_delete(format_matrix);

    return formats;
}

akvcam_devices_list_t akvcam_driver_read_devices(akvcam_settings_t settings,
                                                 akvcam_matrix_t available_formats)
{
    akvcam_devices_list_t devices = akvcam_list_new();
    size_t n_cameras;
    size_t i;

    akvcam_settings_begin_group(settings, "Cameras");
    n_cameras = akvcam_settings_begin_array(settings, "cameras");

    for (i = 0; i < n_cameras; i++) {
        akvcam_device_t device;
        akvcam_settings_set_array_index(settings, i);
        device = akvcam_driver_read_device(settings, available_formats);

        if (device) {
            akvcam_list_push_back(devices,
                                  device,
                                  (akvcam_copy_t) akvcam_device_ref,
                                  (akvcam_delete_t) akvcam_device_delete);
            akvcam_device_delete(device);
        }
    }

    akvcam_settings_end_array(settings);
    akvcam_settings_end_group(settings);

    return devices;
}

static bool akvcam_driver_strings_are_equals(const char *str1, const char *str2)
{
    return strcmp(str1, str2) == 0;
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
    akvcam_driver_rw_mode_strings_ct rw_mode_strings =
            akvcam_driver_rw_mode_strs();
    char *rw_mode_str;
    size_t i;

    akpr_info("Reading device\n");

    type = strcmp(akvcam_settings_value(settings, "type"),
                  "output") == 0? AKVCAM_DEVICE_TYPE_OUTPUT:
                                  AKVCAM_DEVICE_TYPE_CAPTURE;
    description = akvcam_settings_value(settings, "description");

    if (akvcam_strlen(description) < 1) {
        pr_err("Device description is empty\n");

        return NULL;
    }

    modes = akvcam_settings_value_list(settings, "mode", ",");
    mode = 0;

    for (i = 0; akvcam_strlen(rw_mode_strings[i].str) > 0; i++)
        if (akvcam_list_contains(modes,
                                 rw_mode_strings[i].str,
                                 (akvcam_are_equals_t) akvcam_driver_strings_are_equals)) {
            mode |= rw_mode_strings[i].rw_mode;
        }

    akvcam_list_delete(modes);

    if (!mode)
        mode |= AKVCAM_RW_MODE_MMAP | AKVCAM_RW_MODE_USERPTR;

    rw_mode_str = kzalloc(AKVCAM_MAX_STRING_SIZE, GFP_KERNEL);
    akvcam_string_from_rw_mode(mode, rw_mode_str, AKVCAM_MAX_STRING_SIZE);
    akpr_info("Device mode: %s\n", rw_mode_str);
    kfree(rw_mode_str);
    formats = akvcam_driver_read_device_formats(settings, available_formats);

    if (akvcam_list_empty(formats)) {
        pr_err("Can't read device formats\n");
        akvcam_list_delete(formats);

        return NULL;
    }

    device = akvcam_device_new("akvcam-device",
                               description,
                               type,
                               mode,
                               formats,
                               akvcam_driver_global->default_frame,
                               akvcam_driver_global->frame_filter);
    akvcam_list_delete(formats);

    if (akvcam_settings_contains(settings, "videonr"))
        akvcam_device_set_num(device,
                              akvcam_settings_value_int32(settings, "videonr"));

    if (!akvcam_device_v4l2_type(device)) {
        akvcam_device_delete(device);
        device = NULL;
    }

    return device;
}

akvcam_formats_list_t akvcam_driver_read_device_formats(akvcam_settings_t settings,
                                                        akvcam_matrix_t available_formats)
{
    akvcam_string_list_t formats_index;
    akvcam_formats_list_t formats = akvcam_list_new();
    akvcam_formats_list_t format_list;
    akvcam_list_element_t it = NULL;
    u32 index;

    formats_index = akvcam_settings_value_list(settings, "formats", ",");

    for (;;) {
        char *index_str = akvcam_list_next(formats_index, &it);

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

    akvcam_list_delete(formats_index);

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
    u32 *connections_index;
    char *index_str;
    u32 index;
    size_t i;
    size_t j;

    akvcam_settings_begin_group(settings, "Connections");
    n_connections = akvcam_settings_begin_array(settings, "connections");

    for (i = 0; i < n_connections; i++) {
        size_t n_nodes;

        akvcam_settings_set_array_index(settings, i);
        connections = akvcam_settings_value_list(settings, "connection", ":");
        n_nodes = akvcam_list_size(connections);

        if (n_nodes < 2) {
            akpr_warning("No valid connection defined\n");
            akvcam_list_delete(connections);

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

        akvcam_list_delete(connections);

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
                                              (akvcam_copy_t) akvcam_device_ref,
                                              (akvcam_delete_t) akvcam_device_delete);
                        akvcam_list_push_back(akvcam_device_connected_devices_nr(output),
                                              device,
                                              (akvcam_copy_t) akvcam_device_ref,
                                              (akvcam_delete_t) akvcam_device_delete);
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
    akvcam_list_element_t it = NULL;
    AKVCAM_RW_MODE rw_mode;
    akvcam_driver_rw_mode_strings_ct rw_mode_strings =
            akvcam_driver_rw_mode_strs();
    size_t i;

    if (!akvcam_driver_global
        || !akvcam_driver_global->devices
        || akvcam_list_empty(akvcam_driver_global->devices)) {
        akpr_warning("No devices found\n");

        return;
    }

    akpr_info("Virtual Devices:\n");
    akpr_info("\n");

    for (;;) {
        akvcam_device_t device =
                akvcam_list_next(akvcam_driver_global->devices, &it);

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
        rw_mode = akvcam_device_rw_mode(device);

        for (i = 0; akvcam_strlen(rw_mode_strings[i].str) > 0; i++)
            if (rw_mode_strings[i].rw_mode & rw_mode)
                akpr_info("\t\t%s\n", rw_mode_strings[i].description);

        akvcam_driver_print_formats(device);
        akvcam_driver_print_connections(device);
        akpr_info("\n");
    }
}

void akvcam_driver_print_formats(akvcam_device_ct device)
{
    akvcam_formats_list_t formats;
    akvcam_list_element_t it = NULL;

    formats = akvcam_device_formats(device);

    if (akvcam_list_empty(formats)) {
        akpr_warning("No formats defined\n");
    } else {
        akpr_info("\tFormats:\n");

        for (;;) {
            akvcam_format_t format = akvcam_list_next(formats, &it);

            if (!it)
                break;

            akpr_info("\t\t%s\n", akvcam_format_to_string(format));
        }
    }

    akvcam_list_delete(formats);
}

void akvcam_driver_print_connections(akvcam_device_ct device)
{
    akvcam_devices_list_t devices = akvcam_device_connected_devices_nr(device);
    akvcam_list_element_t it = NULL;

    if (akvcam_list_empty(devices)) {
        akpr_warning("No devices connected\n");

        return;
    }

    akpr_info("\tConnections:\n");

    for (;;) {
        akvcam_device_t connected_device = akvcam_list_next(devices, &it);

        if (!it)
            break;

        akpr_info("\t\t/dev/video%d\n", akvcam_device_num(connected_device));
    }
}

akvcam_driver_rw_mode_strings_ct akvcam_driver_rw_mode_strs(void)
{
    static const akvcam_driver_rw_mode_strings rw_mode_strings[] = {
        {AKVCAM_RW_MODE_READWRITE, "rw"     , "ReadWrite"},
        {AKVCAM_RW_MODE_MMAP     , "mmap"   , "MMap"     },
        {AKVCAM_RW_MODE_USERPTR  , "userptr", "UserPtr"  },
        {AKVCAM_RW_MODE_DMABUF   , "dmabuf" , "DMABuf"   },
        {0                       , ""       , ""         },
    };

    return rw_mode_strings;
}
