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

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "file_read.h"
#include "object.h"
#include "rbuffer.h"
#include "utils.h"

#define AKVCAM_READ_BLOCK 512

struct akvcam_file
{
    akvcam_object_t self;
    char *file_name;
    struct file *filp;
    akvcam_rbuffer_tt(char *) buffer;
    size_t size;
    size_t bytes_read;
    size_t file_bytes_read;
    bool is_open;
};

akvcam_file_t akvcam_file_new(const char *file_name)
{
    akvcam_file_t self = kzalloc(sizeof(struct akvcam_file), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_file_delete);
    self->file_name = akvcam_strdup(file_name, AKVCAM_MEMORY_TYPE_KMALLOC);
    self->buffer = akvcam_rbuffer_new();

    return self;
}

void akvcam_file_delete(akvcam_file_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    akvcam_file_close(*self);

    if ((*self)->file_name)
        kfree((*self)->file_name);

    akvcam_rbuffer_delete(&((*self)->buffer));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

const char *akvcam_file_file_name(akvcam_file_t self)
{
    return self->file_name;
}

void akvcam_file_set_file_name(akvcam_file_t self, const char *file_name)
{
    if (self->file_name)
        kfree(self->file_name);

    self->file_name = akvcam_strdup(file_name, AKVCAM_MEMORY_TYPE_KMALLOC);
}

bool akvcam_file_open(akvcam_file_t self)
{
    akvcam_file_close(self);

    if (!akvcam_file_exists(self))
        return false;

    self->filp = filp_open(self->file_name, O_RDONLY, 0);

    if (IS_ERR(self->filp))
        return false;

    self->size = akvcam_file_size(self);
    akvcam_rbuffer_resize(self->buffer,
                          self->size,
                          sizeof(char),
                          AKVCAM_MEMORY_TYPE_VMALLOC);
    akvcam_rbuffer_clear(self->buffer);
    self->bytes_read = 0;
    self->file_bytes_read = 0;
    self->is_open = true;

    return true;
}

void akvcam_file_close(akvcam_file_t self)
{
    if (!self->is_open)
        return;

    filp_close(self->filp, NULL);
    self->filp = NULL;
    akvcam_rbuffer_clear(self->buffer);
    akvcam_rbuffer_resize(self->buffer,
                          0,
                          sizeof(char),
                          AKVCAM_MEMORY_TYPE_VMALLOC);
    self->size = 0;
    self->bytes_read = 0;
    self->file_bytes_read = 0;
    self->is_open = false;
}

bool akvcam_file_is_open(akvcam_file_t self)
{
    return self->is_open;
}

bool akvcam_file_exists(akvcam_file_t self)
{
    struct kstat stats;
    mm_segment_t oldfs;
    int result;

    if (strlen(self->file_name) < 1)
        return 0;

    oldfs = get_fs();
    set_fs(KERNEL_DS);
    result = vfs_stat((const char __user *) self->file_name, &stats);
    set_fs(oldfs);

    return result == 0;
}

size_t akvcam_file_size(akvcam_file_t self)
{
    struct kstat stats;
    mm_segment_t oldfs;
    int result;

    if (strlen(self->file_name) < 1)
        return 0;

    oldfs = get_fs();
    set_fs(KERNEL_DS);
    result = vfs_stat((const char __user *) self->file_name, &stats);
    set_fs(oldfs);

    return result? 0: (size_t) stats.size;
}

bool akvcam_file_eof(akvcam_file_t self)
{
    return self->bytes_read >= self->size;
}

bool akvcam_file_seek(akvcam_file_t self, ssize_t offset, AKVCAM_FILE_SEEK pos)
{
    if (!self->is_open)
        return false;

    akvcam_rbuffer_clear(self->buffer);

    switch (pos) {
    case AKVCAM_FILE_SEEK_BEG:
        self->bytes_read =
        self->file_bytes_read =
                (size_t) akvcam_bound(0, offset, (ssize_t) self->size);

        break;

    case AKVCAM_FILE_SEEK_CUR:
        self->bytes_read =
        self->file_bytes_read =
                (size_t) akvcam_bound(0,
                                      (ssize_t) self->bytes_read + offset,
                                      (ssize_t) self->size);

        break;

    case AKVCAM_FILE_SEEK_END:
        self->bytes_read =
        self->file_bytes_read =
                (size_t) akvcam_bound(0,
                                      (ssize_t) self->size + offset,
                                      (ssize_t) self->size);

        break;
    }

    vfs_setpos(self->filp, (loff_t) self->bytes_read, (loff_t) self->size);

    return true;
}

size_t akvcam_file_read(akvcam_file_t self, void *data, size_t size)
{
    loff_t offset;
    ssize_t bytes_read;
    char read_block[AKVCAM_READ_BLOCK];

    if (!self->is_open || size < 1)
        return 0;

    while (self->file_bytes_read < self->size
           && akvcam_rbuffer_data_size(self->buffer) < size) {
        offset = (loff_t) self->file_bytes_read;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
        bytes_read = kernel_read(self->filp,
                                 offset,
                                 read_block,
                                 AKVCAM_READ_BLOCK);
#else
        bytes_read = kernel_read(self->filp,
                                 read_block,
                                 AKVCAM_READ_BLOCK,
                                 &offset);
#endif

        if (bytes_read < 1)
            break;

        akvcam_rbuffer_queue_bytes(self->buffer, read_block, (size_t) bytes_read);
        self->file_bytes_read += (size_t) bytes_read;
    }

    size = akvcam_min(akvcam_rbuffer_size(self->buffer), size);

    if (size < 1)
        return 0;

    akvcam_rbuffer_dequeue_bytes(self->buffer, data, &size, false);
    self->bytes_read += size;

    return size;
}

static bool akvcam_file_find_new_line(const char *element,
                                      const char *new_line,
                                      size_t size)
{
    UNUSED(size);

    return strncmp(element, new_line, 1) == 0;
}

char *akvcam_file_read_line(akvcam_file_t self)
{
    loff_t offset;
    ssize_t bytes_read;
    ssize_t new_line = -1;
    char read_block[AKVCAM_READ_BLOCK];
    char *line;

    if (!self->is_open)
        return vzalloc(1);

    for (;;) {
        new_line = 0;
        akvcam_rbuffer_find(self->buffer,
                            "\n",
                            1,
                            (akvcam_are_equals_t) akvcam_file_find_new_line,
                            &new_line);

        if (new_line >= 0)
            break;

        if (self->file_bytes_read >= self->size)
            break;

        bytes_read = (ssize_t) akvcam_min(AKVCAM_READ_BLOCK,
                                          self->size - self->file_bytes_read);

        if (bytes_read < 1)
            break;

        offset = (loff_t) self->file_bytes_read;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
        bytes_read = kernel_read(self->filp,
                                 offset,
                                 read_block,
                                 (size_t) bytes_read);
#else
        bytes_read = kernel_read(self->filp,
                                 read_block,
                                 (size_t) bytes_read,
                                 &offset);
#endif

        akvcam_rbuffer_queue_bytes(self->buffer, read_block, (size_t) bytes_read);
        self->file_bytes_read += (size_t) bytes_read;
    }

    if (new_line >= 0) {
        line = vzalloc((size_t) new_line + 2);
        new_line++;
        akvcam_rbuffer_dequeue_bytes(self->buffer,
                                     line,
                                     (size_t *) &new_line,
                                     false);
        line[(size_t) new_line - 1] = 0;
        self->bytes_read += (size_t) new_line;
    } else if (self->bytes_read < self->size) {
        new_line = (ssize_t) akvcam_rbuffer_data_size(self->buffer);
        line = vzalloc((size_t) new_line + 1);
        akvcam_rbuffer_dequeue_bytes(self->buffer,
                                     line,
                                     (size_t *) &new_line,
                                     false);
        self->bytes_read += (size_t) new_line;
    } else {
        line = vzalloc(1);
    }

    return line;
}
