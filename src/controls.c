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
#include <media/v4l2-ctrls.h>

#include "controls.h"
#include "frame_types.h"
#include "log.h"

struct akvcam_controls
{
    struct kref ref;
    struct v4l2_ctrl_handler handler;
    akvcam_signal_callback(controls, updated);
    const struct v4l2_ctrl_config *control_params;
    size_t n_controls;
};

akvcam_signal_define(controls, updated)

static const struct v4l2_ctrl_ops akvcam_controls_ops;

static const struct v4l2_ctrl_config akvcam_controls_capture[] = {
    {.id = V4L2_CID_BRIGHTNESS, .min = -255             , .max = 255            , .step = 1},
    {.id = V4L2_CID_CONTRAST  , .min = -255             , .max = 255            , .step = 1},
    {.id = V4L2_CID_SATURATION, .min = -255             , .max = 255            , .step = 1},
    {.id = V4L2_CID_HUE       , .min = -359             , .max = 359            , .step = 1},
    {.id = V4L2_CID_GAMMA     , .min = -255             , .max = 255            , .step = 1},
    {.id = V4L2_CID_HFLIP     , .min = 0                , .max = 1              , .step = 1},
    {.id = V4L2_CID_VFLIP     , .min = 0                , .max = 1              , .step = 1},
    {.id = V4L2_CID_COLORFX   , .min = V4L2_COLORFX_NONE, .max = V4L2_COLORFX_BW, .step = 1},
    {.id = 0                                                                               },
};

static const char * const akvcam_controls_scaling_menu[] = {
    [AKVCAM_SCALING_FAST  ] = "Fast"  ,
    [AKVCAM_SCALING_LINEAR] = "Linear",
};

static const char * const akvcam_controls_aspect_menu[] = {
    [AKVCAM_ASPECT_RATIO_IGNORE   ] = "Ignore"   ,
    [AKVCAM_ASPECT_RATIO_KEEP     ] = "Keep"     ,
    [AKVCAM_ASPECT_RATIO_EXPANDING] = "Expanding",
};

static const struct v4l2_ctrl_config akvcam_controls_output[] = {
    {.id = V4L2_CID_HFLIP, .max = 1, .step = 1},
    {.id = V4L2_CID_VFLIP, .max = 1, .step = 1},
    {
        .id = AKVCAM_CID_SCALING,
        .type = V4L2_CTRL_TYPE_MENU,
        .name = "Scaling Mode",
        .max = ARRAY_SIZE(akvcam_controls_scaling_menu) - 1,
        .step = 0,
        .qmenu = akvcam_controls_scaling_menu,
        .ops = &akvcam_controls_ops
    },
    {
        .id = AKVCAM_CID_ASPECT_RATIO,
        .type = V4L2_CTRL_TYPE_MENU,
        .name = "Aspect Ratio Mode" ,
        .max = ARRAY_SIZE(akvcam_controls_aspect_menu) - 1,
        .step = 0,
        .qmenu = akvcam_controls_aspect_menu,
        .ops = &akvcam_controls_ops
    },
    {
        .id = AKVCAM_CID_SWAP_RGB,
        .type = V4L2_CTRL_TYPE_BOOLEAN,
        .name = "Swap Read and Blue",
        .max = 1,
        .step = 1,
        .ops = &akvcam_controls_ops
    },
    {.id = 0},
};

size_t akvcam_controls_capture_count(void);
size_t akvcam_controls_output_count(void);
int akvcam_controls_cotrol_changed(struct v4l2_ctrl *control);

