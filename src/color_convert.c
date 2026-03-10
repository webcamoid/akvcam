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

#include <linux/kref.h>
#include <linux/slab.h>

#include "color_convert.h"
#include "format_specs.h"

struct akvcam_color_convert_private
{
    struct kref ref;
    akvcam_color_convert_t parent;
    AKVCAM_YUV_COLOR_SPACE yuv_color_space;
    AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type;
};

akvcam_color_convert_private_t akvcam_color_convert_private_new(akvcam_color_convert_t parent);
void akvcam_color_convert_private_rb_constants(AKVCAM_YUV_COLOR_SPACE color_space,
                                               int64_t *kb,
                                               int64_t *kr,
                                               int64_t *div);
int64_t akvcam_color_convert_private_rounded_div(int64_t num, int64_t den);
int64_t akvcam_color_convert_private_nearest_pow_of_2(int64_t value);
void akvcam_color_convert_private_limits_y(int bits,
                                           AKVCAM_YUV_COLOR_SPACE_TYPE type,
                                           int64_t *min_y,
                                           int64_t *max_y);
void akvcam_color_convert_private_limits_uv(int bits,
                                            AKVCAM_YUV_COLOR_SPACE_TYPE type,
                                            int64_t *min_uv,
                                            int64_t *max_uv);
void akvcam_color_convert_private_load_abc_to_xyz_matrix(akvcam_color_convert_private_t self,
                                                         int abits,
                                                         int bbits,
                                                         int cbits,
                                                         int xbits,
                                                         int ybits,
                                                         int zbits);
void akvcam_color_convert_private_load_rgb_to_yuv_matrix(akvcam_color_convert_private_t self,
                                                         AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                                         AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                         int rbits,
                                                         int gbits,
                                                         int bbits,
                                                         int ybits,
                                                         int ubits,
                                                         int vbits);
void akvcam_color_convert_private_load_yuv_to_rgb_matrix(akvcam_color_convert_private_t self,
                                                         AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                                         AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                         int ybits,
                                                         int ubits,
                                                         int vbits,
                                                         int rbits,
                                                         int gbits,
                                                         int bbits);
void akvcam_color_convert_private_load_rgb_to_gray_matrix(akvcam_color_convert_private_t self,
                                                          AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                                          int rbits,
                                                          int gbits,
                                                          int bbits,
                                                          int gray_bits);
void akvcam_color_convert_private_load_gray_to_rgb_matrix(akvcam_color_convert_private_t self,
                                                          int gray_bits,
                                                          int rbits,
                                                          int gbits,
                                                          int bbits);
void akvcam_color_convert_private_load_yuv_to_gray_matrix(akvcam_color_convert_private_t self,
                                                          AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                          int ybits,
                                                          int gray_bits);
void akvcam_color_convert_private_load_gray_to_yuv_matrix(akvcam_color_convert_private_t self,
                                                          AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                          int gray_bits,
                                                          int ybits,
                                                          int ubits,
                                                          int vbits);
void akvcam_color_convert_private_load_alpha_rgb_matrix(akvcam_color_convert_private_t self,
                                                        int alpha_bits);
void akvcam_color_convert_private_load_alpha_yuv_matrix(akvcam_color_convert_private_t self,
                                                        AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                        int alpha_bits,
                                                        int ybits,
                                                        int ubits,
                                                        int vbits);
void akvcam_color_convert_private_load_alpha_gray_matrix(akvcam_color_convert_private_t self,
                                                         int alpha_bits,
                                                         int gray_bits);

akvcam_color_convert_t akvcam_color_convert_new(void)
{
    akvcam_color_convert_t self = kzalloc(sizeof(struct akvcam_color_convert), GFP_KERNEL);
    self->priv = akvcam_color_convert_private_new(self);
    kref_init(&self->priv->ref);

    return self;
}

akvcam_color_convert_t akvcam_color_convert_new_copy(akvcam_color_convert_ct other)
{
    akvcam_color_convert_t self = kzalloc(sizeof(struct akvcam_color_convert), GFP_KERNEL);
    self->priv = akvcam_color_convert_private_new(self);
    kref_init(&self->priv->ref);

    self->m00 = other->m00; self->m01 = other->m01; self->m02 = other->m02; self->m03 = other->m03;
    self->m10 = other->m10; self->m11 = other->m11; self->m12 = other->m12; self->m13 = other->m13;
    self->m20 = other->m20; self->m21 = other->m21; self->m22 = other->m22; self->m23 = other->m23;

    self->a00 = other->a00; self->a01 = other->a01; self->a02 = other->a02;
    self->a10 = other->a10; self->a11 = other->a11; self->a12 = other->a12;
    self->a20 = other->a20; self->a21 = other->a21; self->a22 = other->a22;

    self->xmin = other->xmin; self->xmax = other->xmax;
    self->ymin = other->ymin; self->ymax = other->ymax;
    self->zmin = other->zmin; self->zmax = other->zmax;

    self->color_shift = other->color_shift;
    self->alpha_shift = other->alpha_shift;

    self->priv->yuv_color_space = other->priv->yuv_color_space;
    self->priv->yuv_color_space_type = other->priv->yuv_color_space_type;

    return self;
}

static void akvcam_color_convert_free(struct kref *ref)
{
    akvcam_color_convert_private_t self = container_of(ref, struct akvcam_color_convert_private, ref);
    akvcam_color_convert_t parent = self->parent;
    kfree(self);
    kfree(parent);
}

void akvcam_color_convert_delete(akvcam_color_convert_t self)
{
    if (self)
        kref_put(&self->priv->ref, akvcam_color_convert_free);
}

akvcam_color_convert_t akvcam_color_convert_ref(akvcam_color_convert_t self)
{
    if (self)
        kref_get(&self->priv->ref);

    return self;
}

