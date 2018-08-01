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

#include "utils.h"

static struct
{
    uint64_t id;
    int last_error;
} akvcam_utils_private;

uint64_t akvcam_id()
{
    return akvcam_utils_private.id++;
}

int akvcam_get_last_error(void)
{
    return akvcam_utils_private.last_error;
}

int akvcam_set_last_error(int error)
{
    akvcam_utils_private.last_error = error;

    return error;
}
