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
#include <media/v4l2-dev.h>

#include "node.h"
#include "buffers.h"
#include "device.h"
#include "events.h"
#include "format.h"
#include "ioctl.h"
#include "list.h"
#include "mutex.h"
#include "object.h"

struct akvcam_node
{
    akvcam_object_t self;
    akvcam_device_t device;
    akvcam_format_t format;
    akvcam_buffers_t buffers;
    akvcam_events_t events;
    akvcam_ioctl_t ioctls;
    akvcam_mutex_t mutex;
};

static struct v4l2_file_operations akvcam_fops;

akvcam_node_t akvcam_node_new(struct akvcam_device *device)
{
    enum v4l2_buf_type buffer_type;
    akvcam_node_t self = kzalloc(sizeof(struct akvcam_node), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_node_delete);
    self->device = device;
    self->events = akvcam_events_new();
    buffer_type = akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT?
                V4L2_BUF_TYPE_VIDEO_OUTPUT: V4L2_BUF_TYPE_VIDEO_CAPTURE;
    self->buffers = akvcam_buffers_new(buffer_type);
    self->format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(self->format,
                       akvcam_list_at(akvcam_device_formats_nr(device), 0));
    self->ioctls = akvcam_ioctl_new();
    self->mutex = akvcam_mutex_new(AKVCAM_MUTEX_MODE_PERFORMANCE);

    return self;
}

void akvcam_node_delete(akvcam_node_t *self)
{
    akvcam_node_t priority_node;

    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    priority_node = akvcam_device_priority_node((*self)->device);

    if (priority_node == *self)
        akvcam_device_set_priority((*self)->device,
                                   V4L2_PRIORITY_DEFAULT,
                                   NULL);

    akvcam_mutex_delete(&((*self)->mutex));
    akvcam_ioctl_delete(&((*self)->ioctls));
    akvcam_buffers_delete(&((*self)->buffers));
    akvcam_format_delete(&((*self)->format));
    akvcam_events_delete(&((*self)->events));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

struct akvcam_device *akvcam_node_device_nr(akvcam_node_t self)
{
    return self->device;
}

struct akvcam_device *akvcam_node_device(akvcam_node_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->device));

    return self->device;
}

struct akvcam_format *akvcam_node_format_nr(akvcam_node_t self)
{
    return self->format;
}

struct akvcam_format *akvcam_node_format(akvcam_node_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->format));

    return self->format;
}

struct akvcam_buffers *akvcam_node_buffers_nr(akvcam_node_t self)
{
    return self->buffers;
}

struct akvcam_buffers *akvcam_node_buffers(akvcam_node_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->buffers));

    return self->buffers;
}

struct akvcam_events *akvcam_node_events_nr(akvcam_node_t self)
{
    return self->events;
}

struct akvcam_events *akvcam_node_events(akvcam_node_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->events));

    return self->events;
}

struct v4l2_file_operations *akvcam_node_fops(void)
{
    return &akvcam_fops;
}

int akvcam_node_open(struct file *filp)
{
    akvcam_device_t device;
    akvcam_list_tt(akvcam_node_t) nodes;
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);

    if (!device)
        return -ENOTTY;

    filp->private_data = akvcam_node_new(device);
    nodes = akvcam_device_nodes_nr(device);
    akvcam_list_push_back(nodes,
                          filp->private_data,
                          (akvcam_deleter_t) akvcam_node_delete);

    return 0;
}

ssize_t akvcam_node_read(struct file *filp,
                         char __user *data,
                         size_t size,
                         loff_t *offset)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    return 0;
}

ssize_t akvcam_node_write(struct file *filp,
                          const char __user *data,
                          size_t size, loff_t *offset)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    return (ssize_t) size;
}

long akvcam_node_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    akvcam_node_t node = filp->private_data;
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    if (!node)
        return -ENOTTY;

    return akvcam_ioctl_do(node->ioctls, node, cmd, (void *) arg);
}

unsigned int akvcam_node_poll(struct file *filp, struct poll_table_struct *wait)
{
    akvcam_node_t node = filp->private_data;
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    return akvcam_events_poll(node->events, filp, wait);
}

int akvcam_node_release(struct file *filp)
{
    akvcam_node_t node;
    akvcam_list_tt(akvcam_node_t) nodes;
    akvcam_list_element_t it;
    akvcam_device_t device;
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);

    if (!device)
        return -ENOTTY;

    node = filp->private_data;

    if (!node)
        return -ENOTTY;

    nodes = akvcam_device_nodes_nr(device);
    it = akvcam_list_find(nodes, node, 0, NULL);

    if (!it)
        return -ENOTTY;

    akvcam_list_erase(nodes, it);

    return 0;
}

static struct v4l2_file_operations akvcam_fops = {
    .owner          = THIS_MODULE        ,
    .open           = akvcam_node_open   ,
    .read           = akvcam_node_read   ,
    .write          = akvcam_node_write  ,
    .unlocked_ioctl = akvcam_node_ioctl  ,
    .poll           = akvcam_node_poll   ,
    .release        = akvcam_node_release,
};
