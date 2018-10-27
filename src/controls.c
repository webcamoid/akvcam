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

#include "controls.h"
#include "frame_types.h"
#include "object.h"

#ifndef V4L2_CTRL_FLAG_NEXT_COMPOUND
#define V4L2_CTRL_FLAG_NEXT_COMPOUND 0x40000000
#endif

typedef union
{
    __u8  name[32];
    __s64 value;
} akvcam_menu_item, *akvcam_menu_item_t;

typedef akvcam_menu_item_t (*akvcam_control_menu_t)(size_t *size,
                                                    bool *int_menu);

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
    akvcam_control_menu_t menu;
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
    akvcam_control_params_t control_params;
    size_t n_controls;
    AKVCAM_DEVICE_TYPE type;
};

akvcam_menu_item_t akvcam_controls_colorfx_menu(size_t *size,
                                                bool *int_menu);

static akvcam_control_params akvcam_controls_capture[] = {
    {V4L2_CID_USER_CLASS, V4L2_CTRL_TYPE_CTRL_CLASS,   "User Controls",    0,   0, 0, 0, V4L2_CTRL_FLAG_READ_ONLY
                                                                                       | V4L2_CTRL_FLAG_WRITE_ONLY, NULL                        },
    {V4L2_CID_BRIGHTNESS,    V4L2_CTRL_TYPE_INTEGER,      "Brightness", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_CONTRAST  ,    V4L2_CTRL_TYPE_INTEGER,        "Contrast", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_SATURATION,    V4L2_CTRL_TYPE_INTEGER,      "Saturation", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_HUE       ,    V4L2_CTRL_TYPE_INTEGER,             "Hue", -359, 359, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_GAMMA     ,    V4L2_CTRL_TYPE_INTEGER,           "Gamma", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_HFLIP     ,    V4L2_CTRL_TYPE_BOOLEAN, "Horizontal Flip",    0,   1, 1, 0,                         0, NULL                        },
    {V4L2_CID_VFLIP     ,    V4L2_CTRL_TYPE_BOOLEAN,   "Vertical Flip",    0,   1, 1, 0,                         0, NULL                        },
    {V4L2_CID_COLORFX   ,       V4L2_CTRL_TYPE_MENU,   "Color Effects",    0,   1, 1, 0,                         0, akvcam_controls_colorfx_menu},
    {0                  ,                         0,                "",    0,   0, 0, 0,                         0, NULL                        },
};

akvcam_menu_item_t akvcam_controls_scaling_menu(size_t *size,
                                                bool *int_menu);
akvcam_menu_item_t akvcam_controls_aspect_menu(size_t *size,
                                               bool *int_menu);

static akvcam_control_params akvcam_controls_output[] = {
    {V4L2_CID_USER_CLASS    , V4L2_CTRL_TYPE_CTRL_CLASS,      "User Controls", 0, 0, 0, 0, V4L2_CTRL_FLAG_READ_ONLY
                                                                                         | V4L2_CTRL_FLAG_WRITE_ONLY, NULL                        },
    {V4L2_CID_HFLIP         ,    V4L2_CTRL_TYPE_BOOLEAN,    "Horizontal Flip", 0, 1, 1, 0,                         0, NULL                        },
    {V4L2_CID_VFLIP         ,    V4L2_CTRL_TYPE_BOOLEAN,      "Vertical Flip", 0, 1, 1, 0,                         0, NULL                        },
    {AKVCAM_CID_SCALING     ,       V4L2_CTRL_TYPE_MENU,       "Scaling Mode", 0, 1, 1, 0,                         0, akvcam_controls_scaling_menu},
    {AKVCAM_CID_ASPECT_RATIO,       V4L2_CTRL_TYPE_MENU,  "Aspect Ratio Mode", 0, 2, 1, 0,                         0, akvcam_controls_aspect_menu },
    {AKVCAM_CID_SWAP_RGB    ,    V4L2_CTRL_TYPE_BOOLEAN, "Swap Read and Blue", 0, 1, 1, 0,                         0, NULL                        },
    {0                      ,                         0,                   "", 0, 0, 0, 0,                         0, NULL                        },
};

