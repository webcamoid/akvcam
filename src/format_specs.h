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

#ifndef AKVCAM_FORMAT_SPECS_H
#define AKVCAM_FORMAT_SPECS_H

#include <linux/types.h>

#include "format_specs_types.h"

akvcam_format_specs_ct akvcam_format_specs_from_fixel_format(__u32 pixel_format);
akvcam_color_component_ct akvcam_format_specs_component(akvcam_format_specs_ct self,
                                                        AKVCAM_COMPONENT_TYPE component_type);
int akvcam_format_specs_component_plane(akvcam_format_specs_ct self,
                                        AKVCAM_COMPONENT_TYPE component_type);
bool akvcam_format_specs_contains(akvcam_format_specs_ct self,
                                  AKVCAM_COMPONENT_TYPE component_type);
size_t akvcam_format_specs_byte_depth(akvcam_format_specs_ct self);
size_t akvcam_format_specs_depth(akvcam_format_specs_ct self);
size_t akvcam_format_specs_number_of_components(akvcam_format_specs_ct self);
size_t akvcam_format_specs_main_components(akvcam_format_specs_ct self);
bool akvcam_format_specs_is_fast(akvcam_format_specs_ct self);
size_t akvcam_plane_pixel_size(akvcam_plane_ct plane);
size_t akvcam_plane_width_div(akvcam_plane_ct plane);
size_t akvcam_plane_height_div(akvcam_plane_ct plane);
uint64_t akvcam_color_component_max(akvcam_color_component_ct color_component);

// public static
__u32 akvcam_fourcc_from_string(const char *fourcc_str);
const char *akvcam_string_from_fourcc(__u32 fourcc);
__u32 akvcam_default_input_pixel_format(void);
__u32 akvcam_default_output_pixel_format(void);
size_t akvcam_supported_pixel_formats(void);
__u32 akvcam_pixel_format_by_index(size_t index);

#endif // AKVCAM_FORMAT_SPECS_H
