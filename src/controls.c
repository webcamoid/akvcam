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
#include <linux/uaccess.h>
#include <linux/videodev2.h>

#include "controls.h"
#include "object.h"

typedef struct
{
    __u32 id;
    __u32 type;
    char  name[32];
    __s32 minimum;
    __s32 maximum;
    __s32 step;
    __s32 default_value;
    __u32 flags;
} akvcam_control_params, *akvcam_control_params_t;

typedef struct
{
    __u32 id;
    __s32 value;
} akvcam_control_value, *akvcam_control_value_t;

struct akvcam_controls
{
    akvcam_object_t self;
    akvcam_control_value_t values;
    akvcam_controls_changed_callback controls_changed;
    size_t n_controls;
};

static akvcam_control_params akvcam_controls_private[] = {
    {V4L2_CID_USER_CLASS, V4L2_CTRL_TYPE_CTRL_CLASS,   "User Controls",    0,   0, 0, 0, V4L2_CTRL_FLAG_READ_ONLY
                                                                                       | V4L2_CTRL_FLAG_WRITE_ONLY},
    {V4L2_CID_BRIGHTNESS,    V4L2_CTRL_TYPE_INTEGER,      "Brightness", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER},
    {V4L2_CID_CONTRAST  ,    V4L2_CTRL_TYPE_INTEGER,        "Contrast", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER},
    {V4L2_CID_SATURATION,    V4L2_CTRL_TYPE_INTEGER,      "Saturation", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER},
    {V4L2_CID_HUE       ,    V4L2_CTRL_TYPE_INTEGER,             "Hue", -359, 359, 1, 0,     V4L2_CTRL_FLAG_SLIDER},
    {V4L2_CID_GAMMA     ,    V4L2_CTRL_TYPE_INTEGER,           "Gamma", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER},
    {V4L2_CID_HFLIP     ,    V4L2_CTRL_TYPE_BOOLEAN, "Horizontal Flip",    0,   1, 1, 0,                         0},
    {V4L2_CID_VFLIP     ,    V4L2_CTRL_TYPE_BOOLEAN,   "Vertical Flip",    0,   1, 1, 0,                         0},
    {0                  ,                         0,                "",    0,   0, 0, 0,                         0},
};

akvcam_control_value_t akvcam_controls_value_by_id(akvcam_controls_t self,
                                                   __u32 id);
akvcam_control_params_t akvcam_controls_params_by_id(akvcam_controls_t self,
                                                     __u32 id);

akvcam_controls_t akvcam_controls_new(void)
{
    size_t i;
    akvcam_controls_t self = kzalloc(sizeof(struct akvcam_controls), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_controls_delete);
    memset(&self->controls_changed, 0, sizeof(akvcam_controls_changed_callback));

    // Check the number of controls available.
    self->n_controls = 0;

    for (i = 0; akvcam_controls_private[i].id; i++)
        self->n_controls++;

    // Initialize controls with default values.
    self->values = kzalloc(self->n_controls * sizeof(akvcam_control_value), GFP_KERNEL);

    for (i = 0; i < self->n_controls; i++) {
        self->values[i].id = akvcam_controls_private[i].id;
        self->values[i].value = akvcam_controls_private[i].default_value;
    }

    return self;
}

void akvcam_controls_delete(akvcam_controls_t *self)
{
    if (!self || !*self)
        return;

    if (akvcam_object_unref((*self)->self) > 0)
        return;

    kfree((*self)->values);
    akvcam_object_free(&((*self)->self));
    kfree(*self);
    *self = NULL;
}

int akvcam_controls_fill(akvcam_controls_t self,
                         struct v4l2_queryctrl *control)
{
    struct v4l2_query_ext_ctrl ext_control;
    memset(&ext_control, 0, sizeof(struct v4l2_query_ext_ctrl));
    ext_control.id = control->id;

    if (akvcam_controls_fill_ext(self, &ext_control) == -EINVAL)
        return -EINVAL;

    memset(control, 0, sizeof(struct v4l2_queryctrl));
    control->id = ext_control.id;
    control->type = ext_control.type;
    snprintf((char *) control->name, 32, "%s", ext_control.name);
    control->minimum = (__s32) ext_control.minimum;
    control->maximum = (__s32) ext_control.maximum;
    control->step = (__s32) ext_control.step;
    control->default_value = (__s32) ext_control.default_value;
    control->flags = ext_control.flags;

    return 0;
}

int akvcam_controls_fill_ext(akvcam_controls_t self,
                             struct v4l2_query_ext_ctrl *control)
{
    size_t i;
    __u32 id = control->id & V4L2_CTRL_ID_MASK;
    bool next = control->id
              & (V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND);
    akvcam_control_params_t _control = NULL;

    if (self->n_controls < 1)
        return -EINVAL;

    if (!id && next)
        _control = akvcam_controls_private;
    else
        for (i = 0; i < self->n_controls; i++) {
            akvcam_control_params_t ctrl = akvcam_controls_private + i;

            if (ctrl->id == id) {
                if (!next)
                    _control = ctrl;
                else if (i + 1 < self->n_controls)
                    _control = akvcam_controls_private + i + 1;
                else
                    return -EINVAL;

                break;
            }
        }

