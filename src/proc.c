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

#include <linux/seq_file.h>

#include "proc.h"
#include "format.h"
#include "format_specs.h"
#include "list.h"

static const struct proc_ops akvcam_proc_fops;

const char *akvcam_proc_file_name(void)
{
    return "akvcaminfo";
}

const struct proc_ops *akvcam_proc_info(void)
{
    return &akvcam_proc_fops;
}

static void akvcam_proc_print_formats(struct seq_file *f, const char *label)
{
    size_t n = akvcam_supported_pixel_formats();
    seq_printf(f, "%s = ", label);

    for (size_t i = 0; i < n; ++i) {
        __u32 fourcc = akvcam_pixel_format_by_index(i);

        if (fourcc != 0)
            seq_printf(f, "%s%s", i? ", ": "", akvcam_string_from_fourcc(fourcc));
    }

    seq_printf(f, "\n");
}

static int akvcam_proc_show_info(struct seq_file *f, void *user_data)
{
    (void) user_data;

    akvcam_proc_print_formats(f, "input_formats");
    akvcam_proc_print_formats(f, "output_formats");
    seq_printf(f, "default_input_format = %s\n", akvcam_string_from_fourcc(akvcam_default_input_pixel_format()));
    seq_printf(f, "default_output_format = %s\n", akvcam_string_from_fourcc(akvcam_default_output_pixel_format()));

    return 0;
}

static int akvcam_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, akvcam_proc_show_info, NULL);
}

static const struct proc_ops akvcam_proc_fops = {
    .proc_open    = akvcam_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};
