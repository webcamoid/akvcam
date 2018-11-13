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

#ifdef VIDIOC_QUERY_EXT_CTRL
struct v4l2_query_ext_ctrl;
#endif

// public
akvcam_controls_t akvcam_controls_new(akvcam_device_t device);
void akvcam_controls_delete(akvcam_controls_t *self);

int akvcam_controls_fill(const akvcam_controls_t self,
                         struct v4l2_queryctrl *control);
int akvcam_controls_fill_menu(const akvcam_controls_t self,
                              struct v4l2_querymenu *menu);
#ifdef VIDIOC_QUERY_EXT_CTRL
int akvcam_controls_fill_ext(const akvcam_controls_t self,
                             struct v4l2_query_ext_ctrl *control);
#endif
int akvcam_controls_get(const akvcam_controls_t self,
                        struct v4l2_control *control);
int akvcam_controls_get_ext(const akvcam_controls_t self,
                            struct v4l2_ext_controls *controls,
                            uint32_t flags);
int akvcam_controls_set(akvcam_controls_t self,
                        const struct v4l2_control *control);
int akvcam_controls_set_ext(akvcam_controls_t self,
                            struct v4l2_ext_controls *controls,
                            uint32_t flags);
int akvcam_controls_try_ext(akvcam_controls_t self,
                            struct v4l2_ext_controls *controls,
                            uint32_t flags);
bool akvcam_controls_contains(const akvcam_controls_t self, __u32 id);
bool akvcam_controls_generate_event(const akvcam_controls_t self,
                                    __u32 id,
                                    struct v4l2_event *event);

// signals
akvcam_callback(controls_changed, const struct v4l2_event *event)
void akvcam_controls_set_changed_callback(akvcam_controls_t self,
                                          const akvcam_controls_changed_callback callback);

#endif // AKVCAM_CONTROLS_H
