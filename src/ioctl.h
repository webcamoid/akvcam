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

#ifndef AKVCAM_IOCTL_H
#define AKVCAM_IOCTL_H

#include <linux/types.h>

#include "node_types.h"

struct akvcam_ioctl;
typedef struct akvcam_ioctl *akvcam_ioctl_t;

akvcam_ioctl_t akvcam_ioctl_new(void);
void akvcam_ioctl_delete(akvcam_ioctl_t *self);

int akvcam_ioctl_do(akvcam_ioctl_t self,
                    akvcam_node_t node,
                    unsigned int cmd,
                    void __user *arg);

#endif // AKVCAM_IOCTL_H
