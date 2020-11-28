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

#include <linux/mm.h>
#include <linux/fs.h>

#include "mmap.h"
#include "buffers.h"
#include "device.h"
#include "driver.h"
#include "node.h"

int akvcam_mmap_map_data(struct vm_area_struct *vma, char *data);

int akvcam_mmap_do(struct file *filp, struct vm_area_struct *vma)
{
    akvcam_device_t device;
    akvcam_buffers_t buffers;
    int32_t device_num;
    void *data;
    int result;

    vma->vm_ops = NULL;
    vma->vm_private_data = filp->private_data;
    device_num = akvcam_node_device_num(filp->private_data);
    device = akvcam_driver_device_from_num_nr(device_num);

    if (!device)
        return -EIO;

    buffers = akvcam_device_buffers_nr(device);

    if (!buffers)
        return -EIO;

    data = akvcam_buffers_data(buffers, (__u32) (vma->vm_pgoff << PAGE_SHIFT));
    result = akvcam_mmap_map_data(vma, data);

    if (data)
        return result;

    return 0;
}

int akvcam_mmap_map_data(struct vm_area_struct *vma, char *data)
{
    struct page *page;
    unsigned long start = vma->vm_start;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (!data)
        return -EINVAL;

    while (size > 0) {
        page = vmalloc_to_page(data);

        if (!page)
            return -EINVAL;

        if (vm_insert_page(vma, start, page))
            return -EAGAIN;

        start += PAGE_SIZE;
        data += PAGE_SIZE;
        size -= PAGE_SIZE;
    }

    return 0;
}