void akvcam_color_convert_copy(akvcam_color_convert_t self, akvcam_color_convert_ct other)
{
    if (!self)
        return;

    if (other) {
        self->m00 = other->m00; self->m01 = other->m01; self->m02 = other->m02; self->m03 = other->m03;
        self->m10 = other->m10; self->m11 = other->m11; self->m12 = other->m12; self->m13 = other->m13;
        self->m20 = other->m20; self->m21 = other->m21; self->m22 = other->m22; self->m23 = other->m23;

        self->a00 = other->a00; self->a01 = other->a01; self->a02 = other->a02;
        self->a10 = other->a10; self->a11 = other->a11; self->a12 = other->a12;
        self->a20 = other->a20; self->a21 = other->a21; self->a22 = other->a22;

        self->xmin = other->xmin; self->xmax = other->xmax;
        self->ymin = other->ymin; self->ymax = other->ymax;
        self->zmin = other->zmin; self->zmax = other->zmax;

        self->color_shift = other->color_shift;
        self->alpha_shift = other->alpha_shift;

        self->priv->yuv_color_space = other->priv->yuv_color_space;
        self->priv->yuv_color_space_type = other->priv->yuv_color_space_type;
    } else {
        self->m00 = 0; self->m01 = 0; self->m02 = 0; self->m03 = 0;
        self->m10 = 0; self->m11 = 0; self->m12 = 0; self->m13 = 0;
        self->m20 = 0; self->m21 = 0; self->m22 = 0; self->m23 = 0;

        self->a00 = 0; self->a01 = 0; self->a02 = 0;
        self->a10 = 0; self->a11 = 0; self->a12 = 0;
        self->a20 = 0; self->a21 = 0; self->a22 = 0;

        self->xmin = 0; self->xmax = 0;
        self->ymin = 0; self->ymax = 0;
        self->zmin = 0; self->zmax = 0;

        self->color_shift = 0;
        self->alpha_shift = 0;

        self->priv->yuv_color_space = AKVCAM_YUV_COLOR_SPACE_ITUR_BT601;
        self->priv->yuv_color_space_type = AKVCAM_YUV_COLOR_SPACE_TYPE_STUDIO_SWING;
    }
}

AKVCAM_YUV_COLOR_SPACE akvcam_color_convert_yuv_color_space(akvcam_color_convert_ct self)
{
    return self->priv->yuv_color_space;
}

void akvcam_color_convert_set_yuv_color_space(akvcam_color_convert_t self,
                                              AKVCAM_YUV_COLOR_SPACE yuv_color_space)
{
    self->priv->yuv_color_space = yuv_color_space;
}

AKVCAM_YUV_COLOR_SPACE_TYPE akvcam_color_convert_yuv_color_space_type(akvcam_color_convert_ct self)
{
    return self->priv->yuv_color_space_type;
}

void akvcam_color_convert_set_yuv_color_space_type(akvcam_color_convert_t self,
                                                   AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type)
{
    self->priv->yuv_color_space_type = yuv_color_space_type;
}

void akvcam_color_convert_load_color_matrix(akvcam_color_convert_t self,
                                            AKVCAM_COLOR_MATRIX color_matrix,
                                            int ibitsa,
                                            int ibitsb,
                                            int ibitsc,
                                            int obitsx,
                                            int obitsy,
                                            int obitsz)
{
    switch (color_matrix) {
    case AKVCAM_COLOR_MATRIX_ABC2XYZ:
        akvcam_color_convert_private_load_abc_to_xyz_matrix(self->priv,
                                                            ibitsa,
                                                            ibitsb,
                                                            ibitsc,
                                                            obitsx,
                                                            obitsy,
                                                            obitsz);

        break;

    case AKVCAM_COLOR_MATRIX_RGB2YUV:
        akvcam_color_convert_private_load_rgb_to_yuv_matrix(self->priv,
                                                            self->priv->yuv_color_space,
                                                            self->priv->yuv_color_space_type,
                                                            ibitsa,
                                                            ibitsb,
                                                            ibitsc,
                                                            obitsx,
                                                            obitsy,
                                                            obitsz);

        break;

    case AKVCAM_COLOR_MATRIX_YUV2RGB:
        akvcam_color_convert_private_load_yuv_to_rgb_matrix(self->priv,
                                                            self->priv->yuv_color_space,
                                                            self->priv->yuv_color_space_type,
                                                            ibitsa,
                                                            ibitsb,
                                                            ibitsc,
                                                            obitsx,
                                                            obitsy,
                                                            obitsz);

        break;

    case AKVCAM_COLOR_MATRIX_RGB2GRAY:
        akvcam_color_convert_private_load_rgb_to_gray_matrix(self->priv,
                                                             self->priv->yuv_color_space,
                                                             ibitsa,
                                                             ibitsb,
                                                             ibitsc,
                                                             obitsx);

        break;

    case AKVCAM_COLOR_MATRIX_GRAY2RGB:
        akvcam_color_convert_private_load_gray_to_rgb_matrix(self->priv,
                                                             ibitsa,
                                                             obitsx,
                                                             obitsy,
                                                             obitsz);

        break;

    case AKVCAM_COLOR_MATRIX_YUV2GRAY:
        akvcam_color_convert_private_load_yuv_to_gray_matrix(self->priv,
                                                             self->priv->yuv_color_space_type,
                                                             ibitsa,
                                                             obitsx);

        break;

    case AKVCAM_COLOR_MATRIX_GRAY2YUV:
        akvcam_color_convert_private_load_gray_to_yuv_matrix(self->priv,
                                                             self->priv->yuv_color_space_type,
                                                             ibitsa,
                                                             obitsx,
                                                             obitsy,
                                                             obitsz);
        break;

    default:
        break;
    }
}

