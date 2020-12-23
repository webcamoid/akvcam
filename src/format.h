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

#include "format_types.h"

struct v4l2_fract;
struct v4l2_format;

// public
akvcam_format_t akvcam_format_new(__u32 fourcc,
                                  size_t width,
                                  size_t height,
                                  const struct v4l2_fract *frame_rate);
akvcam_format_t akvcam_format_new_copy(akvcam_format_ct other);
void akvcam_format_delete(akvcam_format_t self);
akvcam_format_t akvcam_format_ref(akvcam_format_t self);

void akvcam_format_copy(akvcam_format_t self, akvcam_format_ct other);
__u32 akvcam_format_fourcc(akvcam_format_ct self);
void akvcam_format_set_fourcc(akvcam_format_t self, __u32 fourcc);
const char *akvcam_format_fourcc_str(akvcam_format_ct self);
void akvcam_format_set_fourcc_str(akvcam_format_t self, const char *fourcc);
size_t akvcam_format_width(akvcam_format_ct self);
void akvcam_format_set_width(akvcam_format_t self, size_t width);
size_t akvcam_format_height(akvcam_format_ct self);
void akvcam_format_set_height(akvcam_format_t self, size_t height);
struct v4l2_fract akvcam_format_frame_rate(akvcam_format_ct self);
void akvcam_format_set_frame_rate(akvcam_format_t self, const struct v4l2_fract frame_rate);
size_t akvcam_format_bpp(akvcam_format_ct self);
size_t akvcam_format_bypl(akvcam_format_ct self, size_t plane);
size_t akvcam_format_size(akvcam_format_ct self);
size_t akvcam_format_planes(akvcam_format_ct self);
size_t akvcam_format_offset(akvcam_format_ct self, size_t plane);
size_t akvcam_format_plane_size(akvcam_format_ct self, size_t plane);
bool akvcam_format_is_valid(akvcam_format_ct self);
void akvcam_format_clear(akvcam_format_t self);
const char *akvcam_format_to_string(akvcam_format_t self);

// public static
void akvcam_format_round_nearest(int width, int height,
                                 int *owidth, int *oheight,
                                 int align);
__u32 akvcam_format_fourcc_from_string(const char *fourcc_str);
const char *akvcam_format_string_from_fourcc(__u32 fourcc);
akvcam_format_t akvcam_format_nearest(akvcam_formats_list_ct formats,
                                      akvcam_format_ct format);
akvcam_pixel_formats_list_t akvcam_format_pixel_formats(akvcam_formats_list_ct formats);
akvcam_resolutions_list_t akvcam_format_resolutions(akvcam_formats_list_t formats,
                                                    __u32 fourcc);
akvcam_fps_list_t akvcam_format_frame_rates(akvcam_formats_list_ct formats,
                                            __u32 fourcc,
                                            size_t width,
                                            size_t height);
akvcam_format_ct akvcam_format_from_v4l2_nr(akvcam_formats_list_ct formats,
                                            const struct v4l2_format *format);
akvcam_format_t akvcam_format_from_v4l2(akvcam_formats_list_ct formats,
                                        const struct v4l2_format *format);
bool akvcam_format_have_multiplanar(akvcam_formats_list_ct formats);

#endif // AKVCAM_FORMAT_H
