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

#ifndef AKVCAM_MUTEX_H
#define AKVCAM_MUTEX_H

typedef enum
{
    AKVCAM_MUTEX_MODE_PERFORMANCE,
    AKVCAM_MUTEX_MODE_CONSERVATIVE,
} AKVCAM_MUTEX_MODE;

struct akvcam_mutex;
typedef struct akvcam_mutex *akvcam_mutex_t;

// public
akvcam_mutex_t akvcam_mutex_new(AKVCAM_MUTEX_MODE mode);
void akvcam_mutex_delete(akvcam_mutex_t *self);

void akvcam_mutex_lock(akvcam_mutex_t self);
void akvcam_mutex_unlock(akvcam_mutex_t self);

#endif // AKVCAM_MUTEX_H