void akvcam_color_convert_load_alpha_matrix(akvcam_color_convert_t self,
                                            AKVCAM_VIDEO_FORMAT_TYPE format_type,
                                            int ibits_alpha,
                                            int obitsx,
                                            int obitsy,
                                            int obitsz)
{
    switch (format_type) {
    case AKVCAM_VIDEO_FORMAT_TYPE_RGB:
        akvcam_color_convert_private_load_alpha_rgb_matrix(self->priv,
                                                           ibits_alpha);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_YUV:
        akvcam_color_convert_private_load_alpha_yuv_matrix(self->priv,
                                                           self->priv->yuv_color_space_type,
                                                           ibits_alpha,
                                                           obitsx,
                                                           obitsy,
                                                           obitsz);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_GRAY:
        akvcam_color_convert_private_load_alpha_gray_matrix(self->priv,
                                                            ibits_alpha,
                                                            obitsx);

        break;

    default:
        break;
    }
}
void akvcam_color_convert_load_matrix(akvcam_color_convert_t self,
                                      akvcam_format_specs_ct from,
                                      akvcam_format_specs_ct to)
{
    AKVCAM_COLOR_MATRIX color_matrix = AKVCAM_COLOR_MATRIX_ABC2XYZ;
    int ibitsa = 0;
    int ibitsb = 0;
    int ibitsc = 0;
    int obitsx = 0;
    int obitsy = 0;
    int obitsz = 0;

    akvcam_color_component_ct comp_xi = NULL;
    akvcam_color_component_ct comp_yi = NULL;
    akvcam_color_component_ct comp_zi = NULL;

    akvcam_color_component_ct comp_xo = NULL;
    akvcam_color_component_ct comp_yo = NULL;
    akvcam_color_component_ct comp_zo = NULL;

    if (!from || !to)
        return;

    if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB
        && to->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB) {
        color_matrix = AKVCAM_COLOR_MATRIX_ABC2XYZ;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_R);
        comp_yi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_G);
        comp_zi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_B);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = comp_yi? comp_yi->depth: 0;
        ibitsc = comp_zi? comp_zi->depth: 0;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_R);
        comp_yo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_G);
        comp_zo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_B);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = comp_yo? comp_yo->depth: 0;
        obitsz = comp_zo? comp_zo->depth: 0;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_YUV) {
        color_matrix = AKVCAM_COLOR_MATRIX_RGB2YUV;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_R);
        comp_yi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_G);
        comp_zi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_B);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = comp_yi? comp_yi->depth: 0;
        ibitsc = comp_zi? comp_zi->depth: 0;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_Y);
        comp_yo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_U);
        comp_zo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_V);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = comp_yo? comp_yo->depth: 0;
        obitsz = comp_zo? comp_zo->depth: 0;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_GRAY) {
        color_matrix = AKVCAM_COLOR_MATRIX_RGB2GRAY;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_R);
        comp_yi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_G);
        comp_zi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_B);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = comp_yi? comp_yi->depth: 0;
        ibitsc = comp_zi? comp_zi->depth: 0;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_Y);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = obitsx;
        obitsz = obitsx;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_YUV
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB) {
        color_matrix = AKVCAM_COLOR_MATRIX_YUV2RGB;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_Y);
        comp_yi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_U);
        comp_zi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_V);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = comp_yi? comp_yi->depth: 0;
        ibitsc = comp_zi? comp_zi->depth: 0;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_R);
        comp_yo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_G);
        comp_zo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_B);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = comp_yo? comp_yo->depth: 0;
        obitsz = comp_zo? comp_zo->depth: 0;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_YUV
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_YUV) {
        color_matrix = AKVCAM_COLOR_MATRIX_ABC2XYZ;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_Y);
        comp_yi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_U);
        comp_zi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_V);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = comp_yi? comp_yi->depth: 0;
        ibitsc = comp_zi? comp_zi->depth: 0;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_Y);
        comp_yo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_U);
        comp_zo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_V);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = comp_yo? comp_yo->depth: 0;
        obitsz = comp_zo? comp_zo->depth: 0;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_YUV
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_GRAY) {
        color_matrix = AKVCAM_COLOR_MATRIX_YUV2GRAY;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_Y);
        comp_yi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_U);
        comp_zi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_V);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = comp_yi? comp_yi->depth: 0;
        ibitsc = comp_zi? comp_zi->depth: 0;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_Y);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = obitsx;
        obitsz = obitsx;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_GRAY
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB) {
        color_matrix = AKVCAM_COLOR_MATRIX_GRAY2RGB;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_Y);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = ibitsa;
        ibitsc = ibitsa;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_R);
        comp_yo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_G);
        comp_zo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_B);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = comp_yo? comp_yo->depth: 0;
        obitsz = comp_zo? comp_zo->depth: 0;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_GRAY
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_YUV) {
        color_matrix = AKVCAM_COLOR_MATRIX_GRAY2YUV;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_Y);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = ibitsa;
        ibitsc = ibitsa;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_Y);
        comp_yo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_U);
        comp_zo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_V);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = comp_yo? comp_yo->depth: 0;
        obitsz = comp_zo? comp_zo->depth: 0;
    } else if (from->type == AKVCAM_VIDEO_FORMAT_TYPE_GRAY
               && to->type == AKVCAM_VIDEO_FORMAT_TYPE_GRAY) {
        color_matrix = AKVCAM_COLOR_MATRIX_ABC2XYZ;

        comp_xi = akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_Y);

        ibitsa = comp_xi? comp_xi->depth: 0;
        ibitsb = ibitsa;
        ibitsc = ibitsa;

        comp_xo = akvcam_format_specs_component(to, AKVCAM_COMPONENT_TYPE_Y);

        obitsx = comp_xo? comp_xo->depth: 0;
        obitsy = obitsx;
        obitsz = obitsx;
    }

    akvcam_color_convert_load_color_matrix(self,
                                           color_matrix,
                                           ibitsa,
                                           ibitsb,
                                           ibitsc,
                                           obitsx,
                                           obitsy,
                                           obitsz);

    if (akvcam_format_specs_contains(from, AKVCAM_COMPONENT_TYPE_A))
        akvcam_color_convert_load_alpha_matrix(self,
                                               to->type,
                                               akvcam_format_specs_component(from, AKVCAM_COMPONENT_TYPE_A)->depth,
                                               obitsx,
                                               obitsy,
                                               obitsz);
}

