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
#include "driver.h"
#include "events.h"
#include "format.h"
#include "ioctl.h"
#include "list.h"
#include "log.h"

struct akvcam_node
{
    struct kref ref;
    int32_t device_num;
    akvcam_events_t events;
    akvcam_ioctl_t ioctls;
    int64_t id;
    bool non_blocking;
};

static struct v4l2_file_operations akvcam_fops;

akvcam_node_t akvcam_node_new(int32_t device_num)
{
    static int64_t node_id = 0;
    akvcam_node_t self = kzalloc(sizeof(struct akvcam_node), GFP_KERNEL);
    kref_init(&self->ref);
    self->device_num = device_num;
    self->events = akvcam_events_new();
    self->ioctls = akvcam_ioctl_new();
    self->id = node_id++;

    return self;
}

void akvcam_node_free(struct kref *ref)
{
    akvcam_buffers_t buffers;
    akvcam_node_t priority_node;
    akvcam_device_t device;

    akvcam_node_t self = container_of(ref, struct akvcam_node, ref);
    device = akvcam_driver_device_from_num_nr(self->device_num);

    if (device) {
        buffers = akvcam_device_buffers_nr(device);
        akvcam_buffers_deallocate(buffers, self);

        priority_node = akvcam_device_priority_node(device);

        if (priority_node == self)
            akvcam_device_set_priority(device,
                                       V4L2_PRIORITY_DEFAULT,
                                       NULL);
    }

    akvcam_ioctl_delete(self->ioctls);
    akvcam_events_delete(self->events);
    kfree(self);
}

void akvcam_node_delete(akvcam_node_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_node_free);
}

akvcam_node_t akvcam_node_ref(akvcam_node_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

int64_t akvcam_node_id(const akvcam_node_t self)
{
    return self->id;
}

int32_t akvcam_node_device_num(const akvcam_node_t self)
{
    return self->device_num;
}

akvcam_events_t akvcam_node_events_nr(const akvcam_node_t self)
{
    return self->events;
}

akvcam_events_t akvcam_node_events(const akvcam_node_t self)
{
    return akvcam_events_ref(self->events);
}

bool akvcam_node_non_blocking(const akvcam_node_t self)
{
    return self->non_blocking;
}

void akvcam_node_set_non_blocking(akvcam_node_t self, bool non_blocking)
{
    self->non_blocking = non_blocking;
}

struct v4l2_file_operations *akvcam_node_fops(void)
{
    return &akvcam_fops;
}

static int akvcam_node_open(struct file *filp)
{
    akvcam_device_t device;
    akvcam_nodes_list_t nodes;

    akpr_function();
    device = akvcam_device_from_file_nr(filp);

    if (!device)
        return -ENOTTY;

    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));
    filp->private_data = akvcam_node_new(akvcam_device_num(device));
    akvcam_node_set_non_blocking(filp->private_data,
                                 filp->f_flags & O_NONBLOCK);
    nodes = akvcam_device_nodes_nr(device);
    akvcam_list_push_back(nodes,
                          filp->private_data,
                          (akvcam_copy_t) akvcam_node_ref,
                          (akvcam_delete_t) akvcam_node_delete);
    akvcam_node_delete(filp->private_data);

    return 0;
}

static ssize_t akvcam_node_read(struct file *filp,
                                char __user *data,
                                size_t size,
                                loff_t *offset)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    ssize_t bytes_read;

    akpr_function();
    device = akvcam_device_from_file_nr(filp);
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_type(device) != AKVCAM_DEVICE_TYPE_CAPTURE
        || !(akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE))
        return -EINVAL;

    buffers = akvcam_device_buffers_nr(device);

    if (akvcam_buffers_allocated(buffers))
        return -EBUSY;

    if (size < 1)
        return 0;

    if (offset)
        *offset = 0;

    bytes_read = akvcam_buffers_read(buffers,
                                     filp->private_data,
                                     data,
                                     size);

    return bytes_read;
}

static ssize_t akvcam_node_write(struct file *filp,
                                 const char __user *data,
                                 size_t size,
                                 loff_t *offset)
{
    akvcam_node_t node;
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    ssize_t bytes_written;

    akpr_function();
    device = akvcam_device_from_file_nr(filp);
    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));

    if (akvcam_device_type(device) != AKVCAM_DEVICE_TYPE_OUTPUT
        || !(akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE))
        return -EINVAL;

    buffers = akvcam_device_buffers_nr(device);

    if (akvcam_buffers_allocated(buffers))
        return -EBUSY;

    if (size < 1)
        return 0;

    if (offset)
        *offset = 0;

    node = filp->private_data;

    if (!node)
        return -ENOTTY;

    akvcam_device_set_broadcasting_node(device, akvcam_node_id(node));
    akvcam_device_set_streaming_rw(device, true);
    bytes_written = akvcam_buffers_write(buffers,
                                         filp->private_data,
                                         data,
                                         size);

    if (bytes_written < 0)
        akvcam_device_set_streaming_rw(device, false);

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
    __poll_t result = 0;

    akpr_function();

    if (akvcam_device_rw_mode(device) & AKVCAM_RW_MODE_READWRITE
        && !akvcam_buffers_allocated(buffers)) {
        if (akvcam_device_type(device) == AKVCAM_DEVICE_TYPE_OUTPUT)
            return AK_EPOLLOUT | AK_EPOLLWRNORM;
        else
            return AK_EPOLLIN | AK_EPOLLPRI | AK_EPOLLRDNORM;
    }

    result = akvcam_events_poll(node->events, filp, wait);

    return result;
}

static int akvcam_node_mmap(struct file *filp, struct vm_area_struct *vma)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    int32_t device_num;
    int result;

    akpr_function();
    vma->vm_ops = NULL;
    vma->vm_private_data = filp->private_data;
    device_num = akvcam_node_device_num(filp->private_data);
    device = akvcam_driver_device_from_num_nr(device_num);

    if (!device)
        return -EIO;

    buffers = akvcam_device_buffers_nr(device);

    if (!buffers)
        return -EIO;

    result = akvcam_buffers_data_map(buffers,
                                     (__u32) (vma->vm_pgoff << PAGE_SHIFT),
                                     vma);

    if (result)
        akpr_err("MMAP failed: %s\n", akvcam_string_from_error(result));

    return result;
}

bool akvcam_node_nodes_are_equals(akvcam_node_t node1, akvcam_node_t node2)
{
    return node1 == node2;
}

static int akvcam_node_release(struct file *filp)
{
    akvcam_node_t node;
    akvcam_nodes_list_t nodes;
    akvcam_list_element_t it;
    akvcam_device_t device;

    akpr_function();
    device = akvcam_device_from_file_nr(filp);

    if (!device)
        return -ENOTTY;

    akpr_debug("Device: /dev/video%d\n", akvcam_device_num(device));
    node = filp->private_data;

    if (!node)
        return -ENOTTY;

    if (akvcam_node_id(node) == akvcam_device_broadcasting_node(device)) {
        akvcam_device_stop_streaming(device);
        akvcam_device_set_streaming_rw(device, false);
    }

    nodes = akvcam_device_nodes_nr(device);
    it = akvcam_list_find(nodes,
                          node,
                          (akvcam_are_equals_t) akvcam_node_nodes_are_equals);

    if (it) {
        akvcam_list_erase(nodes, it);
        filp->private_data = NULL;
    }

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
