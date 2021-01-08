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

#ifndef AKVCAM_CONTROLS_H
#define AKVCAM_CONTROLS_H

#include <linux/videodev2.h>

#include "controls_types.h"
#include "device_types.h"
#include "utils.h"

struct v4l2_queryctrl;
struct v4l2_control;
struct v4l2_ext_controls;
struct v4l2_event_ctrl;
struct v4l2_event;
struct v4l2_query_ext_ctrl;

// public
akvcam_controls_t akvcam_controls_new(AKVCAM_DEVICE_TYPE device_type);
void akvcam_controls_delete(akvcam_controls_t self);
akvcam_controls_t akvcam_controls_ref(akvcam_controls_t self);

int akvcam_controls_query(akvcam_controls_ct self,
                          struct v4l2_queryctrl *control);
int akvcam_controls_fill_menu(akvcam_controls_ct self,
                              struct v4l2_querymenu *menu);
int akvcam_controls_get(akvcam_controls_ct self,
                        struct v4l2_control *control);
int akvcam_controls_set(akvcam_controls_t self,
                        const struct v4l2_control *control);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
int akvcam_controls_query_ext(akvcam_controls_ct self,
                              struct v4l2_query_ext_ctrl *control);
int akvcam_controls_try_ext(akvcam_controls_ct self,
                            struct v4l2_ext_controls *controls);
int akvcam_controls_get_ext(akvcam_controls_ct self,
                            struct v4l2_ext_controls *controls);
int akvcam_controls_set_ext(akvcam_controls_t self,
                            struct v4l2_ext_controls *controls);
#endif
bool akvcam_controls_contains(akvcam_controls_ct self, __u32 id);
bool akvcam_controls_generate_event(akvcam_controls_ct self,
                                    __u32 id,
                                    struct v4l2_event *event);

// signals
akvcam_signal(controls, updated, const struct v4l2_event *event);

#endif // AKVCAM_CONTROLS_H