void akvcam_color_convert_load_matrix_from_fixel_formats(akvcam_color_convert_t self,
                                                         __u32 from,
                                                         __u32 to)
{
    akvcam_format_specs_ct spec_from =
            akvcam_format_specs_from_fixel_format(from);
    akvcam_format_specs_ct spec_to =
            akvcam_format_specs_from_fixel_format(to);
    akvcam_color_convert_load_matrix(self, spec_from, spec_to);
}

akvcam_color_convert_private_t akvcam_color_convert_private_new(akvcam_color_convert_t parent)
{
    akvcam_color_convert_private_t self =
        kzalloc(sizeof(struct akvcam_color_convert_private), GFP_KERNEL);
    self->parent = parent;
    self->yuv_color_space = AKVCAM_YUV_COLOR_SPACE_ITUR_BT601;
    self->yuv_color_space_type = AKVCAM_YUV_COLOR_SPACE_TYPE_STUDIO_SWING;

    return self;
}

void akvcam_color_convert_private_rb_constants(AKVCAM_YUV_COLOR_SPACE color_space,
                                               int64_t *kr,
                                               int64_t *kb,
                                               int64_t *div)
{
    *kr = 0;
    *kb = 0;
    *div = 10000;

    // Coefficients taken from https://en.wikipedia.org/wiki/YUV
    switch (color_space) {
    // Same weight for all components
    case AKVCAM_YUV_COLOR_SPACE_AVG:
        *kr = 3333;
        *kb = 3333;

        break;

    // https://www.itu.int/rec/R-REC-BT.601/en
    case AKVCAM_YUV_COLOR_SPACE_ITUR_BT601:
        *kr = 2990;
        *kb = 1140;

        break;

    // https://www.itu.int/rec/R-REC-BT.709/en
    case AKVCAM_YUV_COLOR_SPACE_ITUR_BT709:
        *kr = 2126;
        *kb = 722;

        break;

    // https://www.itu.int/rec/R-REC-BT.2020/en
    case AKVCAM_YUV_COLOR_SPACE_ITUR_BT2020:
        *kr = 2627;
        *kb = 593;

        break;

    // http://car.france3.mars.free.fr/HD/INA-%2026%20jan%2006/SMPTE%20normes%20et%20confs/s240m.pdf
    case AKVCAM_YUV_COLOR_SPACE_SMPTE_240M:
        *kr = 2120;
        *kb = 870;

        break;

    default:
        break;
    }
}

int64_t akvcam_color_convert_private_rounded_div(int64_t num, int64_t den)
{
    if (den == 0)
        return num < 0? LLONG_MIN: LLONG_MAX;

    if (((num < 0) && (den > 0)) || ((num > 0) && (den < 0)))
        return (2 * num - den) / (2 * den);

    return (2 * num + den) / (2 * den);
}

int64_t akvcam_color_convert_private_nearest_pow_of_2(int64_t value)
{
    int64_t val = value;
    int64_t res = 0;

    while (val >>= 1)
        res++;

    if (akvcam_abs((1 << (res + 1)) - value) <= akvcam_abs((1 << res) - value))
        return 1 << (res + 1);

    return 1 << res;
}

void akvcam_color_convert_private_limits_y(int bits,
                                           AKVCAM_YUV_COLOR_SPACE_TYPE type,
                                           int64_t *min_y,
                                           int64_t *max_y)
{
    /* g = 9% is the theoretical maximal overshoot (Gibbs phenomenon)
     *
     * https://en.wikipedia.org/wiki/YUV#Numerical_approximations
     * https://en.wikipedia.org/wiki/Gibbs_phenomenon
     * https://math.stackexchange.com/a/259089
     * https://www.youtube.com/watch?v=Ol0uTeXoKaU
     */
    static const int64_t g = 9;
    int64_t max_value;
    int64_t div;

    if (type == AKVCAM_YUV_COLOR_SPACE_TYPE_FULL_SWING) {
        *min_y = 0;
        *max_y = (1 << bits) - 1;

        return;
    }

    max_value = (1 << bits) - 1;
    div = akvcam_color_convert_private_rounded_div(max_value * g, 2 * g + 100);
    *min_y = akvcam_color_convert_private_nearest_pow_of_2(div);
    *max_y = max_value * (g + 100) / (2 * g + 100);
}

void akvcam_color_convert_private_limits_uv(int bits,
                                            AKVCAM_YUV_COLOR_SPACE_TYPE type,
                                            int64_t *min_uv,
                                            int64_t *max_uv)
{
    static const int64_t g = 9;
    int64_t max_value;
    int64_t div;

    if (type == AKVCAM_YUV_COLOR_SPACE_TYPE_FULL_SWING) {
        *min_uv = 0;
        *max_uv = (1 << bits) - 1;

        return;
    }

    max_value = (1 << bits) - 1;
    div = akvcam_color_convert_private_rounded_div(max_value * g, 2 * g + 100);
    *min_uv = akvcam_color_convert_private_nearest_pow_of_2(div);
    *max_uv = (1L << bits) - *min_uv;
}

