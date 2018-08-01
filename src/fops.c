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

#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-vmalloc.h>

#include "fops.h"

struct v4l2_file_operations *akvcam_fops_get(void)
{
    static struct v4l2_file_operations fops = {
        .owner          = THIS_MODULE    ,
        .open           = v4l2_fh_open   ,
        .release        = vb2_fop_release,
        .read           = vb2_fop_read   ,
        .write          = vb2_fop_write  ,
        .poll           = vb2_fop_poll   ,
        .unlocked_ioctl = video_ioctl2   ,
        .mmap           = vb2_fop_mmap   ,
    };

    return &fops;
}
