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

#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "buffer.h"
#include "log.h"
#include "utils.h"

struct akvcam_buffer
{
    struct kref ref;
    struct mutex mtx;
    struct v4l2_buffer buffer;
    void *data;
    bool mapped;
};

static const struct vm_operations_struct akvcam_buffer_vmops;

void akvcam_buffer_vmops_close(struct vm_area_struct *vma);

akvcam_buffer_t akvcam_buffer_new(size_t size)
{
    akvcam_buffer_t self = kzalloc(sizeof(struct akvcam_buffer), GFP_KERNEL);
    kref_init(&self->ref);
    mutex_init(&self->mtx);
    memset(&self->buffer, 0, sizeof(struct v4l2_buffer));
    self->buffer.bytesused = (__u32) size;
    self->data = vzalloc(size);

    return self;
}

static void akvcam_buffer_free(struct kref *ref)
{
    akvcam_buffer_t self = container_of(ref, struct akvcam_buffer, ref);
    vfree(self->data);
    kfree(self);
}

void akvcam_buffer_delete(akvcam_buffer_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_buffer_free);
}

akvcam_buffer_t akvcam_buffer_ref(akvcam_buffer_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

int akvcam_buffer_read(akvcam_buffer_t self, struct v4l2_buffer *v4l2_buff)
{
    int result;

    akpr_function();

    if (!v4l2_buff)
        return -EIO;

    result = mutex_lock_interruptible(&self->mtx);

    if (result)
        return result;

    memcpy(v4l2_buff, &self->buffer, sizeof(struct v4l2_buffer));
    mutex_unlock(&self->mtx);

    return 0;
}

int akvcam_buffer_read_userptr(akvcam_buffer_t self,
                               struct v4l2_buffer *v4l2_buff)
{
    int result;

    akpr_function();

    if (!v4l2_buff)
        return -EIO;

    if (v4l2_buff->length < 1 && v4l2_buff->bytesused < 1)
        return -EINVAL;

    result = mutex_lock_interruptible(&self->mtx);

    if (result)
        return result;

    memcpy(v4l2_buff, &self->buffer, sizeof(struct v4l2_buffer));
    result = 0;

    if (akvcam_v4l2_buf_type_is_mutiplanar(self->buffer.type)) {
        struct v4l2_plane *planes =
                kmalloc(v4l2_buff->length * sizeof(struct v4l2_plane), GFP_KERNEL);

        if (!copy_from_user(planes,
                            (char __user *) v4l2_buff->m.planes,
                            v4l2_buff->length * sizeof(struct v4l2_plane))) {
            size_t i;
            size_t offset = 0;

            for (i = 0; i < v4l2_buff->length; i++) {
                if (copy_to_user((char __user *) planes[i].m.userptr,
                                 (char *) self->data + offset,
                                 planes[i].bytesused)) {
                    akpr_err("Failed copying data to user space.\n");
                    result = -EIO;

                    break;
                }

                offset += planes[i].bytesused;
            }
        } else {
            akpr_err("Failed copying data from user space.\n");
            result = -EIO;
        }

        kfree(planes);
    } else {
        akpr_debug("Copying buffer data to user space.\n");

        if (copy_to_user((char __user *) v4l2_buff->m.userptr,
                         self->data,
                         self->buffer.bytesused)) {
            akpr_err("Failed copying data to user space.\n");
            result = -EIO;
        }
    }

    mutex_unlock(&self->mtx);

    return result;
}

int akvcam_buffer_write(akvcam_buffer_t self,
                        const struct v4l2_buffer *v4l2_buff)
{
    int result;

    akpr_function();

    if (!v4l2_buff)
        return -EIO;

    result = mutex_lock_interruptible(&self->mtx);

    if (result)
        return result;

    memcpy(&self->buffer, v4l2_buff, sizeof(struct v4l2_buffer));
    mutex_unlock(&self->mtx);

    return 0;
}

int akvcam_buffer_write_userptr(akvcam_buffer_t self,
                                const struct v4l2_buffer *v4l2_buff)
{
    int result;

    akpr_function();

    if (!v4l2_buff)
        return -EIO;

    if (v4l2_buff->length < 1 && v4l2_buff->bytesused < 1)
        return -EINVAL;

    result = mutex_lock_interruptible(&self->mtx);

    if (result)
        return result;

    memcpy(&self->buffer, v4l2_buff, sizeof(struct v4l2_buffer));
    result = 0;

    if (akvcam_v4l2_buf_type_is_mutiplanar(self->buffer.type)) {
        struct v4l2_plane *planes =
                kmalloc(v4l2_buff->length * sizeof(struct v4l2_plane), GFP_KERNEL);

        if (!copy_from_user(planes,
                            (char __user *) v4l2_buff->m.planes,
                            v4l2_buff->length * sizeof(struct v4l2_plane))) {
            size_t i;
            size_t offset = 0;

            for (i = 0; i < v4l2_buff->length; i++) {
                if (copy_from_user((char *) self->data + offset,
                                   (char __user *) planes[i].m.userptr,
                                   planes[i].bytesused)) {
                    akpr_err("Failed copying data from user space.\n");
                    result = -EIO;

                    break;
                }

                offset += planes[i].bytesused;
            }
        } else {
            akpr_err("Failed copying data from user space.\n");
            result = -EIO;
        }

        kfree(planes);
    } else {
        akpr_debug("Copying buffer data from user space.\n");

        if (copy_from_user(self->data,
                           (char __user *) v4l2_buff->m.userptr,
                           v4l2_buff->bytesused)) {
            akpr_err("Failed copying data from user space.\n");
            result = -EIO;
        }
    }

    mutex_unlock(&self->mtx);

    return result;
}

bool akvcam_buffer_read_data(akvcam_buffer_t self, void *data, size_t size)
{
    size_t copy_size = akvcam_min(size, self->buffer.bytesused);

    akpr_function();

    if (!data || copy_size < 1)
        return false;

    if (mutex_lock_interruptible(&self->mtx))
        return false;

    memcpy(data, self->data, copy_size);
    mutex_unlock(&self->mtx);

    return true;
}

bool akvcam_buffer_write_data(akvcam_buffer_t self,
                              const void *data,
                              size_t size)
{
    size_t copy_size = akvcam_min(size, self->buffer.bytesused);

    akpr_function();

    if (!data || copy_size < 1)
        return false;

    if (mutex_lock_interruptible(&self->mtx))
        return false;

    memcpy(self->data, data, copy_size);
    mutex_unlock(&self->mtx);

    return true;
}

int akvcam_buffer_map_data(akvcam_buffer_t self, struct vm_area_struct *vma)
{
    __u8 *data;
    unsigned long start = vma->vm_start;
    unsigned long size = vma->vm_end - vma->vm_start;
    int result = 0;

    akpr_function();
    akpr_debug("Buffer: %s\n", akvcam_string_from_v4l2_buffer(&self->buffer));

    if (mutex_lock_interruptible(&self->mtx))
        return -EIO;

    if (self->buffer.memory != V4L2_MEMORY_MMAP) {
        akpr_err("This is not a MMAP buffer.\n");
        mutex_unlock(&self->mtx);

        return -EPERM;
    }

    data = self->data;
    vma->vm_private_data = self;
    vma->vm_ops = &akvcam_buffer_vmops;

    if (data) {
        while (size > 0) {
            struct page *page = vmalloc_to_page(data);

            if (!page) {
                result = -EINVAL;

                break;
            }

            if (vm_insert_page(vma, start, page)) {
                result = -EAGAIN;

                break;
            }

            start += PAGE_SIZE;
            data += PAGE_SIZE;
            size -= PAGE_SIZE;
        }
    } else {
        result = -EINVAL;
    }

    if (result == 0)
        self->mapped = true;

    mutex_unlock(&self->mtx);

    return result;
}

bool akvcam_buffer_mapped(akvcam_buffer_t self)
{
    return self->mapped;
}

void akvcam_buffer_vmops_close(struct vm_area_struct *vma)
{
    akvcam_buffer_t self = vma->vm_private_data;

    akpr_function();
    self->mapped = false;
}

static const struct vm_operations_struct akvcam_buffer_vmops = {
    .close = akvcam_buffer_vmops_close,
};