void akvcam_color_convert_private_load_abc_to_xyz_matrix(akvcam_color_convert_private_t self,
                                                         int abits,
                                                         int bbits,
                                                         int cbits,
                                                         int xbits,
                                                         int ybits,
                                                         int zbits)
{
    int shift = akvcam_max(abits, akvcam_max(bbits, cbits));
    int64_t shift_div = 1LL << shift;
    int64_t rounding = 1LL << akvcam_abs(shift - 1);

    int64_t amax = (1LL << abits) - 1;
    int64_t bmax = (1LL << bbits) - 1;
    int64_t cmax = (1LL << cbits) - 1;

    int64_t xmax = (1LL << xbits) - 1;
    int64_t ymax = (1LL << ybits) - 1;
    int64_t zmax = (1LL << zbits) - 1;

    int64_t kx = akvcam_color_convert_private_rounded_div(shift_div * xmax, amax);
    int64_t ky = akvcam_color_convert_private_rounded_div(shift_div * ymax, bmax);
    int64_t kz = akvcam_color_convert_private_rounded_div(shift_div * zmax, cmax);

    self->parent->m00 = kx; self->parent->m01 = 0 ; self->parent->m02 = 0 ; self->parent->m03 = rounding;
    self->parent->m10 = 0 ; self->parent->m11 = ky; self->parent->m12 = 0 ; self->parent->m13 = rounding;
    self->parent->m20 = 0 ; self->parent->m21 = 0 ; self->parent->m22 = kz; self->parent->m23 = rounding;

    self->parent->xmin = 0; self->parent->xmax = xmax;
    self->parent->ymin = 0; self->parent->ymax = ymax;
    self->parent->zmin = 0; self->parent->zmax = zmax;

    self->parent->color_shift = shift;
}

void akvcam_color_convert_private_load_rgb_to_yuv_matrix(akvcam_color_convert_private_t self,
                                                         AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                                         AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                         int rbits,
                                                         int gbits,
                                                         int bbits,
                                                         int ybits,
                                                         int ubits,
                                                         int vbits)
{
    int64_t kyr;
    int64_t kyb;
    int64_t div;
    int64_t kyg;
    int64_t kur;
    int64_t kug;
    int64_t kub;
    int64_t kvr;
    int64_t kvg;
    int64_t kvb;
    int shift;
    int64_t shift_div;
    int64_t rounding;
    int64_t rmax;
    int64_t gmax;
    int64_t bmax;
    int64_t min_y;
    int64_t max_y;
    int64_t diff_y;
    int64_t kiyr;
    int64_t kiyg;
    int64_t kiyb;
    int64_t min_u;
    int64_t max_u;
    int64_t diff_u;
    int64_t kiur;
    int64_t kiug;
    int64_t kiub;
    int64_t min_v;
    int64_t max_v;
    int64_t diff_v;
    int64_t kivr;
    int64_t kivg;
    int64_t kivb;
    int64_t ciy;
    int64_t ciu;
    int64_t civ;

    kyr = 0;
    kyb = 0;
    div = 0;
    akvcam_color_convert_private_rb_constants(yuv_color_space, &kyr, &kyb, &div);
    kyg = div - kyr - kyb;

    kur = -kyr;
    kug = -kyg;
    kub = div - kyb;

    kvr = div - kyr;
    kvg = -kyg;
    kvb = -kyb;

    shift = akvcam_max(rbits, akvcam_max(gbits, bbits));
    shift_div = 1LL << shift;
    rounding = 1LL << (shift - 1);

    rmax = (1LL << rbits) - 1;
    gmax = (1LL << gbits) - 1;
    bmax = (1LL << bbits) - 1;

    min_y = 0;
    max_y = 0;
    akvcam_color_convert_private_limits_y(ybits, yuv_color_space_type, &min_y, &max_y);
    diff_y = max_y - min_y;

    kiyr = akvcam_color_convert_private_rounded_div(shift_div * diff_y * kyr, div * rmax);
    kiyg = akvcam_color_convert_private_rounded_div(shift_div * diff_y * kyg, div * gmax);
    kiyb = akvcam_color_convert_private_rounded_div(shift_div * diff_y * kyb, div * bmax);

    min_u = 0;
    max_u = 0;
    akvcam_color_convert_private_limits_uv(ubits, yuv_color_space_type, &min_u, &max_u);
    diff_u = max_u - min_u;

    kiur = akvcam_color_convert_private_rounded_div(shift_div * diff_u * kur, 2 * rmax * kub);
    kiug = akvcam_color_convert_private_rounded_div(shift_div * diff_u * kug, 2 * gmax * kub);
    kiub = akvcam_color_convert_private_rounded_div(shift_div * diff_u      , 2 * bmax);

    min_v = 0;
    max_v = 0;
    akvcam_color_convert_private_limits_uv(vbits, yuv_color_space_type, &min_v, &max_v);
    diff_v = max_v - min_v;

    kivr = akvcam_color_convert_private_rounded_div(shift_div * diff_v      , 2 * rmax);
    kivg = akvcam_color_convert_private_rounded_div(shift_div * diff_v * kvg, 2 * gmax * kvr);
    kivb = akvcam_color_convert_private_rounded_div(shift_div * diff_v * kvb, 2 * bmax * kvr);

    ciy = rounding + shift_div * min_y;
    ciu = rounding + shift_div * (min_u + max_u) / 2;
    civ = rounding + shift_div * (min_v + max_v) / 2;

    self->parent->m00 = kiyr; self->parent->m01 = kiyg; self->parent->m02 = kiyb; self->parent->m03 = ciy;
    self->parent->m10 = kiur; self->parent->m11 = kiug; self->parent->m12 = kiub; self->parent->m13 = ciu;
    self->parent->m20 = kivr; self->parent->m21 = kivg; self->parent->m22 = kivb; self->parent->m23 = civ;

    self->parent->xmin = min_y; self->parent->xmax = max_y;
    self->parent->ymin = min_u; self->parent->ymax = max_u;
    self->parent->zmin = min_v; self->parent->zmax = max_v;

    self->parent->color_shift = shift;
}