size_t akvcam_controls_capture_count(void);
size_t akvcam_controls_output_count(void);
akvcam_control_value_t akvcam_controls_value_by_id(const akvcam_controls_t self,
                                                   __u32 id);
akvcam_control_params_t akvcam_controls_params_by_id(const akvcam_controls_t self,
                                                     __u32 id);

akvcam_controls_t akvcam_controls_new(AKVCAM_DEVICE_TYPE type)
{
    size_t i;
    akvcam_controls_t self = kzalloc(sizeof(struct akvcam_controls), GFP_KERNEL);
    self->self = akvcam_object_new(self, (akvcam_deleter_t) akvcam_controls_delete);

    // Initialize controls with default values.
    if (type == AKVCAM_DEVICE_TYPE_OUTPUT) {
        self->n_controls = akvcam_controls_output_count();
        self->control_params = akvcam_controls_output;
    } else {
        self->n_controls = akvcam_controls_capture_count();
        self->control_params = akvcam_controls_capture;
    }

    self->values = kzalloc(self->n_controls * sizeof(akvcam_control_value), GFP_KERNEL);

    for (i = 0; i < self->n_controls; i++) {
        self->values[i].id = self->control_params[i].id;
        self->values[i].value = self->control_params[i].default_value;
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

int akvcam_controls_fill(const akvcam_controls_t self,
                         struct v4l2_queryctrl *control)
{
#ifdef VIDIOC_QUERY_EXT_CTRL
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
#else
    return -ENOTTY;
#endif
}

int akvcam_controls_fill_menu(const akvcam_controls_t self,
                              struct v4l2_querymenu *menu)
{
    akvcam_menu_item_t menu_items;
    size_t size = 0;
    bool int_menu = false;
    akvcam_control_params_t params;

    params = akvcam_controls_params_by_id(self, menu->id);
    menu->reserved = 0;

    if (!params || !params->menu)
        return -EINVAL;

    menu_items = params->menu(&size, &int_menu);

    if (!menu_items || size < 1)
        return -EINVAL;

    if ((ssize_t) menu->index < params->minimum
        || (ssize_t) menu->index > params->maximum
        || menu->index >= size)
        return -EINVAL;

    if (int_menu)
        menu->value = menu_items[menu->index].value;
    else
        snprintf((char*) menu->name, 32, "%s", menu_items[menu->index].name);

    return 0;
}

#ifdef VIDIOC_QUERY_EXT_CTRL
int akvcam_controls_fill_ext(const akvcam_controls_t self,
                             struct v4l2_query_ext_ctrl *control)
{
    size_t i;
    __u32 priv_id;
    __u32 id = control->id
             & ~(V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND);
    bool next = control->id
              & (V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND);
    bool is_private;
    akvcam_control_params_t _control = NULL;

    if (self->n_controls < 1)
        return -EINVAL;

    if (!id && next) {
        _control = self->control_params;
    } else {
        priv_id = V4L2_CID_PRIVATE_BASE;

        for (i = 0; i < self->n_controls; i++) {
            akvcam_control_params_t ctrl = self->control_params + i;
            is_private = V4L2_CTRL_DRIVER_PRIV(ctrl->id);

            if (ctrl->id == id || (is_private && priv_id == id)) {
                if (!next)
                    _control = ctrl;
                else if (i + 1 < self->n_controls)
                    _control = self->control_params + i + 1;
                else
                    return -EINVAL;

                break;
            }

            if (is_private)
                priv_id++;
        }
    }

    if (_control) {
        memset(control, 0, sizeof(struct v4l2_query_ext_ctrl));
        control->id = id >= V4L2_CID_PRIVATE_BASE? id: _control->id;
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
#endif

int akvcam_controls_get(const akvcam_controls_t self,
                        struct v4l2_control *control)
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

int akvcam_controls_get_ext(const akvcam_controls_t self,
                            struct v4l2_ext_controls *controls,
                            uint32_t flags)
{
#ifdef VIDIOC_QUERY_EXT_CTRL
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
                               (struct v4l2_ext_control __user *)
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
            if (copy_to_user((struct v4l2_ext_control __user *)
                             controls->controls + i,
                             control,
                             sizeof(struct v4l2_ext_control)) != 0) {
                result = -EIO;

                break;
            }
    }

    if (!kernel)
        kfree(control);

    return result;
#else
    return -ENOTTY;
#endif
}

int akvcam_controls_set(akvcam_controls_t self,
                        const struct v4l2_control *control)
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
                               (struct v4l2_ext_control __user *)
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
            memset(&event, 0, sizeof(struct v4l2_event));
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
                               (struct v4l2_ext_control __user *)
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

bool akvcam_controls_contains(const akvcam_controls_t self, __u32 id)
{
    size_t i;

    for (i = 0; i < self->n_controls; i++)
        if (self->control_params[i].id == id)
            return true;

    return false;
}

bool akvcam_controls_generate_event(const akvcam_controls_t self,
                                    __u32 id,
                                    struct v4l2_event *event)
{
    size_t i;
    akvcam_control_params_t control_params;
    akvcam_control_value_t control;

    for (i = 0; i < self->n_controls; i++)
        if (self->control_params[i].id == id) {
            control = self->values + i;
            control_params = self->control_params + i;

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
                                          const akvcam_controls_changed_callback callback)
{
    self->controls_changed = callback;
}

size_t akvcam_controls_capture_count(void)
{
    size_t i;
    static size_t count = 0;

    if (count < 1)
        for (i = 0; akvcam_controls_capture[i].id; i++)
            count++;

    return count;
}

size_t akvcam_controls_output_count(void)
{
    size_t i;
    static size_t count = 0;

    if (count < 1)
        for (i = 0; akvcam_controls_output[i].id; i++)
            count++;

    return count;
}

akvcam_menu_item_t akvcam_controls_colorfx_menu(size_t *size,
                                                bool *int_menu)
{
    static akvcam_menu_item colorfx[] = {
        {.name = "None"         },
        {.name = "Black & White"},
    };

    if (size)
        *size = 2;

    if (int_menu)
        *int_menu = false;

    return colorfx;
}

akvcam_menu_item_t akvcam_controls_scaling_menu(size_t *size,
                                                bool *int_menu)
{
    static akvcam_menu_item scaling[] = {
        {.name = "Fast"  },
        {.name = "Linear"},
    };

    if (size)
        *size = 2;

    if (int_menu)
        *int_menu = false;

    return scaling;
}

akvcam_menu_item_t akvcam_controls_aspect_menu(size_t *size,
                                               bool *int_menu)
{
    static akvcam_menu_item aspect[] = {
        {.name = "Ignore"   },
        {.name = "Keep"     },
        {.name = "Expanding"},
    };

    if (size)
        *size = 3;

    if (int_menu)
        *int_menu = false;

    return aspect;
}

akvcam_control_value_t akvcam_controls_value_by_id(const akvcam_controls_t self,
                                                   __u32 id)
{
    size_t i;
    __u32 priv_id = V4L2_CID_PRIVATE_BASE;
    bool is_private;
    akvcam_control_value_t value;

    for (i = 0; i < self->n_controls; i++) {
        value = self->values + i;
        is_private = V4L2_CTRL_DRIVER_PRIV(value->id);

        if (value->id == id || (is_private && priv_id == id))
            return value;

        if (is_private)
            priv_id++;
    }

    return NULL;
}

akvcam_control_params_t akvcam_controls_params_by_id(const akvcam_controls_t self,
                                                     __u32 id)
{
    size_t i;
    __u32 priv_id = V4L2_CID_PRIVATE_BASE;
    bool is_private;
    akvcam_control_params_t param;

    for (i = 0; i < self->n_controls; i++) {
        param = self->control_params + i;
        is_private = V4L2_CTRL_DRIVER_PRIV(param->id);

        if (param->id == id || (is_private && priv_id == id))
            return param;

        if (is_private)
            priv_id++;
    }

    return NULL;
}
