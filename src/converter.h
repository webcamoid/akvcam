/* akvcam, virtual camera for Linux.
 * Copyright (C) 2026  Gonzalo Exequiel Pedone
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

#ifndef AKVCAM_CONVERTER_H
#define AKVCAM_CONVERTER_H

#include <linux/types.h>

#include "converter_types.h"
#include "color_convert_types.h"
#include "frame_types.h"
#include "format_types.h"

// public
akvcam_converter_t akvcam_converter_new(void);
akvcam_converter_t akvcam_converter_new_copy(akvcam_converter_ct other);
void akvcam_converter_delete(akvcam_converter_t self);
akvcam_converter_t akvcam_converter_ref(akvcam_converter_t self);

void akvcam_converter_copy(akvcam_converter_t self, akvcam_converter_ct other);
akvcam_format_t akvcam_converter_output_format(akvcam_converter_ct self);
void akvcam_converter_set_output_format(akvcam_converter_t self,
                                        akvcam_format_ct format);
AKVCAM_YUV_COLOR_SPACE akvcam_converter_yuv_color_space(akvcam_converter_ct self);
void akvcam_converter_set_yuv_color_space(akvcam_converter_t self,
                                          AKVCAM_YUV_COLOR_SPACE yuv_color_space);
AKVCAM_YUV_COLOR_SPACE_TYPE akvcam_converter_yuv_color_space_type(akvcam_converter_ct self);
void akvcam_converter_set_yuv_color_space_type(akvcam_converter_t self,
                                               AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type);
AKVCAM_SCALING_MODE akvcam_converter_scaling_mode(akvcam_converter_ct self);
void akvcam_converter_set_scaling_mode(akvcam_converter_t self,
                                       AKVCAM_SCALING_MODE scaling_mode);
AKVCAM_ASPECT_RATIO_MODE akvcam_converter_aspect_ratio_mode(akvcam_converter_ct self);
void akvcam_converter_set_aspect_ratio_mode(akvcam_converter_t self,
                                            AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode);
void akvcam_converter_set_cache_index(akvcam_converter_t self,
                                      int index);
bool akvcam_converter_begin(akvcam_converter_t self);
void akvcam_converter_end(akvcam_converter_t self);
akvcam_frame_t akvcam_converter_convert(akvcam_converter_t self,
                                        akvcam_frame_ct frame);
void akvcam_converter_reset(akvcam_converter_t self);

// public static
const char *akvcam_converter_scaling_mode_to_string(AKVCAM_SCALING_MODE scaling_mode);
const char *akvcam_converter_aspect_ratio_mode_to_string(AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode);

#endif // AKVCAM_CONVERTER_H