void akvcam_color_convert_private_load_yuv_to_rgb_matrix(akvcam_color_convert_private_t self,
                                                         AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                                         AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                         int ybits,
                                                         int ubits,
                                                         int vbits,
                                                         int rbits,
                                                         int gbits,
                                                         int bbits)
{
    int64_t kyr;
    int64_t kyb;
    int64_t div;
    int64_t kyg;
    int64_t min_y;
    int64_t max_y;
    int64_t diff_y;
    int64_t min_u;
    int64_t max_u;
    int64_t diff_u;
    int64_t minV;
    int64_t maxV;
    int64_t diff_v;
    int shift;
    int64_t shift_div;
    int64_t rounding;
    int64_t rmax;
    int64_t gmax;
    int64_t bmax;
    int64_t kry;
    int64_t krv;
    int64_t kgy;
    int64_t kgu;
    int64_t kgv;
    int64_t kby;
    int64_t kbu;
    int64_t cir;
    int64_t cig;
    int64_t cib;

    kyr = 0;
    kyb = 0;
    div = 0;
    akvcam_color_convert_private_rb_constants(yuv_color_space, &kyr, &kyb, &div);
    kyg = div - kyr - kyb;

    min_y = 0;
    max_y = 0;
    akvcam_color_convert_private_limits_y(ybits, yuv_color_space_type, &min_y, &max_y);
    diff_y = max_y - min_y;

    min_u = 0;
    max_u = 0;
    akvcam_color_convert_private_limits_uv(ubits, yuv_color_space_type, &min_u, &max_u);
    diff_u = max_u - min_u;

    minV = 0;
    maxV = 0;
    akvcam_color_convert_private_limits_uv(vbits, yuv_color_space_type, &minV, &maxV);
    diff_v = maxV - minV;

    shift = akvcam_max(ybits, akvcam_max(ubits, vbits));
    shift_div = 1LL << shift;
    rounding = 1LL << (shift - 1);

    rmax = (1LL << rbits) - 1;
    gmax = (1LL << gbits) - 1;
    bmax = (1LL << bbits) - 1;

    kry = akvcam_color_convert_private_rounded_div(shift_div * rmax, diff_y);
    krv = akvcam_color_convert_private_rounded_div(2 * shift_div * rmax * (div - kyr), div * diff_v);

    kgy = akvcam_color_convert_private_rounded_div(shift_div * gmax, diff_y);
    kgu = akvcam_color_convert_private_rounded_div(2 * shift_div * gmax * kyb * (kyb - div), div * kyg * diff_u);
    kgv = akvcam_color_convert_private_rounded_div(2 * shift_div * gmax * kyr * (kyr - div), div * kyg * diff_v);

    kby = akvcam_color_convert_private_rounded_div(shift_div * bmax, diff_y);
    kbu = akvcam_color_convert_private_rounded_div(2 * shift_div * bmax * (div - kyb), div * diff_u);

    cir = rounding - kry * min_y - krv * (minV + maxV) / 2;
    cig = rounding - kgy * min_y - (kgu * (min_u + max_u) + kgv * (minV + maxV)) / 2;
    cib = rounding - kby * min_y - kbu * (min_u + max_u) / 2;

    self->parent->m00 = kry; self->parent->m01 = 0  ; self->parent->m02 = krv; self->parent->m03 = cir;
    self->parent->m10 = kgy; self->parent->m11 = kgu; self->parent->m12 = kgv; self->parent->m13 = cig;
    self->parent->m20 = kby; self->parent->m21 = kbu; self->parent->m22 = 0  ; self->parent->m23 = cib;

    self->parent->xmin = 0; self->parent->xmax = rmax;
    self->parent->ymin = 0; self->parent->ymax = gmax;
    self->parent->zmin = 0; self->parent->zmax = bmax;

    self->parent->color_shift = shift;
}

void akvcam_color_convert_private_load_rgb_to_gray_matrix(akvcam_color_convert_private_t self,
                                                          AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                                          int rbits,
                                                          int gbits,
                                                          int bbits,
                                                          int gray_bits)
{
    AKVCAM_YUV_COLOR_SPACE_TYPE type = AKVCAM_YUV_COLOR_SPACE_TYPE_FULL_SWING;
    int64_t kyr;
    int64_t kyb;
    int64_t div;
    int64_t kyg;
    int shift;
    int64_t shift_div;
    int64_t rounding;
    int64_t rmax;
    int64_t gmax;
    int64_t bmax;
    int64_t min_y;
    int64_t max_y;
    int64_t diff_y;
    int64_t kiyr;
    int64_t kiyg;
    int64_t kiyb;
    int64_t min_u;
    int64_t max_u;
    int64_t min_v;
    int64_t max_v;
    int64_t ciy;
    int64_t ciu;
    int64_t civ;

    kyr = 0;
    kyb = 0;
    div = 0;
    akvcam_color_convert_private_rb_constants(yuv_color_space, &kyr, &kyb, &div);
    kyg = div - kyr - kyb;

    shift = akvcam_max(rbits, akvcam_max(gbits, bbits));
    shift_div = 1LL << shift;
    rounding = 1LL << (shift - 1);

    rmax = (1LL << rbits) - 1;
    gmax = (1LL << gbits) - 1;
    bmax = (1LL << bbits) - 1;

    min_y = 0;
    max_y = 0;
    akvcam_color_convert_private_limits_y(gray_bits, type, &min_y, &max_y);
    diff_y = max_y - min_y;

    kiyr = akvcam_color_convert_private_rounded_div(shift_div * diff_y * kyr, div * rmax);
    kiyg = akvcam_color_convert_private_rounded_div(shift_div * diff_y * kyg, div * gmax);
    kiyb = akvcam_color_convert_private_rounded_div(shift_div * diff_y * kyb, div * bmax);

    min_u = 0;
    max_u = 0;
    akvcam_color_convert_private_limits_uv(gray_bits, type, &min_u, &max_u);

    min_v = 0;
    max_v = 0;
    akvcam_color_convert_private_limits_uv(gray_bits, type, &min_v, &max_v);

    ciy = rounding + shift_div * min_y;
    ciu = rounding + shift_div * (min_u + max_u) / 2;
    civ = rounding + shift_div * (min_v + max_v) / 2;

    self->parent->m00 = kiyr; self->parent->m01 = kiyg; self->parent->m02 = kiyb; self->parent->m03 = ciy;
    self->parent->m10 = 0   ; self->parent->m11 = 0   ; self->parent->m12 = 0   ; self->parent->m13 = ciu;
    self->parent->m20 = 0   ; self->parent->m21 = 0   ; self->parent->m22 = 0   ; self->parent->m23 = civ;

    self->parent->xmin = min_y; self->parent->xmax = max_y;
    self->parent->ymin = min_u; self->parent->ymax = max_u;
    self->parent->zmin = min_v; self->parent->zmax = max_v;

    self->parent->color_shift = shift;
}

