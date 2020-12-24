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

#include <linux/device.h>
#include <linux/slab.h>
#include <media/v4l2-dev.h>

#include "attributes.h"
#include "controls.h"
#include "device.h"
#include "list.h"
#include "utils.h"

static const struct attribute_group *akvcam_attributes_capture_groups[2];
static const struct attribute_group *akvcam_attributes_output_groups[2];

typedef struct
{
    const char *name;
    __u32 id;
} akvcam_attributes_controls_map, *akvcam_attributes_controls_map_t;

static akvcam_attributes_controls_map akvcam_attributes_controls[] = {
    {"brightness"  ,     V4L2_CID_BRIGHTNESS},
    {"contrast"    ,       V4L2_CID_CONTRAST},
    {"saturation"  ,     V4L2_CID_SATURATION},
    {"hue"         ,            V4L2_CID_HUE},
    {"gamma"       ,          V4L2_CID_GAMMA},
    {"hflip"       ,          V4L2_CID_HFLIP},
    {"vflip"       ,          V4L2_CID_VFLIP},
    {"scaling"     ,      AKVCAM_CID_SCALING},
    {"aspect_ratio", AKVCAM_CID_ASPECT_RATIO},
    {"swap_rgb"    ,     AKVCAM_CID_SWAP_RGB},
    {"colorfx"     ,        V4L2_CID_COLORFX},
    {NULL          ,                       0},
};

size_t akvcam_attributes_controls_count(void);
__u32 akvcam_attributes_controls_id_by_name(const char *name);

struct akvcam_attributes
{
    struct kref ref;
    AKVCAM_DEVICE_TYPE device_type;
};

akvcam_attributes_t akvcam_attributes_new(AKVCAM_DEVICE_TYPE device_type)
{
    akvcam_attributes_t self = kzalloc(sizeof(struct akvcam_attributes), GFP_KERNEL);
    kref_init(&self->ref);
    self->device_type = device_type;

    return self;
}

static void akvcam_attributes_free(struct kref *ref)
{
    akvcam_attributes_t self = container_of(ref, struct akvcam_attributes, ref);
    kfree(self);
}

void akvcam_attributes_delete(akvcam_attributes_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_attributes_free);
}

