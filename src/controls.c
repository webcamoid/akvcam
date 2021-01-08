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

#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "controls.h"
#include "frame_types.h"
#include "list.h"
#include "log.h"

typedef union
{
    __u8  name[32];
    __s64 value;
} akvcam_menu_item, *akvcam_menu_item_t;
typedef const akvcam_menu_item *akvcam_menu_item_ct;

typedef akvcam_menu_item_ct (*akvcam_control_menu_t)(akvcam_controls_ct controls,
                                                     size_t *size,
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
typedef const akvcam_control_params *akvcam_control_params_ct;

typedef struct
{
    __u32 id;
    __s32 value;
    char value_str[AKVCAM_MAX_STRING_SIZE];
} akvcam_control_value, *akvcam_control_value_t;
typedef const akvcam_control_value *akvcam_control_value_ct;

struct akvcam_controls
{
    struct kref ref;
    akvcam_control_value_t values;
    akvcam_signal_callback(controls, updated);
    akvcam_control_params_ct control_params;
    size_t n_controls;
    AKVCAM_DEVICE_TYPE type;
};

akvcam_signal_define(controls, updated)

akvcam_menu_item_ct akvcam_controls_colorfx_menu(akvcam_controls_ct controls,
                                                 size_t *size,
                                                 bool *int_menu);

static const akvcam_control_params akvcam_controls_capture[] = {
    {V4L2_CID_USER_CLASS, V4L2_CTRL_TYPE_CTRL_CLASS,   "User Controls",    0,   0, 0, 0, V4L2_CTRL_FLAG_READ_ONLY
                                                                                       | V4L2_CTRL_FLAG_WRITE_ONLY, NULL                        },
    {V4L2_CID_BRIGHTNESS,    V4L2_CTRL_TYPE_INTEGER,      "Brightness", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_CONTRAST  ,    V4L2_CTRL_TYPE_INTEGER,        "Contrast", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_SATURATION,    V4L2_CTRL_TYPE_INTEGER,      "Saturation", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_HUE       ,    V4L2_CTRL_TYPE_INTEGER,             "Hue", -359, 359, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_GAMMA     ,    V4L2_CTRL_TYPE_INTEGER,           "Gamma", -255, 255, 1, 0,     V4L2_CTRL_FLAG_SLIDER, NULL                        },
    {V4L2_CID_HFLIP     ,    V4L2_CTRL_TYPE_BOOLEAN, "Horizontal Flip",    0,   1, 1, 0,                         0, NULL                        },
    {V4L2_CID_VFLIP     ,    V4L2_CTRL_TYPE_BOOLEAN,   "Vertical Flip",    0,   1, 1, 0,                         0, NULL                        },
    {V4L2_CID_COLORFX   ,       V4L2_CTRL_TYPE_MENU,   "Color Effects",    0,   0, 1, 0,                         0, akvcam_controls_colorfx_menu},
    {0                  ,                         0,                "",    0,   0, 0, 0,                         0, NULL                        },
};

akvcam_menu_item_ct akvcam_controls_scaling_menu(akvcam_controls_ct controls,
                                                 size_t *size,
                                                 bool *int_menu);
akvcam_menu_item_ct akvcam_controls_aspect_menu(akvcam_controls_ct controls,
                                                size_t *size,
                                                bool *int_menu);

static const akvcam_control_params akvcam_controls_output[] = {
    {V4L2_CID_USER_CLASS    , V4L2_CTRL_TYPE_CTRL_CLASS,      "User Controls", 0, 0, 0, 0, V4L2_CTRL_FLAG_READ_ONLY
                                                                                         | V4L2_CTRL_FLAG_WRITE_ONLY, NULL                        },
    {V4L2_CID_HFLIP         ,    V4L2_CTRL_TYPE_BOOLEAN,    "Horizontal Flip", 0, 1, 1, 0,                         0, NULL                        },
    {V4L2_CID_VFLIP         ,    V4L2_CTRL_TYPE_BOOLEAN,      "Vertical Flip", 0, 1, 1, 0,                         0, NULL                        },
    {AKVCAM_CID_SCALING     ,       V4L2_CTRL_TYPE_MENU,       "Scaling Mode", 0, 0, 1, 0,                         0, akvcam_controls_scaling_menu},
    {AKVCAM_CID_ASPECT_RATIO,       V4L2_CTRL_TYPE_MENU,  "Aspect Ratio Mode", 0, 0, 1, 0,                         0, akvcam_controls_aspect_menu },
    {AKVCAM_CID_SWAP_RGB    ,    V4L2_CTRL_TYPE_BOOLEAN, "Swap Read and Blue", 0, 1, 1, 0,                         0, NULL                        },
    {0                      ,                         0,                   "", 0, 0, 0, 0,                         0, NULL                        },
};

size_t akvcam_controls_capture_count(void);
size_t akvcam_controls_output_count(void);
akvcam_control_value_t akvcam_controls_value_by_id(akvcam_controls_ct self,
                                                   __u32 id);
akvcam_control_params_ct akvcam_controls_params_by_id(akvcam_controls_ct self,
                                                     __u32 id);

akvcam_controls_t akvcam_controls_new(AKVCAM_DEVICE_TYPE device_type)
{
    size_t i;
    akvcam_controls_t self = kzalloc(sizeof(struct akvcam_controls), GFP_KERNEL);
    kref_init(&self->ref);

    // Initialize controls with default values.
    if (device_type == AKVCAM_DEVICE_TYPE_OUTPUT) {
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
        memset(self->values[i].value_str, 0, AKVCAM_MAX_STRING_SIZE);
    }

    return self;
}

static void akvcam_controls_free(struct kref *ref)
{
    akvcam_controls_t self = container_of(ref, struct akvcam_controls, ref);
    kfree(self->values);
    kfree(self);
}

void akvcam_controls_delete(akvcam_controls_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_controls_free);
}