void akvcam_color_convert_private_load_gray_to_rgb_matrix(akvcam_color_convert_private_t self,
                                                          int gray_bits,
                                                          int rbits,
                                                          int gbits,
                                                          int bbits)
{
    int shift;
    int64_t shift_div;
    int64_t rounding;
    int64_t graymax;
    int64_t rmax;
    int64_t gmax;
    int64_t bmax;
    int64_t kr;
    int64_t kg;
    int64_t kb;

    shift = gray_bits;
    shift_div = 1LL << shift;
    rounding = 1LL << (shift - 1);
    graymax = (1LL << gray_bits) - 1;
    rmax = (1LL << rbits) - 1;
    gmax = (1LL << gbits) - 1;
    bmax = (1LL << bbits) - 1;
    kr = akvcam_color_convert_private_rounded_div(shift_div * rmax, graymax);
    kg = akvcam_color_convert_private_rounded_div(shift_div * gmax, graymax);
    kb = akvcam_color_convert_private_rounded_div(shift_div * bmax, graymax);

    self->parent->m00 = kr; self->parent->m01 = 0; self->parent->m02 = 0; self->parent->m03 = rounding;
    self->parent->m10 = kg; self->parent->m11 = 0; self->parent->m12 = 0; self->parent->m13 = rounding;
    self->parent->m20 = kb; self->parent->m21 = 0; self->parent->m22 = 0; self->parent->m23 = rounding;
    self->parent->xmin = 0; self->parent->xmax = rmax;
    self->parent->ymin = 0; self->parent->ymax = gmax;
    self->parent->zmin = 0; self->parent->zmax = bmax;
    self->parent->color_shift = shift;
}

void akvcam_color_convert_private_load_yuv_to_gray_matrix(akvcam_color_convert_private_t self,
                                                          AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                          int ybits,
                                                          int gray_bits)
{
    AKVCAM_YUV_COLOR_SPACE_TYPE otype = AKVCAM_YUV_COLOR_SPACE_TYPE_FULL_SWING;
    int shift;
    int64_t shift_div;
    int64_t rounding;
    int64_t graymax;
    int64_t min_y;
    int64_t max_y;
    int64_t diff_y;
    int64_t ky;
    int64_t min_u;
    int64_t max_u;
    int64_t min_v;
    int64_t max_v;
    int64_t ciy;
    int64_t ciu;
    int64_t civ;

    shift = ybits;
    shift_div = 1LL << shift;
    rounding = 1LL << (shift - 1);
    graymax = (1LL << gray_bits) - 1;
    min_y = 0;
    max_y = 0;
    akvcam_color_convert_private_limits_y(ybits, yuv_color_space_type, &min_y, &max_y);
    diff_y = max_y - min_y;

    ky = akvcam_color_convert_private_rounded_div(shift_div * graymax, diff_y);

    min_u = 0;
    max_u = 0;
    akvcam_color_convert_private_limits_uv(gray_bits, otype, &min_u, &max_u);

    min_v = 0;
    max_v = 0;
    akvcam_color_convert_private_limits_uv(gray_bits, otype, &min_v, &max_v);

    ciy = rounding - akvcam_color_convert_private_rounded_div(shift_div * min_y * graymax, diff_y);
    ciu = rounding + shift_div * (min_u + max_u) / 2;
    civ = rounding + shift_div * (min_v + max_v) / 2;

    self->parent->m00 = ky; self->parent->m01 = 0; self->parent->m02 = 0; self->parent->m03 = ciy;
    self->parent->m10 = 0 ; self->parent->m11 = 0; self->parent->m12 = 0; self->parent->m13 = ciu;
    self->parent->m20 = 0 ; self->parent->m21 = 0; self->parent->m22 = 0; self->parent->m23 = civ;
    self->parent->xmin = 0; self->parent->xmax = graymax;
    self->parent->ymin = 0; self->parent->ymax = graymax;
    self->parent->zmin = 0; self->parent->zmax = graymax;
    self->parent->color_shift = shift;
}

