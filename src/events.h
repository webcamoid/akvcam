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

#ifndef AKVCAM_EVENTS_H
#define AKVCAM_EVENTS_H

#include <linux/types.h>

#include "utils.h"

struct akvcam_events;
typedef struct akvcam_events *akvcam_events_t;
struct v4l2_event_subscription;
struct v4l2_event;
struct file;
struct poll_table_struct;

akvcam_events_t akvcam_events_new(void);
void akvcam_events_delete(akvcam_events_t *self);

void akvcam_events_subscribe(akvcam_events_t self,
                             struct v4l2_event_subscription *subscription);
void akvcam_events_unsubscribe(akvcam_events_t self,
                               struct v4l2_event_subscription *subscription);
void akvcam_events_unsubscribe_all(akvcam_events_t self);
__poll_t akvcam_events_poll(akvcam_events_t self,
                            struct file *filp,
                            struct poll_table_struct *wait);
bool akvcam_events_enqueue(akvcam_events_t self,
                           const struct v4l2_event *event);
bool akvcam_events_dequeue(akvcam_events_t self, struct v4l2_event *event);
bool akvcam_events_available(akvcam_events_t self);

#endif // AKVCAM_EVENTS_H