akvcam_controls_t akvcam_controls_ref(akvcam_controls_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

int akvcam_controls_query(akvcam_controls_ct self,
                          struct v4l2_queryctrl *control)
{
    __u32 id = control->id & V4L2_CTRL_ID_MASK;
    bool next = control->id
              & (V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND);
    akvcam_control_params_ct _control = NULL;

    akpr_function();

    if (self->n_controls < 1)
        return -EINVAL;

    if (!id && next) {
        _control = self->control_params;
    } else {
        size_t i;
        __u32 priv_id = V4L2_CID_PRIVATE_BASE;

        for (i = 0; i < self->n_controls; i++) {
            akvcam_control_params_ct ctrl = self->control_params + i;
            bool is_private = V4L2_CTRL_DRIVER_PRIV(ctrl->id);

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

    if (!_control)
        return -EINVAL;

    memset(control, 0, sizeof(struct v4l2_queryctrl));
    control->id = id >= V4L2_CID_PRIVATE_BASE? id: _control->id;
    control->type = _control->type;
    snprintf((char *) control->name, 32, "%s", _control->name);
    control->minimum = _control->minimum;

    if (_control->menu) {
        size_t menu_size = 0;
        _control->menu(self, &menu_size, NULL);
        control->maximum = (__s32) akvcam_max(menu_size - 1, 0);
    } else {
        control->maximum = _control->maximum;
    }

    control->step = _control->step;
    control->default_value = _control->default_value;
    control->flags = _control->flags;

    return 0;
}

int akvcam_controls_fill_menu(akvcam_controls_ct self,
                              struct v4l2_querymenu *menu)
{
    akvcam_menu_item_ct menu_items;
    size_t size = 0;
    bool int_menu = false;
    akvcam_control_params_ct params;

    akpr_function();
    params = akvcam_controls_params_by_id(self, menu->id);
    menu->reserved = 0;

    if (!params || !params->menu)
        return -EINVAL;

    menu_items = params->menu(self, &size, &int_menu);

    if (!menu_items || size < 1)
        return -EINVAL;

    if ((ssize_t) menu->index < params->minimum
        || menu->index >= size)
        return -EINVAL;

    if (int_menu)
        menu->value = menu_items[menu->index].value;
    else
        snprintf((char*) menu->name, 32, "%s", menu_items[menu->index].name);

    return 0;
}

int akvcam_controls_get(akvcam_controls_ct self,
                        struct v4l2_control *control)
{
    akvcam_control_params_ct control_params;
    akvcam_control_value_ct control_value;

    akpr_function();

    control_params = akvcam_controls_params_by_id(self, control->id);
    control_value = akvcam_controls_value_by_id(self, control->id);

    if (!control_value) {
        akpr_err("Control ID not found: 0x%08x\n", control->id);

        return -EINVAL;
    }

    akpr_info("Reading '%s' control.\n", control_params->name);

    if (control_params->flags & V4L2_CTRL_FLAG_WRITE_ONLY) {
        akpr_err("Control not readable\n");

        return -EACCES;
    }

    control->value = control_value->value;
    akpr_info("Control value: %d\n", control->value);

    return 0;
}

int akvcam_controls_set(akvcam_controls_t self,
                        const struct v4l2_control *control)
{
    akvcam_control_params_ct control_params;
    akvcam_control_value_t control_value;

    akpr_function();

    control_params = akvcam_controls_params_by_id(self, control->id);
    control_value = akvcam_controls_value_by_id(self, control->id);

    if (!control_value) {
        akpr_err("Control ID not found: 0x%08x\n", control->id);

        return -EINVAL;
    }

    akpr_info("Writing '%s' control.\n", control_params->name);

    if (control_params->flags & V4L2_CTRL_FLAG_READ_ONLY) {
        akpr_err("Control not writable\n");

        return -EACCES;
    }

    if (control->value < control_params->minimum)
        return -ERANGE;

    if (control_params->menu) {
        size_t menu_size = 0;
        control_params->menu(self, &menu_size, NULL);

        if (control->value >= (__s32) menu_size)
            return -ERANGE;
    } else if (control->value > control_params->maximum) {
        return -ERANGE;
    }

    control_value->value = control->value;
    akpr_info("Control value: %d\n", control->value);

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
int akvcam_controls_query_ext(akvcam_controls_ct self,
                              struct v4l2_query_ext_ctrl *control)
{
    __u32 id = control->id & V4L2_CTRL_ID_MASK;
    bool next = control->id
              & (V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND);
    akvcam_control_params_ct _control = NULL;

    akpr_function();

    if (self->n_controls < 1)
        return -EINVAL;

    if (!id && next) {
        _control = self->control_params;
    } else {
        size_t i;
        __u32 priv_id = V4L2_CID_PRIVATE_BASE;

        for (i = 0; i < self->n_controls; i++) {
            akvcam_control_params_ct ctrl = self->control_params + i;
            bool is_private = V4L2_CTRL_DRIVER_PRIV(ctrl->id);

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

    if (!_control)
        return -EINVAL;

    memset(control, 0, sizeof(struct v4l2_query_ext_ctrl));
    control->id = id >= V4L2_CID_PRIVATE_BASE? id: _control->id;
    control->type = _control->type;
    snprintf(control->name, 32, "%s", _control->name);
    control->minimum = _control->minimum;

    if (_control->menu) {
        size_t menu_size = 0;
        _control->menu(self, &menu_size, NULL);
        control->maximum = (__s64) akvcam_max(menu_size - 1, 0);
    } else {
        control->maximum = _control->maximum;
    }

    control->step = (__u64) _control->step;
    control->default_value = _control->default_value;
    control->flags = _control->flags;

    return 0;
}

int akvcam_controls_try_ext(akvcam_controls_ct self,
                            struct v4l2_ext_controls *controls)
{
    size_t i;
    akvcam_control_params_ct control_params;
    struct v4l2_ext_control *control = NULL;
    int result = 0;

    akpr_function();
    controls->error_idx = controls->count;
    akvcam_init_reserved(controls);

    if (controls->which == V4L2_CTRL_WHICH_DEF_VAL)
        return -EINVAL;

    control = kzalloc(sizeof(struct v4l2_ext_control), GFP_KERNEL);

    for (i = 0; i < controls->count; i++) {
        controls->error_idx = (__u32) i;

        if (copy_from_user(control,
                           (struct v4l2_ext_control __user *)
                           controls->controls + i,
                           sizeof(struct v4l2_ext_control)) != 0) {
            result = -EIO;

            break;
        }

        akvcam_init_field(control, reserved2);
        control_params = akvcam_controls_params_by_id(self, control->id);

        if (!control_params) {
            akpr_err("Control ID not found: 0x%08x\n", control->id);
            result = -EINVAL;

            break;
        }

        akpr_info("Trying '%s' control.\n", control_params->name);

        if (control_params->flags & V4L2_CTRL_FLAG_READ_ONLY) {
            akpr_err("Control not writable\n");
            result = -EACCES;

            break;
        }

        if (control->value < control_params->minimum) {
            result = -ERANGE;

            break;
        }

        if (control_params->menu) {
            size_t menu_size = 0;
            control_params->menu(self, &menu_size, NULL);

            if (control->value >= (__s32) menu_size) {
                result = -ERANGE;

                break;
            }
        } else if (control->value > control_params->maximum) {
            result = -ERANGE;

            break;
        }
    }

    kfree(control);

    return result;
}

int akvcam_controls_get_ext(akvcam_controls_ct self,
                            struct v4l2_ext_controls *controls)
{
    size_t i;
    struct v4l2_ext_control *control = NULL;
    int result = 0;

    akpr_function();
    akpr_debug("IN: %s\n", akvcam_string_from_ext_controls(controls));
    controls->error_idx = controls->count;
    akvcam_init_reserved(controls);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
    if (controls->which == V4L2_CTRL_WHICH_REQUEST_VAL)
        return -ENOTTY;
#endif

    control = kzalloc(sizeof(struct v4l2_ext_control), GFP_KERNEL);

    for (i = 0; i < controls->count; i++) {
        akvcam_control_params_ct control_params;
        akvcam_control_value_ct control_value;

        if (copy_from_user(control,
                           (struct v4l2_ext_control __user *)
                           controls->controls + i,
                           sizeof(struct v4l2_ext_control)) != 0) {
            result = -EIO;

            break;
        }

        akvcam_init_field(control, reserved2);
        akpr_info("Control ID: 0x%08x\n", control->id);
        control_params = akvcam_controls_params_by_id(self, control->id);
        control_value = akvcam_controls_value_by_id(self, control->id);

        if (!control_value || !control_value) {
            akpr_err("Control ID not found: 0x%08x\n", control->id);
            result = -EINVAL;

            break;
        }

        akpr_info("Control name: %s\n", control_params->name);

        if (control_params->flags & V4L2_CTRL_FLAG_WRITE_ONLY) {
            akpr_err("Control not readable\n");
            result = -EACCES;

            break;
        }

        if (control_params->type == V4L2_CTRL_TYPE_STRING) {
            __u32 buffer_size =
                    akvcam_min(control->size, AKVCAM_MAX_STRING_SIZE);
            size_t str_len = akvcam_strlen(control_value->value_str);

            if (buffer_size < str_len) {
                result = -ENOSPC;

                break;
            }

            if (clear_user((char __user *) control->string,
                           control->size) != 0) {
                result = -EIO;

                break;
            }

            if (copy_to_user((char __user *) control->string,
                             control_value->value_str,
                             str_len) != 0) {
                result = -EIO;

                break;
            }
        } else {
            if (controls->which == V4L2_CTRL_WHICH_CUR_VAL
                || controls->which == V4L2_CTRL_ID2WHICH(control->id)) {
                control->value = control_value->value;
            } else if (controls->which == V4L2_CTRL_WHICH_DEF_VAL) {
                control->value = control_params->default_value;
            } else {
                result = -EIO;

                break;
            }

            if (result == 0)
                akpr_info("Control value: %d\n", control->value);
        }

        akvcam_init_field(control, reserved2);

        if (copy_to_user((struct v4l2_ext_control __user *)
                         controls->controls + i,
                         control,
                         sizeof(struct v4l2_ext_control)) != 0) {
            result = -EIO;

            break;
        }
    }

    kfree(control);

    return result;
}

int akvcam_controls_set_ext(akvcam_controls_t self,
                            struct v4l2_ext_controls *controls)
{
    size_t i;
    struct v4l2_ext_control *control = NULL;
    struct v4l2_event event;
    int result;

    akpr_function();
    akpr_debug("IN: %s\n", akvcam_string_from_ext_controls(controls));
    controls->error_idx = controls->count;
    akvcam_init_reserved(controls);

    if (controls->which == V4L2_CTRL_WHICH_DEF_VAL)
        return -EINVAL;

    control = kzalloc(sizeof(struct v4l2_ext_control), GFP_KERNEL);

    if (!control)
        return -ENOMEM;

    for (i = 0; i < controls->count; i++) {
        akvcam_control_params_ct control_params;
        akvcam_control_value_t control_value;

        if (copy_from_user(control,
                           (struct v4l2_ext_control __user *)
                           controls->controls + i,
                           sizeof(struct v4l2_ext_control)) != 0) {
            result = -EIO;

            break;
        }

        akvcam_init_field(control, reserved2);
        akpr_info("Control ID: 0x%08x\n", control->id);
        control_params = akvcam_controls_params_by_id(self, control->id);
        control_value = akvcam_controls_value_by_id(self, control->id);

        if (!control_value || !control_value) {
            akpr_err("Control ID not found: 0x%08x\n", control->id);
            result = -EINVAL;

            break;
        }

        akpr_info("Control name: %s\n", control_params->name);

        if (control_params->flags & V4L2_CTRL_FLAG_READ_ONLY) {
            akpr_err("Control not writable\n");
            result = -EACCES;

            break;
        }

        if (control->value < control_params->minimum) {
            result = -ERANGE;

            break;
        }

        if (control_params->menu) {
            size_t menu_size = 0;
            control_params->menu(self, &menu_size, NULL);

            if (control->value >= (__s32) menu_size) {
                result = -ERANGE;

                break;
            }
        } else if (control->value > control_params->maximum) {
            result = -ERANGE;

            break;
        }

        akpr_info("Control value: %d\n", control->value);
        control_value->value = control->value;

        if (akvcam_controls_generate_event(self, control->id, &event))
            akvcam_emit(self, updated, &event);
    }

    kfree(control);

    return result;
}
#endif

bool akvcam_controls_contains(akvcam_controls_ct self, __u32 id)
{
    size_t i;

    for (i = 0; i < self->n_controls; i++)
        if (self->control_params[i].id == id)
            return true;

    return false;
}

bool akvcam_controls_generate_event(akvcam_controls_ct self,
                                    __u32 id,
                                    struct v4l2_event *event)
{
    akvcam_control_params_ct control_params;
    akvcam_control_value_ct control_value;

    akpr_function();
    control_params = akvcam_controls_params_by_id(self, id);
    control_value = akvcam_controls_value_by_id(self, id);

    if (!control_params || !control_value)
        return false;

    if (control_params->type == V4L2_CTRL_TYPE_CTRL_CLASS)
        return false;

    memset(event, 0, sizeof(struct v4l2_event));
    event->type = V4L2_EVENT_CTRL;
    event->id = id;
    event->u.ctrl.changes = V4L2_EVENT_CTRL_CH_VALUE;
    event->u.ctrl.type = control_params->type;
    event->u.ctrl.value = control_value->value;
    event->u.ctrl.flags = control_params->flags;
    event->u.ctrl.minimum = control_params->minimum;

    if (control_params->menu) {
        size_t menu_size = 0;
        control_params->menu(self, &menu_size, NULL);
        event->u.ctrl.maximum = (__s32) akvcam_max(menu_size - 1, 0);
    } else {
        event->u.ctrl.maximum = control_params->maximum;
    }

    event->u.ctrl.step = control_params->step;
    event->u.ctrl.default_value = control_params->default_value;

    return true;
}

size_t akvcam_controls_capture_count(void)
{
    static size_t count = 0;

    if (count < 1) {
        size_t i;

        for (i = 0; akvcam_controls_capture[i].id; i++)
            count++;
    }

    return count;
}

size_t akvcam_controls_output_count(void)
{
    static size_t count = 0;

    if (count < 1) {
        size_t i;

        for (i = 0; akvcam_controls_output[i].id; i++)
            count++;
    }

    return count;
}

akvcam_menu_item_ct akvcam_controls_colorfx_menu(akvcam_controls_ct controls,
                                                 size_t *size,
                                                 bool *int_menu)
{
    static const akvcam_menu_item colorfx[] = {
        {.name = "None"         },
        {.name = "Black & White"},
    };

    UNUSED(controls);

    if (size)
        *size = 2;

    if (int_menu)
        *int_menu = false;

    return colorfx;
}

akvcam_menu_item_ct akvcam_controls_scaling_menu(akvcam_controls_ct controls,
                                                 size_t *size,
                                                 bool *int_menu)
{
    static const akvcam_menu_item scaling[] = {
        {.name = "Fast"  },
        {.name = "Linear"},
    };

    UNUSED(controls);

    if (size)
        *size = 2;

    if (int_menu)
        *int_menu = false;

    return scaling;
}

akvcam_menu_item_ct akvcam_controls_aspect_menu(akvcam_controls_ct controls,
                                                size_t *size,
                                                bool *int_menu)
{
    static const akvcam_menu_item aspect[] = {
        {.name = "Ignore"   },
        {.name = "Keep"     },
        {.name = "Expanding"},
    };

    UNUSED(controls);

    if (size)
        *size = 3;

    if (int_menu)
        *int_menu = false;

    return aspect;
}

akvcam_control_value_t akvcam_controls_value_by_id(akvcam_controls_ct self,
                                                   __u32 id)
{
    size_t i;
    __u32 priv_id = V4L2_CID_PRIVATE_BASE;

    for (i = 0; i < self->n_controls; i++) {
        akvcam_control_value_t value = self->values + i;
        bool is_private = V4L2_CTRL_DRIVER_PRIV(value->id);

        if (value->id == id || (is_private && priv_id == id))
            return value;

        if (is_private)
            priv_id++;
    }

    return NULL;
}

akvcam_control_params_ct akvcam_controls_params_by_id(akvcam_controls_ct self,
                                                      __u32 id)
{
    size_t i;
    __u32 priv_id = V4L2_CID_PRIVATE_BASE;

    for (i = 0; i < self->n_controls; i++) {
        akvcam_control_params_ct param = self->control_params + i;
        bool is_private = V4L2_CTRL_DRIVER_PRIV(param->id);

        if (param->id == id || (is_private && priv_id == id))
            return param;

        if (is_private)
            priv_id++;
    }

    return NULL;
}