akvcam_controls_t akvcam_controls_new(AKVCAM_DEVICE_TYPE device_type)
{
    akvcam_controls_t self = kzalloc(sizeof(struct akvcam_controls), GFP_KERNEL);
    size_t i;

    kref_init(&self->ref);

    // Initialize controls with default values.
    if (device_type == AKVCAM_DEVICE_TYPE_OUTPUT) {
        self->control_params = akvcam_controls_output;
        self->n_controls = akvcam_controls_output_count();
    } else {
        self->control_params = akvcam_controls_capture;
        self->n_controls = akvcam_controls_capture_count();
    }

    v4l2_ctrl_handler_init(&self->handler, self->n_controls);

    for (i = 0; i < self->n_controls; i++) {
        const struct v4l2_ctrl_config *params = self->control_params + i;

        if (params->id < AKVCAM_CID_BASE) {
            const char *name;
            enum v4l2_ctrl_type type;
            s64 min;
            s64 max;
            u64 step;
            s64 def;
            u32 flags;
            v4l2_ctrl_fill(params->id,
                           &name,
                           &type,
                           &min,
                           &max,
                           &step,
                           &def,
                           &flags);

            if (type == V4L2_CTRL_TYPE_MENU) {
                v4l2_ctrl_new_std_menu(&self->handler,
                                       &akvcam_controls_ops,
                                       params->id,
                                       params->max,
                                       params->menu_skip_mask,
                                       params->def);
            } else {
                v4l2_ctrl_new_std(&self->handler,
                                  &akvcam_controls_ops,
                                  params->id,
                                  params->min,
                                  params->max,
                                  params->step,
                                  params->def);
            }
        } else {
            v4l2_ctrl_new_custom(&self->handler, params, self);
        }
    }

    return self;
}

static void akvcam_controls_free(struct kref *ref)
{
    akvcam_controls_t self = container_of(ref, struct akvcam_controls, ref);
    v4l2_ctrl_handler_free(&self->handler);;
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
__s32 akvcam_controls_value(akvcam_controls_t self, __u32 id)
{
    struct v4l2_ctrl *control = v4l2_ctrl_find(&self->handler, id);

    if (!control)
        return 0;

    return v4l2_ctrl_g_ctrl(control);
}

const char *akvcam_controls_string_value(akvcam_controls_t self, __u32 id)
{
    struct v4l2_ctrl *control = v4l2_ctrl_find(&self->handler, id);

    if (!control || !control->qmenu)
        return NULL;

    return control->qmenu[v4l2_ctrl_g_ctrl(control)];
}

int akvcam_controls_set_value(akvcam_controls_t self, __u32 id, __s32 value)
{
    struct v4l2_ctrl *control = v4l2_ctrl_find(&self->handler, id);
    int result;

    if (!control)
        return -EINVAL;

    result = v4l2_ctrl_s_ctrl(control, value);

    if (result)
        return result;

    akvcam_emit(self, updated, control->id, control->val);

    return 0;
}

int akvcam_controls_set_string_value(akvcam_controls_t self, __u32 id, const char *value)
{
    struct v4l2_ctrl *control = v4l2_ctrl_find(&self->handler, id);
    s64 i;
    int result = -EINVAL;

    if (!control || !control->qmenu)
        return -EINVAL;

    for (i = 0; i <= control->maximum; i++)
        if (strncmp(control->qmenu[i], value, AKVCAM_MAX_STRING_SIZE) == 0) {
            result = 0;

            break;
        }

    if (result)
        return result;

    result = v4l2_ctrl_s_ctrl(control, i);

    if (result)
        return result;

    akvcam_emit(self, updated, control->id, control->val);

    return 0;
}

struct v4l2_ctrl_handler *akvcam_controls_handler(akvcam_controls_t self)
{
    return self->handler.error? NULL: &self->handler;
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

int akvcam_controls_cotrol_changed(struct v4l2_ctrl *control)
{
    akvcam_controls_t self =
            container_of(control->handler, struct akvcam_controls, handler);

    akvcam_emit(self, updated, control->id, control->val);

    return 0;
}

static const struct v4l2_ctrl_ops akvcam_controls_ops = {
    .s_ctrl = akvcam_controls_cotrol_changed,
};
