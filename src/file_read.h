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

#ifndef AKVCAM_FILE_H
#define AKVCAM_FILE_H

#include <linux/types.h>

struct akvcam_file;
typedef struct akvcam_file *akvcam_file_t;
typedef const struct akvcam_file *akvcam_file_ct;

typedef enum
{
    AKVCAM_FILE_SEEK_BEG,
    AKVCAM_FILE_SEEK_CUR,
    AKVCAM_FILE_SEEK_END
} AKVCAM_FILE_SEEK;

// public
akvcam_file_t akvcam_file_new(const char *file_name);
void akvcam_file_delete(akvcam_file_t self);
akvcam_file_t akvcam_file_ref(akvcam_file_t self);

const char *akvcam_file_file_name(akvcam_file_ct self);
void akvcam_file_set_file_name(akvcam_file_t self, const char *file_name);
bool akvcam_file_open(akvcam_file_t self);
void akvcam_file_close(akvcam_file_t self);
bool akvcam_file_is_open(akvcam_file_ct self);
bool akvcam_file_eof(akvcam_file_ct self);
bool akvcam_file_seek(akvcam_file_t self, ssize_t offset, AKVCAM_FILE_SEEK pos);
size_t akvcam_file_read(akvcam_file_t self, void *data, size_t size);
char *akvcam_file_read_line(akvcam_file_t self);

#endif // AKVCAM_FILE_H