    if (_control) {
        memset(control, 0, sizeof(struct v4l2_query_ext_ctrl));
        control->id = _control->id;
        control->type = _control->type;
        snprintf(control->name, 32, "%s", _control->name);
        control->minimum = _control->minimum;
        control->maximum = _control->maximum;
        control->step = (__u64) _control->step;
        control->default_value = _control->default_value;
        control->flags = _control->flags;

        return 0;
    }

    return -EINVAL;
}

int akvcam_controls_get(akvcam_controls_t self, struct v4l2_control *control)
{
    int result;
    struct v4l2_ext_controls ext_controls;
    struct v4l2_ext_control ext_control;
    memset(&ext_control, 0, sizeof(struct v4l2_ext_control));
    ext_control.id = control->id;

    memset(&ext_controls, 0, sizeof(struct v4l2_ext_controls));

#ifdef V4L2_CTRL_WHICH_CUR_VAL
    ext_controls.which = V4L2_CTRL_WHICH_CUR_VAL;
#else
    ext_controls.ctrl_class = V4L2_CTRL_CLASS_USER;
#endif

    ext_controls.count = 1;
    ext_controls.controls = &ext_control;

    result = akvcam_controls_get_ext(self,
                                     &ext_controls,
                                     AKVCAM_CONTROLS_FLAG_KERNEL);

    if (result != 0)
        return result;

    control->value = ext_control.value;

    return 0;
}

int akvcam_controls_get_ext(akvcam_controls_t self,
                            struct v4l2_ext_controls *controls,
                            uint32_t flags)
{
    size_t i;
    __u32 id;
    struct v4l2_ext_control *control = NULL;
    akvcam_control_value_t _control;
    bool kernel = flags & AKVCAM_CONTROLS_FLAG_KERNEL;
    int result = akvcam_controls_try_ext(self,
                                         controls,
                                         flags
                                         | AKVCAM_CONTROLS_FLAG_GET);

    if (result != 0)
        return result;

    if (!kernel)
        control = kzalloc(sizeof(struct v4l2_ext_control), GFP_KERNEL);

    for (i = 0; i < controls->count; i++) {
        if (kernel) {
            control = controls->controls + i;
        } else {
            if (copy_from_user(control,
                               controls->controls + i,
                               sizeof(struct v4l2_ext_control)) != 0) {
                result = -EIO;

                break;
            }
        }

        id = control->id;
        _control = akvcam_controls_value_by_id(self, id);
        memset(control, 0, sizeof(struct v4l2_ext_control));
        control->id = id;
        control->value = _control->value;

        if (!kernel)
            if (copy_to_user(controls->controls + i,
                             control,
                             sizeof(struct v4l2_ext_control)) != 0) {
                result = -EIO;

                break;
            }
    }

    if (!kernel)
        kfree(control);

    return result;
}

int akvcam_controls_set(akvcam_controls_t self, struct v4l2_control *control)
{
    struct v4l2_ext_controls ext_controls;
    struct v4l2_ext_control ext_control;
    memset(&ext_control, 0, sizeof(struct v4l2_ext_control));
    ext_control.id = control->id;
    ext_control.value = control->value;

    memset(&ext_controls, 0, sizeof(struct v4l2_ext_controls));

#ifdef V4L2_CTRL_WHICH_CUR_VAL
    ext_controls.which = V4L2_CTRL_WHICH_CUR_VAL;
#else
    ext_controls.ctrl_class = V4L2_CTRL_CLASS_USER;
#endif

    ext_controls.count = 1;
    ext_controls.controls = &ext_control;

    return akvcam_controls_set_ext(self,
                                   &ext_controls,
                                   AKVCAM_CONTROLS_FLAG_KERNEL);
}

int akvcam_controls_set_ext(akvcam_controls_t self,
                            struct v4l2_ext_controls *controls,
                            uint32_t flags)
{
    size_t i;
    akvcam_control_params *control_params;
    struct v4l2_ext_control *control = NULL;
    akvcam_control_value_t _control;
    struct v4l2_event event;
    bool kernel = flags & AKVCAM_CONTROLS_FLAG_KERNEL;
    int result = akvcam_controls_try_ext(self,
                                         controls,
                                         flags
                                         | AKVCAM_CONTROLS_FLAG_SET);

    if (result != 0)
        return result;

    if (!kernel)
        control = kzalloc(sizeof(struct v4l2_ext_control), GFP_KERNEL);

    for (i = 0; i < controls->count; i++) {
        if (kernel) {
            control = controls->controls + i;
        } else {
            if (copy_from_user(control,
                               controls->controls + i,
                               sizeof(struct v4l2_ext_control)) != 0) {
                result = -EIO;

                break;
            }
        }

        _control = akvcam_controls_value_by_id(self, control->id);
        _control->value = control->value;

        if (self->controls_changed.callback) {
            control_params = akvcam_controls_params_by_id(self, control->id);
            event.type = V4L2_EVENT_CTRL;
            event.id = control->id;
            event.u.ctrl.changes = V4L2_EVENT_CTRL_CH_VALUE;
            event.u.ctrl.type = control_params->type;
            event.u.ctrl.value = control->value;
            event.u.ctrl.flags = control_params->flags;
            event.u.ctrl.minimum = control_params->minimum;
            event.u.ctrl.maximum = control_params->maximum;
            event.u.ctrl.step = control_params->step;
            event.u.ctrl.default_value = control_params->default_value;

            self->controls_changed.callback(self->controls_changed.user_data,
                                            &event);
        }
    }

    if (!kernel)
        kfree(control);

    return result;
}

