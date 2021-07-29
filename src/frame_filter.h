/* akvcam, virtual camera for Linux.
 * Copyright (C) 2021  Gonzalo Exequiel Pedone
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

#ifndef AKVCAM_FRAME_FILTER_H
#define AKVCAM_FRAME_FILTER_H

#include <linux/types.h>

#include "frame_filter_types.h"
#include "frame_types.h"

// public
akvcam_frame_filter_t akvcam_frame_filter_new(void);
void akvcam_frame_filter_delete(akvcam_frame_filter_t self);
akvcam_frame_filter_t akvcam_frame_filter_ref(akvcam_frame_filter_t self);

void akvcam_frame_filter_swap_rgb(akvcam_frame_filter_ct self,
                                  akvcam_frame_t frame);
void akvcam_frame_filter_hsl(akvcam_frame_filter_ct self,
                             akvcam_frame_t frame,
                             int hue,
                             int saturation,
                             int luminance);
void akvcam_frame_filter_contrast(akvcam_frame_filter_ct self,
                                  akvcam_frame_t frame,
                                  int contrast);
void akvcam_frame_filter_gamma(akvcam_frame_filter_ct self,
                               akvcam_frame_t frame,
                               int gamma);
void akvcam_frame_filter_gray(akvcam_frame_filter_ct self,
                              akvcam_frame_t frame);
void akvcam_frame_filter_apply(akvcam_frame_filter_ct self,
                               akvcam_frame_t frame,
                               int hue,
                               int saturation,
                               int luminance,
                               int contrast,
                               int gamma,
                               bool gray,
                               bool swap_rgb);

#endif // AKVCAM_FRAME_FILTER_H