akvcam_attributes_t akvcam_attributes_ref(akvcam_attributes_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_attributes_set(akvcam_attributes_t self, struct device *dev)
{
    dev->groups = self->device_type == AKVCAM_DEVICE_TYPE_OUTPUT?
                    akvcam_attributes_output_groups:
                    akvcam_attributes_capture_groups;
}

size_t akvcam_attributes_controls_count(void)
{
    static size_t count = 0;

    if (count < 1) {
        size_t i;

        for (i = 0; akvcam_attributes_controls[i].name; i++)
            count++;
    }

    return count;
}

__u32 akvcam_attributes_controls_id_by_name(const char *name)
{
    size_t count = akvcam_attributes_controls_count();
    size_t i;

    for (i = 0; i < count; i++)
        if (strcmp(akvcam_attributes_controls[i].name,
                   name) == 0)
            return akvcam_attributes_controls[i].id;

    return 0;
}

static ssize_t akvcam_attributes_connected_devices_show(struct device *dev,
                                                        struct device_attribute *attribute,
                                                        char *buffer)
{
    struct video_device *vdev = to_video_device(dev);
    akvcam_device_t device = video_get_drvdata(vdev);
    akvcam_devices_list_t devices;
    akvcam_list_element_t it = NULL;
    size_t space_left = PAGE_SIZE;
    size_t i;

    UNUSED(attribute);
    devices = akvcam_device_connected_devices_nr(device);
    memset(buffer, 0, PAGE_SIZE);

    for (i = 0; i < 64 && space_left > 0; i++) {
        int bytes_written;

        device = akvcam_list_next(devices, &it);

        if (!it)
            break;

        bytes_written = snprintf(buffer,
                                 space_left,
                                 "/dev/video%d\n",
                                 akvcam_device_num(device));
        buffer += bytes_written;
        space_left -= (size_t) bytes_written;
    }

    return (ssize_t) (PAGE_SIZE - space_left);
}

static ssize_t akvcam_attributes_streaming_devices_show(struct device *dev,
                                                        struct device_attribute *attribute,
                                                        char *buffer)
{
    struct video_device *vdev = to_video_device(dev);
    akvcam_device_t device = video_get_drvdata(vdev);
    akvcam_devices_list_t devices;
    akvcam_list_element_t it = NULL;
    size_t space_left = PAGE_SIZE;
    int bytes_written;
    size_t i;

    UNUSED(attribute);
    devices = akvcam_device_connected_devices_nr(device);
    memset(buffer, 0, PAGE_SIZE);

    for (i = 0; i < 64;) {
        device = akvcam_list_next(devices, &it);

        if (!it)
            break;

        if (akvcam_device_streaming(device)
            || akvcam_device_streaming_rw(device)) {
            bytes_written = snprintf(buffer,
                                     space_left,
                                     "/dev/video%d\n",
                                     akvcam_device_num(device));
            buffer += bytes_written;
            space_left -= (size_t) bytes_written;
            i++;
        }
    }

    return (ssize_t) (PAGE_SIZE - space_left);
}

static ssize_t akvcam_attributes_device_modes_show(struct device *dev,
                                                   struct device_attribute *attribute,
                                                   char *buffer)
{
    struct video_device *vdev = to_video_device(dev);
    akvcam_device_t device = video_get_drvdata(vdev);
    AKVCAM_RW_MODE mode = akvcam_device_rw_mode(device);
    size_t space_left = PAGE_SIZE;
    int bytes_written;

    UNUSED(attribute);
    memset(buffer, 0, PAGE_SIZE);

    if (mode & AKVCAM_RW_MODE_READWRITE) {
        bytes_written = snprintf(buffer, space_left, "rw\n");
        buffer += bytes_written;
        space_left -= (size_t) bytes_written;
    }

    if (mode & AKVCAM_RW_MODE_MMAP) {
        bytes_written = snprintf(buffer, space_left, "mmap\n");
        buffer += bytes_written;
        space_left -= (size_t) bytes_written;
    }

    if (mode & AKVCAM_RW_MODE_USERPTR) {
        bytes_written = snprintf(buffer, space_left, "usrptr\n");
        buffer += bytes_written;
        space_left -= (size_t) bytes_written;
    }

    return (ssize_t) (PAGE_SIZE - space_left);
}


static ssize_t akvcam_attributes_int_show(struct device *dev,
                                          struct device_attribute *attribute,
                                          char *buffer)
{
    struct video_device *vdev = to_video_device(dev);
    akvcam_device_t device = video_get_drvdata(vdev);
    akvcam_controls_t controls;
    struct v4l2_control control;

    controls = akvcam_device_controls_nr(device);
    memset(&control, 0, sizeof(struct v4l2_control));
    control.id = akvcam_attributes_controls_id_by_name(attribute->attr.name);
    akvcam_controls_get(controls, &control);
    memset(buffer, 0, PAGE_SIZE);

    return sprintf(buffer, "%d\n", control.value);
}

static ssize_t akvcam_attributes_int_store(struct device *dev,
                                           struct device_attribute *attribute,
                                           const char *buffer,
                                           size_t size)
{
    struct video_device *vdev = to_video_device(dev);
    akvcam_device_t device = video_get_drvdata(vdev);
    akvcam_controls_t controls;
    struct v4l2_control control;
    __s32 value = 0;
    int result;

    if (kstrtos32(buffer, 10, (__s32 *) &value) != 0)
        return -EINVAL;

    controls = akvcam_device_controls_nr(device);
    memset(&control, 0, sizeof(struct v4l2_control));
    control.id = akvcam_attributes_controls_id_by_name(attribute->attr.name);
    control.value = value;
    result = akvcam_controls_set(controls, &control);

    if (result)
        return result;

    return (ssize_t) size;
}

static ssize_t akvcam_attributes_menu_show(struct device *dev,
                                           struct device_attribute *attribute,
                                           char *buffer)
{
    struct video_device *vdev = to_video_device(dev);
    akvcam_device_t device = video_get_drvdata(vdev);
    akvcam_controls_t controls;
    struct v4l2_control control;
    struct v4l2_querymenu menu;

    controls = akvcam_device_controls_nr(device);
    memset(&control, 0, sizeof(struct v4l2_control));
    control.id = akvcam_attributes_controls_id_by_name(attribute->attr.name);
    akvcam_controls_get(controls, &control);
    memset(&menu, 0, sizeof(struct v4l2_querymenu));
    menu.id = control.id;
    menu.index = (__u32) control.value;
    akvcam_controls_fill_menu(controls, &menu);
    memset(buffer, 0, PAGE_SIZE);

    return sprintf(buffer, "%s\n", menu.name);
}

static ssize_t akvcam_attributes_menu_store(struct device *dev,
                                            struct device_attribute *attribute,
                                            const char *buffer,
                                            size_t size)
{
    struct video_device *vdev = to_video_device(dev);
    akvcam_device_t device = video_get_drvdata(vdev);
    akvcam_controls_t controls;
    struct v4l2_control control;
    struct v4l2_querymenu menu;
    char *buffer_stripped;
    __u32 id = akvcam_attributes_controls_id_by_name(attribute->attr.name);
    __u32 i;
    ssize_t result = -EINVAL;

    controls = akvcam_device_controls_nr(device);
    buffer_stripped = akvcam_strip_str(buffer, AKVCAM_MEMORY_TYPE_KMALLOC);

    for (i = 0;; i++) {
        memset(&menu, 0, sizeof(struct v4l2_querymenu));
        menu.id = id;
        menu.index = i;

        if (akvcam_controls_fill_menu(controls, &menu))
            break;

        if (strcmp((char *) menu.name, buffer_stripped) == 0) {
            memset(&control, 0, sizeof(struct v4l2_control));
            control.id = id;
            control.value = (__s32) i;

            if (akvcam_controls_set(controls, &control))
                break;

            result = (ssize_t) size;

            break;
        }
    }

    kfree(buffer_stripped);

    return result;
}

static DEVICE_ATTR(connected_devices,
                   S_IRUGO,
                   akvcam_attributes_connected_devices_show,
                   NULL);
static DEVICE_ATTR(listeners,
                   S_IRUGO,
                   akvcam_attributes_streaming_devices_show,
                   NULL);
static DEVICE_ATTR(broadcasters,
                   S_IRUGO,
                   akvcam_attributes_streaming_devices_show,
                   NULL);
static DEVICE_ATTR(modes,
                   S_IRUGO,
                   akvcam_attributes_device_modes_show,
                   NULL);
static DEVICE_ATTR(brightness,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);
static DEVICE_ATTR(contrast,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);
static DEVICE_ATTR(saturation,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);
static DEVICE_ATTR(hue,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);
static DEVICE_ATTR(gamma,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);
static DEVICE_ATTR(hflip,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);
static DEVICE_ATTR(colorfx,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_menu_show,
                   akvcam_attributes_menu_store);
static DEVICE_ATTR(vflip,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);
static DEVICE_ATTR(aspect_ratio,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_menu_show,
                   akvcam_attributes_menu_store);
static DEVICE_ATTR(scaling,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_menu_show,
                   akvcam_attributes_menu_store);
static DEVICE_ATTR(swap_rgb,
                   S_IRUGO | S_IWUSR,
                   akvcam_attributes_int_show,
                   akvcam_attributes_int_store);

// Define capture groups.

static struct attribute	*akvcam_attributes_capture[] = {
    &dev_attr_connected_devices.attr,
    &dev_attr_broadcasters.attr,
    &dev_attr_modes.attr,
    &dev_attr_brightness.attr,
    &dev_attr_contrast.attr,
    &dev_attr_saturation.attr,
    &dev_attr_hue.attr,
    &dev_attr_gamma.attr,
    &dev_attr_hflip.attr,
    &dev_attr_vflip.attr,
    &dev_attr_colorfx.attr,
    NULL
};

static struct attribute_group akvcam_attributes_capture_group = {
    .name = "controls",
    .attrs = akvcam_attributes_capture
};

static const struct attribute_group *akvcam_attributes_capture_groups[] = {
    &akvcam_attributes_capture_group,
    NULL
};

// Define output groups.

static struct attribute	*akvcam_attributes_output[] = {
    &dev_attr_connected_devices.attr,
    &dev_attr_listeners.attr,
    &dev_attr_modes.attr,
    &dev_attr_hflip.attr,
    &dev_attr_vflip.attr,
    &dev_attr_aspect_ratio.attr,
    &dev_attr_scaling.attr,
    &dev_attr_swap_rgb.attr,
    NULL
};

static struct attribute_group akvcam_attributes_output_group = {
    .name = "controls",
    .attrs = akvcam_attributes_output
};

static const struct attribute_group *akvcam_attributes_output_groups[] = {
    &akvcam_attributes_output_group,
    NULL
};
