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

#ifndef AKVCAM_DEVICE_H
#define AKVCAM_DEVICE_H

#include <linux/types.h>

typedef enum
{
    AKVCAM_DEVICE_TYPE_CAPTURE,
    AKVCAM_DEVICE_TYPE_OUTPUT,
} AKVCAM_DEVICE_TYPE;

struct akvcam_controls;
struct akvcam_device;
struct akvcam_format;
struct akvcam_list;
struct file;
struct v4l2_fh;
struct video_device;
typedef struct akvcam_device *akvcam_device_t;

// public
akvcam_device_t akvcam_device_new(const char *name,
                                  AKVCAM_DEVICE_TYPE type,
                                  struct akvcam_list *formats);
void akvcam_device_delete(akvcam_device_t *self);
bool akvcam_device_register(akvcam_device_t self);
void akvcam_device_unregister(akvcam_device_t self);
u16 akvcam_device_num(akvcam_device_t self);
AKVCAM_DEVICE_TYPE akvcam_device_type(akvcam_device_t self);
struct akvcam_controls *akvcam_device_controls_nr(akvcam_device_t self);
struct akvcam_controls *akvcam_device_controls(akvcam_device_t self);
struct akvcam_list *akvcam_device_formats_nr(akvcam_device_t self);
struct akvcam_list *akvcam_device_formats(akvcam_device_t self);
struct akvcam_format *akvcam_device_format_nr(akvcam_device_t self);
struct akvcam_format *akvcam_device_format(akvcam_device_t self);
struct video_device *akvcam_device_vdev(akvcam_device_t self);

// public static
size_t akvcam_device_sizeof(void);
akvcam_device_t akvcam_device_from_file_nr(struct file *filp);
akvcam_device_t akvcam_device_from_file(struct file *filp);
akvcam_device_t akvcam_device_from_v4l2_fh_nr(struct v4l2_fh *fh);
akvcam_device_t akvcam_device_from_v4l2_fh(struct v4l2_fh *fh);

#endif //AKVCAM_ DEVICE_H