int akvcam_controls_try_ext(akvcam_controls_t self,
                            struct v4l2_ext_controls *controls,
                            uint32_t flags)
{
    size_t i;
    akvcam_control_params_t _control;
    struct v4l2_ext_control *control = NULL;
    int result = 0;
    uint32_t mode = flags & 0x3;
    bool kernel = flags & AKVCAM_CONTROLS_FLAG_KERNEL;
    __u32 which_control;
    controls->error_idx = controls->count;
    memset(controls->reserved, 0, 2 * sizeof(__u32));

#ifdef V4L2_CTRL_WHICH_CUR_VAL
    which_control = controls->which;
#else
    which_control = controls->ctrl_class;
#endif

    if (which_control != V4L2_CTRL_CLASS_USER && which_control != 0)
        return -EINVAL;

    if (!controls->count)
        return 0;

    if (!kernel)
        control = kzalloc(sizeof(struct v4l2_ext_control), GFP_KERNEL);

    for (i = 0; i < controls->count; i++) {
        if (mode == AKVCAM_CONTROLS_FLAG_TRY)
            controls->error_idx = (__u32) i;

        if (kernel) {
            control = controls->controls + i;
        } else {
            if (copy_from_user(control,
                               controls->controls + i,
                               sizeof(struct v4l2_ext_control)) != 0) {
                result = -EIO;

                break;
            }
        }

        *control->reserved2 = 0;
        _control = akvcam_controls_params_by_id(self, control->id);

        if (!_control) {
            result = -EINVAL;

            break;
        }

        if ((mode != AKVCAM_CONTROLS_FLAG_GET
             && (_control->flags & V4L2_CTRL_FLAG_READ_ONLY))
             || (mode == AKVCAM_CONTROLS_FLAG_GET
                 && (_control->flags & V4L2_CTRL_FLAG_WRITE_ONLY))) {
            result = -EACCES;

            break;
        }

        if (mode == AKVCAM_CONTROLS_FLAG_GET)
            continue;

        if (control->value < _control->minimum
            || control->value > _control->maximum) {
            result = -ERANGE;

            break;
        }
    }

    if (!kernel)
        kfree(control);

    return result;
}

bool akvcam_controls_contains(akvcam_controls_t self, __u32 id)
{
    size_t i;

    for (i = 0; i < self->n_controls; i++)
        if (akvcam_controls_private[i].id == id)
            return true;

    return false;
}

bool akvcam_controls_generate_event(akvcam_controls_t self,
                                    __u32 id,
                                    struct v4l2_event *event)
{
    size_t i;
    akvcam_control_params_t control_params;
    akvcam_control_value_t control;

    for (i = 0; i < self->n_controls; i++)
        if (akvcam_controls_private[i].id == id) {
            control = self->values + i;
            control_params = akvcam_controls_private + i;

            if (control_params->type == V4L2_CTRL_TYPE_CTRL_CLASS)
                return false;

            event->type = V4L2_EVENT_CTRL;
            event->id = id;
            event->u.ctrl.changes = V4L2_EVENT_CTRL_CH_VALUE;
            event->u.ctrl.type = control_params->type;
            event->u.ctrl.value = control->value;
            event->u.ctrl.flags = control_params->flags;
            event->u.ctrl.minimum = control_params->minimum;
            event->u.ctrl.maximum = control_params->maximum;
            event->u.ctrl.step = control_params->step;
            event->u.ctrl.default_value = control_params->default_value;

            return true;
        }

    return false;
}

void akvcam_controls_set_changed_callback(akvcam_controls_t self,
                                          akvcam_controls_changed_callback callback)
{
    self->controls_changed = callback;
}

akvcam_control_value_t akvcam_controls_value_by_id(akvcam_controls_t self,
                                                   __u32 id)
{
    size_t i;

    for (i = 0; i < self->n_controls; i++) {
        akvcam_control_value_t value = self->values + i;

        if (value->id == id)
            return value;
    }

    return NULL;
}

akvcam_control_params_t akvcam_controls_params_by_id(akvcam_controls_t self,
                                                     __u32 id)
{
    size_t i;

    for (i = 0; i < self->n_controls; i++) {
        akvcam_control_params_t value = akvcam_controls_private + i;

        if (value->id == id)
            return value;
    }

    return NULL;
}
