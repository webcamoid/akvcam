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

#ifndef AKVCAM_FORMAT_H
#define AKVCAM_FORMAT_H

#include <linux/types.h>

struct akvcam_format;
typedef struct akvcam_format *akvcam_format_t;
struct akvcam_list;
struct v4l2_fract;
struct v4l2_format;

// public
akvcam_format_t akvcam_format_new(__u32 fourcc,
                                  __u32 width,
                                  __u32 height,
                                  const struct v4l2_fract *frame_rate);
void akvcam_format_delete(akvcam_format_t *self);

void akvcam_format_copy(akvcam_format_t self, const akvcam_format_t other);
__u32 akvcam_format_fourcc(const akvcam_format_t self);
void akvcam_format_set_fourcc(akvcam_format_t self, __u32 fourcc);
__u32 akvcam_format_width(const akvcam_format_t self);
void akvcam_format_set_width(akvcam_format_t self, __u32 width);
__u32 akvcam_format_height(const akvcam_format_t self);
void akvcam_format_set_height(akvcam_format_t self, __u32 height);
struct v4l2_fract *akvcam_format_frame_rate(const akvcam_format_t self);
size_t akvcam_format_bpp(const akvcam_format_t self);
size_t akvcam_format_bypl(const akvcam_format_t self);
size_t akvcam_format_size(const akvcam_format_t self);
bool akvcam_format_is_valid(const akvcam_format_t self);
void akvcam_format_clear(akvcam_format_t self);

// public static
size_t akvcam_format_sizeof(void);
void akvcam_format_round_nearest(int width, int height,
                                 int *owidth, int *oheight,
                                 int align);
__u32 akvcam_format_fourcc_from_string(const char *fourcc_str);
const char *akvcam_format_string_from_fourcc(__u32 fourcc);
akvcam_format_t akvcam_format_nearest_nr(struct akvcam_list *formats,
                                         const akvcam_format_t format);
akvcam_format_t akvcam_format_nearest(struct akvcam_list *formats,
                                      const akvcam_format_t format);
struct akvcam_list *akvcam_format_pixel_formats(struct akvcam_list *formats);
struct akvcam_list *akvcam_format_resolutions(struct akvcam_list *formats,
                                              __u32 fourcc);
struct akvcam_list *akvcam_format_frame_rates(struct akvcam_list *formats,
                                              __u32 fourcc,
                                              __u32 width,
                                              __u32 height);
akvcam_format_t akvcam_format_from_v4l2_nr(struct akvcam_list *formats,
                                           const struct v4l2_format *format);
akvcam_format_t akvcam_format_from_v4l2(struct akvcam_list *formats,
                                        const struct v4l2_format *format);

#endif // AKVCAM_FORMAT_H