void akvcam_color_convert_private_load_gray_to_yuv_matrix(akvcam_color_convert_private_t self,
                                                          AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                          int gray_bits,
                                                          int ybits,
                                                          int ubits,
                                                          int vbits)
{
    int shift;
    int64_t shift_div;
    int64_t rounding;
    int64_t graymax;
    int64_t min_y;
    int64_t max_y;
    int64_t diff_y;
    int64_t ky;
    int64_t min_u;
    int64_t max_u;
    int64_t min_v;
    int64_t max_v;
    int64_t ciy;
    int64_t ciu;
    int64_t civ;

    shift = gray_bits;
    shift_div = 1LL << shift;
    rounding = 1LL << (shift - 1);
    graymax = (1LL << gray_bits) - 1;

    min_y = 0;
    max_y = 0;
    akvcam_color_convert_private_limits_y(ybits, yuv_color_space_type, &min_y, &max_y);
    diff_y = max_y - min_y;
    ky = akvcam_color_convert_private_rounded_div(shift_div * diff_y, graymax);

    min_u = 0;
    max_u = 0;
    akvcam_color_convert_private_limits_uv(ubits, yuv_color_space_type, &min_u, &max_u);

    min_v = 0;
    max_v = 0;
    akvcam_color_convert_private_limits_uv(vbits, yuv_color_space_type, &min_v, &max_v);

    ciy = rounding + shift_div * min_y;
    ciu = rounding + shift_div * (min_u + max_u) / 2;
    civ = rounding + shift_div * (min_v + max_v) / 2;

    self->parent->m00 = ky; self->parent->m01 = 0; self->parent->m02 = 0; self->parent->m03 = ciy;
    self->parent->m10 = 0 ; self->parent->m11 = 0; self->parent->m12 = 0; self->parent->m13 = ciu;
    self->parent->m20 = 0 ; self->parent->m21 = 0; self->parent->m22 = 0; self->parent->m23 = civ;
    self->parent->xmin = min_y; self->parent->xmax = max_y;
    self->parent->ymin = min_u; self->parent->ymax = max_u;
    self->parent->zmin = min_v; self->parent->zmax = max_v;
    self->parent->color_shift = shift;
}

void akvcam_color_convert_private_load_alpha_rgb_matrix(akvcam_color_convert_private_t self,
                                                        int alpha_bits)
{
    int64_t amax;
    int64_t shift_div;
    int64_t rounding;
    int64_t k;

    amax = (1LL << alpha_bits) - 1;
    self->parent->alpha_shift = alpha_bits;
    shift_div = 1LL << self->parent->alpha_shift;
    rounding = 1LL << (self->parent->alpha_shift - 1);
    k = akvcam_color_convert_private_rounded_div(shift_div, amax);

    self->parent->a00 = k; self->parent->a01 = 0; self->parent->a02 = rounding;
    self->parent->a10 = k; self->parent->a11 = 0; self->parent->a12 = rounding;
    self->parent->a20 = k; self->parent->a21 = 0; self->parent->a22 = rounding;
}

void akvcam_color_convert_private_load_alpha_yuv_matrix(akvcam_color_convert_private_t self,
                                                        AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type,
                                                        int alpha_bits,
                                                        int ybits,
                                                        int ubits,
                                                        int vbits)
{
    int64_t amax;
    int64_t shift_div;
    int64_t rounding;
    uint64_t k;
    int64_t min_y;
    int64_t max_y;
    int64_t ky;
    int64_t min_u;
    int64_t max_u;
    int64_t ku;
    int64_t min_v;
    int64_t max_v;
    int64_t kv;
    int64_t ciy;
    int64_t ciu;
    int64_t civ;

    amax = (1LL << alpha_bits) - 1;
    self->parent->alpha_shift = alpha_bits;
    shift_div = 1LL << self->parent->alpha_shift;
    rounding = 1LL << (self->parent->alpha_shift - 1);
    k = shift_div / amax;

    min_y = 0;
    max_y = 0;
    akvcam_color_convert_private_limits_y(ybits, yuv_color_space_type, &min_y, &max_y);
    ky = -akvcam_color_convert_private_rounded_div(shift_div * min_y, amax);

    min_u = 0;
    max_u = 0;
    akvcam_color_convert_private_limits_uv(ubits, yuv_color_space_type, &min_u, &max_u);
    ku = -akvcam_color_convert_private_rounded_div(shift_div * (min_u + max_u), 2 * amax);

    min_v = 0;
    max_v = 0;
    akvcam_color_convert_private_limits_uv(vbits, yuv_color_space_type, &min_v, &max_v);
    kv = -akvcam_color_convert_private_rounded_div(shift_div * (min_v + max_v), 2 * amax);

    ciy = rounding + shift_div * min_y;
    ciu = rounding + shift_div * (min_u + max_u) / 2;
    civ = rounding + shift_div * (min_v + max_v) / 2;

    self->parent->a00 = k; self->parent->a01 = ky; self->parent->a02 = ciy;
    self->parent->a10 = k; self->parent->a11 = ku; self->parent->a12 = ciu;
    self->parent->a20 = k; self->parent->a21 = kv; self->parent->a22 = civ;
}

void akvcam_color_convert_private_load_alpha_gray_matrix(akvcam_color_convert_private_t self,
                                                         int alpha_bits,
                                                         int gray_bits)
{
    AKVCAM_YUV_COLOR_SPACE_TYPE otype = AKVCAM_YUV_COLOR_SPACE_TYPE_FULL_SWING;
    int64_t amax;
    int64_t shift_div;
    int64_t rounding;
    int64_t k;
    int64_t min_u;
    int64_t max_u;
    int64_t min_v;
    int64_t max_v;
    int64_t ciu;
    int64_t civ;

    amax = (1LL << alpha_bits) - 1;
    self->parent->alpha_shift = alpha_bits;
    shift_div = 1LL << self->parent->alpha_shift;
    rounding = 1LL << (self->parent->alpha_shift - 1);
    k = akvcam_color_convert_private_rounded_div(shift_div, amax);

    min_u = 0;
    max_u = 0;
    akvcam_color_convert_private_limits_uv(gray_bits, otype, &min_u, &max_u);

    min_v = 0;
    max_v = 0;
    akvcam_color_convert_private_limits_uv(gray_bits, otype, &min_v, &max_v);

    ciu = rounding + shift_div * (min_u + max_u) / 2;
    civ = rounding + shift_div * (min_v + max_v) / 2;

    self->parent->a00 = k; self->parent->a01 = 0; self->parent->a02 = rounding;
    self->parent->a10 = 0; self->parent->a11 = 0; self->parent->a12 = ciu;
    self->parent->a20 = 0; self->parent->a21 = 0; self->parent->a22 = civ;
}
