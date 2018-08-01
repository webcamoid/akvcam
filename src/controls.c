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

#include <linux/slab.h>
#include <media/v4l2-ctrls.h>

#include "controls.h"
#include "object.h"
#include "utils.h"

#define AKVCAM_NUM_CONTROLS 7

struct akvcam_controls
{
    akvcam_object_t self;
    struct v4l2_ctrl_handler ctrl_handler;
};

akvcam_controls_t akvcam_controls_new(void)
{
    int result;
    akvcam_controls_t self =
            kzalloc(sizeof(struct akvcam_controls), GFP_KERNEL);

    if (!self) {
        akvcam_set_last_error(-ENOMEM);

        goto akvcam_controls_new_failed;
    }

    self->self =
            akvcam_object_new(self, (akvcam_deleter_t) akvcam_controls_delete);

    if (!self->self)
        goto akvcam_controls_new_failed;

    result = v4l2_ctrl_handler_init(&self->ctrl_handler, AKVCAM_NUM_CONTROLS);

    if (result) {
        akvcam_set_last_error(result);

        goto akvcam_controls_new_failed;
    }

    result = v4l2_ctrl_handler_setup(&self->ctrl_handler);

    if (result) {
        akvcam_set_last_error(result);

        goto akvcam_controls_new_failed;
    }

    v4l2_ctrl_new_std(&self->ctrl_handler, NULL, V4L2_CID_BRIGHTNESS, -255, 255, 1, 0);
    v4l2_ctrl_new_std(&self->ctrl_handler, NULL, V4L2_CID_CONTRAST  , -255, 255, 1, 0);
    v4l2_ctrl_new_std(&self->ctrl_handler, NULL, V4L2_CID_SATURATION, -255, 255, 1, 0);
    v4l2_ctrl_new_std(&self->ctrl_handler, NULL, V4L2_CID_HUE       , -359, 359, 1, 0);
    v4l2_ctrl_new_std(&self->ctrl_handler, NULL, V4L2_CID_GAIN      , -255, 255, 1, 0);
    v4l2_ctrl_new_std(&self->ctrl_handler, NULL, V4L2_CID_HFLIP     ,    0,   1, 1, 0);
    v4l2_ctrl_new_std(&self->ctrl_handler, NULL, V4L2_CID_VFLIP     ,    0,   1, 1, 0);

    akvcam_set_last_error(0);

    return self;

akvcam_controls_new_failed:
    if (self) {
        v4l2_ctrl_handler_free(&self->ctrl_handler);
        akvcam_object_free(&AKVCAM_TO_OBJECT(self));
        kfree(self);
    }

    return NULL;
}

void akvcam_controls_delete(akvcam_controls_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    v4l2_ctrl_handler_free(&((*self)->ctrl_handler));
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

size_t akvcam_controls_count(akvcam_controls_t self)
{
    UNUSED(self);

    return AKVCAM_NUM_CONTROLS;
}

struct v4l2_ctrl_handler *akvcam_controls_handler(akvcam_controls_t self)
{
    return &self->ctrl_handler;
}
