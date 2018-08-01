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
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#include "device.h"
#include "controls.h"
#include "fops.h"
#include "format.h"
#include "ioctl_ops.h"
#include "list.h"
#include "object.h"
#include "queue.h"
#include "utils.h"

struct akvcam_device
{
    akvcam_object_t self;
    akvcam_controls_t controls;
    akvcam_list_t formats;
    akvcam_format_t format;
    akvcam_queue_t queue;
    struct v4l2_device v4l2_dev;
    struct video_device *vdev;
    AKVCAM_DEVICE_TYPE type;
};

akvcam_device_t akvcam_device_new(const char *name,
                                  AKVCAM_DEVICE_TYPE type,
                                  struct akvcam_list *formats)
{
    akvcam_device_t self = kzalloc(sizeof(struct akvcam_device), GFP_KERNEL);

    if (!self) {
        akvcam_set_last_error(-ENOMEM);

        goto akvcam_device_new_failed;
    }

    self->self =
            akvcam_object_new(self, (akvcam_deleter_t) akvcam_device_delete);

    if (!self->self)
        goto akvcam_device_new_failed;

    self->formats = formats;
    akvcam_object_ref(AKVCAM_TO_OBJECT(formats));

    if (!self->formats)
        goto akvcam_device_new_failed;

    self->format = akvcam_format_new(0, 0, 0, NULL);
    akvcam_format_copy(self->format, akvcam_list_at(formats, 0));

    if (!self->format)
        goto akvcam_device_new_failed;

    self->queue = akvcam_queue_new(self);

    if (!self->queue)
        goto akvcam_device_new_failed;

    self->controls = akvcam_controls_new();

    if (!self->controls)
        goto akvcam_device_new_failed;

    self->type = type;
    memset(&self->v4l2_dev, 0, sizeof(struct v4l2_device));
    snprintf(self->v4l2_dev.name,
             V4L2_DEVICE_NAME_SIZE,
             "akvcam-device-%llu", akvcam_id());

    self->v4l2_dev.ctrl_handler = akvcam_controls_handler(self->controls);
    self->vdev = video_device_alloc();

    if (!self->vdev) {
        akvcam_set_last_error(-ENOMEM);

        goto akvcam_device_new_failed;
    }

    self->vdev->queue = akvcam_queue_vb2_queue(self->queue);
    self->vdev->lock = akvcam_queue_mutex(self->queue);
    self->vdev->ioctl_ops = akvcam_ioctl_ops_get(type);

    if (!self->vdev->ioctl_ops)
        goto akvcam_device_new_failed;

    snprintf(self->vdev->name, 32, "%s", name);
    self->vdev->v4l2_dev = &self->v4l2_dev;
    self->vdev->vfl_type = VFL_TYPE_GRABBER;
    self->vdev->vfl_dir =
            type == AKVCAM_DEVICE_TYPE_OUTPUT? VFL_DIR_TX: VFL_DIR_RX;
    self->vdev->minor = -1;
    self->vdev->fops = akvcam_fops_get();
    self->vdev->tvnorms = V4L2_STD_ALL;
    self->vdev->release = video_device_release;
    video_set_drvdata(self->vdev, self);
    akvcam_set_last_error(0);

    return self;

akvcam_device_new_failed:
    if (self) {
        if (self->vdev) {
            if(self->vdev->ioctl_ops)
                kfree(self->vdev->ioctl_ops);

            kfree(self->vdev);
        }

        akvcam_format_delete(&self->format);
        akvcam_list_delete(&self->formats);
        akvcam_controls_delete(&self->controls);
        akvcam_queue_delete(&self->queue);
        akvcam_object_free(&AKVCAM_TO_OBJECT(self));
        kfree(self);
    }

    return NULL;
}

void akvcam_device_delete(akvcam_device_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_device_unregister(*self);

    if ((*self)->vdev) {
        if ((*self)->vdev->ioctl_ops)
            kfree((*self)->vdev->ioctl_ops);

        kfree((*self)->vdev);
    }

    akvcam_format_delete(&((*self)->format));
    akvcam_list_delete(&((*self)->formats));
    akvcam_controls_delete(&((*self)->controls));
    akvcam_queue_delete(&(*self)->queue);
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

bool akvcam_device_register(akvcam_device_t self)
{
    int result = v4l2_device_register(NULL, &self->v4l2_dev);

    if (!result)
        result = video_register_device(self->vdev, VFL_TYPE_GRABBER, -1);

    akvcam_set_last_error(result);

    return result? false: true;
}

void akvcam_device_unregister(akvcam_device_t self)
{
    if (self->vdev)
        video_unregister_device(self->vdev);

    v4l2_device_unregister(&self->v4l2_dev);
}

u16 akvcam_device_num(akvcam_device_t self)
{
    return self->vdev->num;
}

AKVCAM_DEVICE_TYPE akvcam_device_type(akvcam_device_t self)
{
    return self->type;
}

struct akvcam_controls *akvcam_device_controls_nr(akvcam_device_t self)
{
    return self->controls;
}

struct akvcam_controls *akvcam_device_controls(akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->controls));

    return self->controls;
}

struct akvcam_list *akvcam_device_formats_nr(akvcam_device_t self)
{
    return self->formats;
}

struct akvcam_list *akvcam_device_formats(akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->formats));

    return self->formats;
}

struct akvcam_format *akvcam_device_format_nr(akvcam_device_t self)
{
    return self->format;
}

struct akvcam_format *akvcam_device_format(akvcam_device_t self)
{
    akvcam_object_ref(AKVCAM_TO_OBJECT(self->format));

    return self->format;
}

struct video_device *akvcam_device_vdev(akvcam_device_t self)
{
    return self->vdev;
}

size_t akvcam_device_sizeof()
{
    return sizeof(struct akvcam_device);
}

akvcam_device_t akvcam_device_from_file_nr(struct file *filp)
{
    struct video_device *vdev;

    if (!filp)
        return NULL;

    vdev = video_devdata(filp);

    if (!vdev)
        return NULL;

    return video_get_drvdata(vdev);
}

akvcam_device_t akvcam_device_from_file(struct file *filp)
{
    akvcam_device_t device = akvcam_device_from_file_nr(filp);

    if (!device)
        return NULL;

    akvcam_object_ref(AKVCAM_TO_OBJECT(device));

    return device;
}

akvcam_device_t akvcam_device_from_v4l2_fh_nr(struct v4l2_fh *fh)
{
    if (!fh || !fh->vdev)
        return NULL;

    return video_get_drvdata(fh->vdev);
}

akvcam_device_t akvcam_device_from_v4l2_fh(struct v4l2_fh *fh)
{
    akvcam_device_t device = akvcam_device_from_v4l2_fh_nr(fh);

    if (!device)
        return NULL;

    akvcam_object_ref(AKVCAM_TO_OBJECT(device));

    return device;
}
