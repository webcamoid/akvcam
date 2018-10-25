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
#include <linux/vmalloc.h>
#include <media/v4l2-dev.h>

#include "node.h"
#include "buffers.h"
#include "device.h"
#include "events.h"
#include "format.h"
#include "ioctl.h"
#include "list.h"
#include "mmap.h"
#include "object.h"

struct akvcam_node
{
    akvcam_object_t self;
    akvcam_device_t device;
    akvcam_events_t events;
    akvcam_ioctl_t ioctls;
    bool non_blocking;
};

static struct v4l2_file_operations akvcam_fops;

akvcam_node_t akvcam_node_new(akvcam_device_t device)
{
    akvcam_node_t self = kzalloc(sizeof(struct akvcam_node), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_node_delete);
    self->device = device;
    self->events = akvcam_events_new();
    self->ioctls = akvcam_ioctl_new();

    return self;
}

void akvcam_node_delete(akvcam_node_t *self)
{
    akvcam_buffers_t buffers;
    akvcam_node_t priority_node;

    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    buffers = akvcam_device_buffers_nr((*self)->device);
    akvcam_buffers_deallocate(buffers, *self);
    priority_node = akvcam_device_priority_node((*self)->device);

    if (priority_node == *self)
        akvcam_device_set_priority((*self)->device,
                                   V4L2_PRIORITY_DEFAULT,
                                   NULL);

    akvcam_ioctl_delete(&((*self)->ioctls));
    akvcam_events_delete(&((*self)->events));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

akvcam_device_t akvcam_node_device_nr(const akvcam_node_t self)
{
    return self->device;
}

akvcam_device_t akvcam_node_device(const akvcam_node_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->device));

    return self->device;
}

akvcam_events_t akvcam_node_events_nr(const akvcam_node_t self)
{
    return self->events;
}

akvcam_events_t akvcam_node_events(const akvcam_node_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->events));

    return self->events;
}

bool akvcam_node_non_blocking(const akvcam_node_t self)
{
    return self->non_blocking;
}

void akvcam_node_set_non_blocking(akvcam_node_t self, bool non_blocking)
{
    self->non_blocking = non_blocking;
}

size_t akvcam_node_sizeof(void)
{
    return sizeof(struct akvcam_node);
}

struct v4l2_file_operations *akvcam_node_fops(void)
{
    return &akvcam_fops;
}

static int akvcam_node_open(struct file *filp)
{
    akvcam_device_t device;
    akvcam_nodes_list_t nodes;
    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);

    if (!device)
        return -ENOTTY;

    filp->private_data = akvcam_node_new(device);
    akvcam_node_set_non_blocking(filp->private_data,
                                 filp->f_flags & O_NONBLOCK);
    nodes = akvcam_device_nodes_nr(device);
    akvcam_list_push_back(nodes,
                          filp->private_data,
                          akvcam_node_sizeof(),
                          (akvcam_deleter_t) akvcam_node_delete,
                          true);
    akvcam_node_delete((akvcam_node_t *) &filp->private_data);

    return 0;
}

static ssize_t akvcam_node_read(struct file *filp,
                                char __user *data,
                                size_t size,
                                loff_t *offset)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    ssize_t bytes_read = 0;
    void *vdata;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    buffers = akvcam_device_buffers_nr(device);

    if (akvcam_buffers_allocated(buffers))
        return -EBUSY;

    if (size < 1)
        return 0;

    if (offset)
        *offset = 0;

    if (akvcam_device_prepare_frame(device)) {
        vdata = vmalloc(size);
        bytes_read = akvcam_buffers_read_rw(buffers,
                                            filp->private_data,
                                            vdata,
                                            size);
        copy_to_user(data, vdata, size);
        vfree(vdata);
        akvcam_buffers_notify_frame(buffers);
    }

    return bytes_read;
}

static ssize_t akvcam_node_write(struct file *filp,
                                 const char __user *data,
                                 size_t size, loff_t *offset)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    ssize_t bytes_written = 0;
    void *vdata;

    printk(KERN_INFO "%s()\n", __FUNCTION__);
    device = akvcam_device_from_file_nr(filp);
    buffers = akvcam_device_buffers_nr(device);

    if (akvcam_buffers_allocated(buffers))
        return -EBUSY;

    if (size < 1)
        return 0;

    if (offset)
        *offset = 0;

    vdata = vmalloc(size);
    copy_from_user(vdata, data, size);
    bytes_written = akvcam_buffers_write_rw(buffers,
                                            filp->private_data,
                                            vdata,
                                            size);
    vfree(vdata);

    return bytes_written;
}

static long akvcam_node_ioctl(struct file *filp,
                              unsigned int cmd,
                              unsigned long arg)
{
    akvcam_node_t node = filp->private_data;

    if (!node)
        return -ENOTTY;

    return akvcam_ioctl_do(node->ioctls, node, cmd, (void __user *) arg);
}

static __poll_t akvcam_node_poll(struct file *filp,
                                 struct poll_table_struct *wait)
{
    akvcam_node_t node = filp->private_data;
    akvcam_device_t device = akvcam_device_from_file_nr(filp);
    akvcam_buffers_t buffers = akvcam_device_buffers_nr(device);

    printk(KERN_INFO "%s()\n", __FUNCTION__);

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE
        && !akvcam_buffers_allocated(buffers)) {
        if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
            return AK_EPOLLOUT | AK_EPOLLWRNORM;
        else
            return AK_EPOLLIN | AK_EPOLLPRI | AK_EPOLLRDNORM;
    }

    return akvcam_events_poll(node->events, filp, wait);
}

static int akvcam_node_mmap(struct file *filp, struct vm_area_struct *vma)
{
    printk(KERN_INFO "%s()\n", __FUNCTION__);

    return akvcam_mmap_do(filp, vma);
}

static int akvcam_node_release(struct file *filp)
{
    akvcam_node_t node;
    akvcam_nodes_list_t nodes;
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
    .mmap           = akvcam_node_mmap   ,
    .release        = akvcam_node_release,
};
