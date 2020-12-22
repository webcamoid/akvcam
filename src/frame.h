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

#ifndef AKVCAM_FRAME_H
#define AKVCAM_FRAME_H

#include <linux/types.h>

#include "frame_types.h"
#include "format_types.h"

// public
akvcam_frame_t akvcam_frame_new(akvcam_format_t format,
                                const void *data,
                                size_t size);
akvcam_frame_t akvcam_frame_new_copy(akvcam_frame_t other);
void akvcam_frame_delete(akvcam_frame_t self);
akvcam_frame_t akvcam_frame_ref(akvcam_frame_t self);

void akvcam_frame_copy(akvcam_frame_t self, const akvcam_frame_t other);
akvcam_format_t akvcam_frame_format(const akvcam_frame_t self);
void *akvcam_frame_data(const akvcam_frame_t self);
void *akvcam_frame_line(const akvcam_frame_t self, size_t plane, size_t y);
const void *akvcam_frame_const_line(const akvcam_frame_t self,
                                    size_t plane,
                                    size_t y);
size_t akvcam_frame_size(const akvcam_frame_t self);
void akvcam_frame_resize(akvcam_frame_t self, size_t size);
void akvcam_frame_clear(akvcam_frame_t self);
bool akvcam_frame_load(akvcam_frame_t self, const char *file_name);
void akvcam_frame_mirror(akvcam_frame_t self,
                         bool horizontalMirror,
                         bool verticalMirror);
bool akvcam_frame_scaled(akvcam_frame_t self,
                         size_t width,
                         size_t height,
                         AKVCAM_SCALING mode,
                         AKVCAM_ASPECT_RATIO aspectRatio);
void akvcam_frame_swap_rgb(akvcam_frame_t self);
bool akvcam_frame_convert(akvcam_frame_t self, __u32 fourcc);
void akvcam_frame_adjust_hsl(akvcam_frame_t self,
                             int hue,
                             int saturation,
                             int luminance);
void akvcam_frame_adjust_contrast(akvcam_frame_t self, int contrast);
void akvcam_frame_adjust_gamma(akvcam_frame_t self, int gamma);
void akvcam_frame_to_gray_scale(akvcam_frame_t self);
void akvcam_frame_adjust(akvcam_frame_t self,
                         int hue,
                         int saturation,
                         int luminance,
                         int contrast,
                         int gamma,
                         bool gray);

// public static
const char *akvcam_frame_scaling_to_string(AKVCAM_SCALING scaling);
const char *akvcam_frame_aspect_ratio_to_string(AKVCAM_ASPECT_RATIO aspect_ratio);
bool akvcam_frame_can_convert(__u32 in_fourcc, __u32 out_fourcc);

#endif // AKVCAM_FRAME_H
