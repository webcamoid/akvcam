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
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "frame.h"
#include "color_convert.h"
#include "file_read.h"
#include "format.h"
#include "format_specs.h"
#include "log.h"
#include "utils.h"

#define MAX_PLANES 4

typedef enum
{
    AKVCAM_FILL_TYPE_VECTOR,
    AKVCAM_FILL_TYPE_1,
    AKVCAM_FILL_TYPE_3,
} AKVCAM_FILL_TYPE;

typedef enum
{
    AKVCAM_FILL_DATA_TYPES_8,
    AKVCAM_FILL_DATA_TYPES_16,
    AKVCAM_FILL_DATA_TYPES_32,
    AKVCAM_FILL_DATA_TYPES_64,
} AKVCAM_FILL_DATA_TYPES;

typedef enum
{
    AKVCAM_ALPHA_MODE_AO,
    AKVCAM_ALPHA_MODE_O,
} AKVCAM_ALPHA_MODE;

typedef struct
{
    struct kref ref;

    akvcam_color_convert_t color_convert;
    AKVCAM_FILL_TYPE fill_type;
    AKVCAM_FILL_DATA_TYPES fill_data_types;
    AKVCAM_ALPHA_MODE alpha_mode;

    int endianess;

    int width;
    int height;
    size_t read_width;

    int *dst_width_offset_x;
    int *dst_width_offset_y;
    int *dst_width_offset_z;
    int *dst_width_offset_a;

    int plane_xo;
    int plane_yo;
    int plane_zo;
    int plane_ao;

    akvcam_color_component_ct comp_xo;
    akvcam_color_component_ct comp_yo;
    akvcam_color_component_ct comp_zo;
    akvcam_color_component_ct comp_ao;

    size_t xo_offset;
    size_t yo_offset;
    size_t zo_offset;
    size_t ao_offset;

    size_t xo_shift;
    size_t yo_shift;
    size_t zo_shift;
    size_t ao_shift;

    uint64_t mask_xo;
    uint64_t mask_yo;
    uint64_t mask_zo;
    uint64_t mask_ao;
} akvcam_fill_parameters;

typedef akvcam_fill_parameters *akvcam_fill_parameters_t;
typedef const akvcam_fill_parameters *akvcam_fill_parameters_ct;

typedef struct
{
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offBits;
} akvcam_bmp_header, *akvcam_bmp_header_t;

typedef struct
{
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t compression;
    uint32_t sizeImage;
    uint32_t xPelsPerMeter;
    uint32_t yPelsPerMeter;
    uint32_t clrUsed;
    uint32_t clrImportant;
} akvcam_bmp_image_header, *akvcam_bmp_image_header_t;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} akvcam_palette_pixel, *akvcam_palette_pixel_t;

typedef const akvcam_palette_pixel *akvcam_palette_pixel_ct;

struct akvcam_frame
{
    struct kref ref;
    akvcam_format_t format;
    uint8_t *data;
    uint8_t *planes[MAX_PLANES];
    akvcam_fill_parameters_t fc;
};

/* Fill functions */

#define AKVCAM_FILL3(data_type) \
    static inline void akvcam_frame_private_fill3_##data_type(akvcam_frame_t self, \
                                                              akvcam_fill_parameters_ct fc, \
                                                              uint32_t color) \
    { \
        size_t x; \
        \
        int xi = (int)((color >> 16) & 0xff); \
        int yi = (int)((color >>  8) & 0xff); \
        int zi = (int)((color >>  0) & 0xff); \
        int ai = (int)((color >> 24) & 0xff); \
        \
        uint8_t *line_x; \
        uint8_t *line_y; \
        uint8_t *line_z; \
        \
        int64_t xo_ = 0; \
        int64_t yo_ = 0; \
        int64_t zo_ = 0; \
        akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo_, &yo_, &zo_); \
        akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo_, &yo_, &zo_); \
        \
        line_x = self->planes[fc->plane_xo] + fc->xo_offset; \
        line_y = self->planes[fc->plane_yo] + fc->yo_offset; \
        line_z = self->planes[fc->plane_zo] + fc->zo_offset; \
        \
        for (x = 0; x < fc->read_width; ++x) { \
            int xd_x = fc->dst_width_offset_x[x]; \
            int xd_y = fc->dst_width_offset_y[x]; \
            int xd_z = fc->dst_width_offset_z[x]; \
            \
            data_type *xo = (data_type *)(line_x + xd_x); \
            data_type *yo = (data_type *)(line_y + xd_y); \
            data_type *zo = (data_type *)(line_z + xd_z); \
            \
            *xo = (*xo & (data_type)(fc->mask_xo)) | ((data_type)(xo_) << fc->xo_shift); \
            *yo = (*yo & (data_type)(fc->mask_yo)) | ((data_type)(yo_) << fc->yo_shift); \
            *zo = (*zo & (data_type)(fc->mask_zo)) | ((data_type)(zo_) << fc->zo_shift); \
        } \
    }

#define AKVCAM_FILL3A(data_type) \
    static inline void akvcam_frame_private_fill3a_##data_type(akvcam_frame_t self, \
                                                               akvcam_fill_parameters_ct fc, \
                                                               uint32_t color) \
    { \
        size_t x; \
        \
        int xi = (int)((color >> 16) & 0xff); \
        int yi = (int)((color >>  8) & 0xff); \
        int zi = (int)((color >>  0) & 0xff); \
        int ai = (int)((color >> 24) & 0xff); \
        \
        uint8_t *line_x; \
        uint8_t *line_y; \
        uint8_t *line_z; \
        uint8_t *line_a; \
        \
        int64_t xo_ = 0; \
        int64_t yo_ = 0; \
        int64_t zo_ = 0; \
        akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo_, &yo_, &zo_); \
        \
        line_x = self->planes[fc->plane_xo] + fc->xo_offset; \
        line_y = self->planes[fc->plane_yo] + fc->yo_offset; \
        line_z = self->planes[fc->plane_zo] + fc->zo_offset; \
        line_a = self->planes[fc->plane_ao] + fc->ao_offset; \
        \
        for (x = 0; x < fc->read_width; ++x) { \
            int xd_x = fc->dst_width_offset_x[x]; \
            int xd_y = fc->dst_width_offset_y[x]; \
            int xd_z = fc->dst_width_offset_z[x]; \
            int xd_a = fc->dst_width_offset_a[x]; \
            \
            data_type *xo = (data_type *)(line_x + xd_x); \
            data_type *yo = (data_type *)(line_y + xd_y); \
            data_type *zo = (data_type *)(line_z + xd_z); \
            data_type *ao = (data_type *)(line_a + xd_a); \
            \
            *xo = (*xo & (data_type)(fc->mask_xo)) | ((data_type)(xo_) << fc->xo_shift); \
            *yo = (*yo & (data_type)(fc->mask_yo)) | ((data_type)(yo_) << fc->yo_shift); \
            *zo = (*zo & (data_type)(fc->mask_zo)) | ((data_type)(zo_) << fc->zo_shift); \
            *ao = (*ao & (data_type)(fc->mask_ao)) | ((data_type)(ai)  << fc->ao_shift); \
        } \
    }

#define AKVCAM_FILL1(data_type) \
    static inline void akvcam_frame_private_fill1_##data_type(akvcam_frame_t self, \
                                                              akvcam_fill_parameters_ct fc, \
                                                              uint32_t color) \
    { \
        size_t x; \
        \
        int xi = (int)((color >> 16) & 0xff); \
        int yi = (int)((color >>  8) & 0xff); \
        int zi = (int)((color >>  0) & 0xff); \
        int ai = (int)((color >> 24) & 0xff); \
        \
        uint8_t *line_x; \
        \
        int64_t xo_ = 0; \
        akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo_); \
        akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo_); \
        \
        line_x = self->planes[fc->plane_xo] + fc->xo_offset; \
        \
        for (x = 0; x < fc->read_width; ++x) { \
            int xd_x = fc->dst_width_offset_x[x]; \
            data_type *xo = (data_type *)(line_x + xd_x); \
            *xo = (*xo & (data_type)(fc->mask_xo)) | ((data_type)(xo_) << fc->xo_shift); \
        } \
    }

#define AKVCAM_FILL1A(data_type) \
    static inline void akvcam_frame_private_fill1a_##data_type(akvcam_frame_t self, \
                                                               akvcam_fill_parameters_ct fc, \
                                                               uint32_t color) \
    { \
        size_t x; \
        \
        int xi = (int)((color >> 16) & 0xff); \
        int yi = (int)((color >>  8) & 0xff); \
        int zi = (int)((color >>  0) & 0xff); \
        int ai = (int)((color >> 24) & 0xff); \
        \
        uint8_t *line_x; \
        uint8_t *line_a; \
        \
        int64_t xo_ = 0; \
        akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo_); \
        \
        line_x = self->planes[fc->plane_xo] + fc->xo_offset; \
        line_a = self->planes[fc->plane_ao] + fc->ao_offset; \
        \
        for (x = 0; x < fc->read_width; ++x) { \
            int xd_x = fc->dst_width_offset_x[x]; \
            int xd_a = fc->dst_width_offset_a[x]; \
            \
            data_type *xo = (data_type *)(line_x + xd_x); \
            data_type *ao = (data_type *)(line_a + xd_a); \
            \
            *xo = (*xo & (data_type)(fc->mask_xo)) | ((data_type)(xo_) << fc->xo_shift); \
            *ao = (*ao & (data_type)(fc->mask_ao)) | ((data_type)(ai)  << fc->ao_shift); \
        } \
    }

/* Vectorized fill functions */

#define AKVCAM_FILLV3(data_type) \
    static inline void akvcam_frame_private_fillv3_##data_type(akvcam_frame_t self, \
                                                               akvcam_fill_parameters_ct fc, \
                                                               uint32_t color) \
    { \
        size_t x; \
        \
        int xi = (int)((color >> 16) & 0xff); \
        int yi = (int)((color >>  8) & 0xff); \
        int zi = (int)((color >>  0) & 0xff); \
        int ai = (int)((color >> 24) & 0xff); \
        \
        uint8_t *line_x; \
        uint8_t *line_y; \
        uint8_t *line_z; \
        \
        int64_t xo_ = 0; \
        int64_t yo_ = 0; \
        int64_t zo_ = 0; \
        akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo_, &yo_, &zo_); \
        akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo_, &yo_, &zo_); \
        \
        line_x = self->planes[fc->plane_xo] + fc->xo_offset; \
        line_y = self->planes[fc->plane_yo] + fc->yo_offset; \
        line_z = self->planes[fc->plane_zo] + fc->zo_offset; \
        \
        for (x = 0; x < fc->read_width; ++x) { \
            int xd_x = fc->dst_width_offset_x[x]; \
            int xd_y = fc->dst_width_offset_y[x]; \
            int xd_z = fc->dst_width_offset_z[x]; \
            \
            data_type *xo = (data_type *)(line_x + xd_x); \
            data_type *yo = (data_type *)(line_y + xd_y); \
            data_type *zo = (data_type *)(line_z + xd_z); \
            \
            *xo = (*xo & (data_type)(fc->mask_xo)) | ((data_type)(xo_) << fc->xo_shift); \
            *yo = (*yo & (data_type)(fc->mask_yo)) | ((data_type)(yo_) << fc->yo_shift); \
            *zo = (*zo & (data_type)(fc->mask_zo)) | ((data_type)(zo_) << fc->zo_shift); \
        } \
    }

#define AKVCAM_FILLV3A(data_type) \
    static inline void akvcam_frame_private_fillv3a_##data_type(akvcam_frame_t self, \
                                                                akvcam_fill_parameters_ct fc, \
                                                                uint32_t color) \
    { \
        size_t x; \
        \
        int xi = (int)((color >> 16) & 0xff); \
        int yi = (int)((color >>  8) & 0xff); \
        int zi = (int)((color >>  0) & 0xff); \
        int ai = (int)((color >> 24) & 0xff); \
        \
        uint8_t *line_x; \
        uint8_t *line_y; \
        uint8_t *line_z; \
        uint8_t *line_a; \
        \
        int64_t xo_ = 0; \
        int64_t yo_ = 0; \
        int64_t zo_ = 0; \
        akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo_, &yo_, &zo_); \
        \
        line_x = self->planes[fc->plane_xo] + fc->xo_offset; \
        line_y = self->planes[fc->plane_yo] + fc->yo_offset; \
        line_z = self->planes[fc->plane_zo] + fc->zo_offset; \
        line_a = self->planes[fc->plane_ao] + fc->ao_offset; \
        \
        for (x = 0; x < fc->read_width; ++x) { \
            int xd_x = fc->dst_width_offset_x[x]; \
            int xd_y = fc->dst_width_offset_y[x]; \
            int xd_z = fc->dst_width_offset_z[x]; \
            int xd_a = fc->dst_width_offset_a[x]; \
            \
            data_type *xo = (data_type *)(line_x + xd_x); \
            data_type *yo = (data_type *)(line_y + xd_y); \
            data_type *zo = (data_type *)(line_z + xd_z); \
            data_type *ao = (data_type *)(line_a + xd_a); \
            \
            *xo = (*xo & (data_type)(fc->mask_xo)) | ((data_type)(xo_) << fc->xo_shift); \
            *yo = (*yo & (data_type)(fc->mask_yo)) | ((data_type)(yo_) << fc->yo_shift); \
            *zo = (*zo & (data_type)(fc->mask_zo)) | ((data_type)(zo_) << fc->zo_shift); \
            *ao = (*ao & (data_type)(fc->mask_ao)) | ((data_type)(ai)  << fc->ao_shift); \
        } \
    }

#define AKVCAM_FILL_FRAME3(data_type) \
    static inline void akvcam_frame_private_fill_frame3_##data_type(akvcam_frame_t self, \
                                                                    akvcam_fill_parameters_ct fc, \
                                                                    uint32_t color) \
    { \
        switch (fc->alpha_mode) { \
        case AKVCAM_ALPHA_MODE_AO: \
            akvcam_frame_private_fill3a_##data_type(self, fc, color); \
            break; \
        case AKVCAM_ALPHA_MODE_O: \
            akvcam_frame_private_fill3_##data_type(self, fc, color); \
            break; \
        } \
    }

#define AKVCAM_FILL_FRAME1(data_type) \
    static inline void akvcam_frame_private_fill_frame1_##data_type(akvcam_frame_t self, \
                                                                    akvcam_fill_parameters_ct fc, \
                                                                    uint32_t color) \
    { \
        switch (fc->alpha_mode) { \
        case AKVCAM_ALPHA_MODE_AO: \
            akvcam_frame_private_fill1a_##data_type(self, fc, color); \
            break; \
        case AKVCAM_ALPHA_MODE_O: \
            akvcam_frame_private_fill1_##data_type(self, fc, color); \
            break; \
        } \
    }

#define AKVCAM_FILLV_FRAME3(data_type) \
    static inline void akvcam_frame_private_fillv_frame3_##data_type(akvcam_frame_t self, \
                                                                     akvcam_fill_parameters_ct fc, \
                                                                     uint32_t color) \
    { \
        switch (fc->alpha_mode) { \
        case AKVCAM_ALPHA_MODE_AO: \
            akvcam_frame_private_fillv3a_##data_type(self, fc, color); \
            break; \
        case AKVCAM_ALPHA_MODE_O: \
            akvcam_frame_private_fillv3_##data_type(self, fc, color); \
            break; \
        } \
    }

#define AKVCAM_FILL(data_type) \
    static inline void akvcam_frame_private_fill_##data_type(akvcam_frame_t self, \
                                                             akvcam_fill_parameters_ct fc, \
                                                             uint32_t color) \
    { \
        switch (fc->fill_type) { \
        case AKVCAM_FILL_TYPE_VECTOR: \
            akvcam_frame_private_fillv_frame3_##data_type(self, fc, color); \
            break; \
        case AKVCAM_FILL_TYPE_3: \
            akvcam_frame_private_fill_frame3_##data_type(self, fc, color); \
            break; \
        case AKVCAM_FILL_TYPE_1: \
            akvcam_frame_private_fill_frame1_##data_type(self, fc, color); \
            break; \
        } \
    }

AKVCAM_FILL3(uint8_t)
AKVCAM_FILL3(uint16_t)
AKVCAM_FILL3(uint32_t)

AKVCAM_FILL3A(uint8_t)
AKVCAM_FILL3A(uint16_t)
AKVCAM_FILL3A(uint32_t)

AKVCAM_FILL1(uint8_t)
AKVCAM_FILL1(uint16_t)
AKVCAM_FILL1(uint32_t)

AKVCAM_FILL1A(uint8_t)
AKVCAM_FILL1A(uint16_t)
AKVCAM_FILL1A(uint32_t)

AKVCAM_FILLV3(uint8_t)
AKVCAM_FILLV3(uint16_t)
AKVCAM_FILLV3(uint32_t)

AKVCAM_FILLV3A(uint8_t)
AKVCAM_FILLV3A(uint16_t)
AKVCAM_FILLV3A(uint32_t)

AKVCAM_FILL_FRAME3(uint8_t)
AKVCAM_FILL_FRAME3(uint16_t)
AKVCAM_FILL_FRAME3(uint32_t)

AKVCAM_FILL_FRAME1(uint8_t)
AKVCAM_FILL_FRAME1(uint16_t)
AKVCAM_FILL_FRAME1(uint32_t)

AKVCAM_FILLV_FRAME3(uint8_t)
AKVCAM_FILLV_FRAME3(uint16_t)
AKVCAM_FILLV_FRAME3(uint32_t)

AKVCAM_FILL(uint8_t)
AKVCAM_FILL(uint16_t)
AKVCAM_FILL(uint32_t)

static bool akvcam_frame_load_rle8(akvcam_frame_t self,
                                   akvcam_file_t bmp_file,
                                   const akvcam_bmp_image_header *image_header,
                                   const akvcam_palette_pixel *palette,
                                   bool top_down);
static void akvcam_frame_private_update_planes(akvcam_frame_t self);
void akvcam_frame_private_clear(akvcam_frame_t self);
static akvcam_fill_parameters_t akvcam_fill_parameters_new(void);
static void akvcam_fill_parameters_free(struct kref *ref);
static void akvcam_fill_parameters_allocate_buffers(akvcam_fill_parameters_t self,
                                                    akvcam_format_ct format);
static void akvcam_fill_parameters_clear_buffers(akvcam_fill_parameters_t self);
static void akvcam_fill_parameters_configure(akvcam_fill_parameters_t self,
                                             akvcam_format_ct format,
                                             akvcam_color_convert_t color_convert);
static void akvcam_fill_parameters_configure_fill(akvcam_fill_parameters_t self,
                                                  akvcam_format_ct format);

akvcam_frame_t akvcam_frame_new(akvcam_format_t format)
{
    akvcam_frame_t self = kzalloc(sizeof(struct akvcam_frame), GFP_KERNEL);
    size_t data_size;
    kref_init(&self->ref);
    self->format = format?
                    akvcam_format_new_copy(format):
                    akvcam_format_new(0, 0, 0, NULL);
    data_size = akvcam_format_size(self->format);

    if (data_size > 0)
        self->data = vzalloc(data_size);

    akvcam_frame_private_update_planes(self);

    return self;
}

akvcam_frame_t akvcam_frame_new_copy(akvcam_frame_ct other)
{
    akvcam_frame_t self = kzalloc(sizeof(struct akvcam_frame), GFP_KERNEL);
    size_t data_size;
    kref_init(&self->ref);
    self->format = akvcam_format_new_copy(other->format);
    data_size = akvcam_format_size(self->format);

    if (other->data && data_size > 0) {
        self->data = vzalloc(data_size);
        memcpy(self->data, other->data, data_size);
    }

    self->fc = other->fc;

    if (self->fc)
        kref_get(&self->fc->ref);

    akvcam_frame_private_update_planes(self);

    return self;
}

static void akvcam_frame_free(struct kref *ref)
{
    akvcam_frame_t self = container_of(ref, struct akvcam_frame, ref);

    if (self->fc)
        kref_put(&self->fc->ref, akvcam_fill_parameters_free);

    if (self->data)
        vfree(self->data);

    akvcam_format_delete(self->format);
    kfree(self);
}

void akvcam_frame_delete(akvcam_frame_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_frame_free);
}

akvcam_frame_t akvcam_frame_ref(akvcam_frame_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_frame_copy(akvcam_frame_t self, akvcam_frame_ct other)
{
    size_t data_size;
    akvcam_format_copy(self->format, other->format);

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    data_size = akvcam_format_size(self->format);

    if (other->data && data_size > 0) {
        self->data = vzalloc(data_size);
        memcpy(self->data, other->data, data_size);
    }

    if (self->fc)
        kref_put(&self->fc->ref, akvcam_fill_parameters_free);

    self->fc = other->fc;

    if (self->fc)
        kref_get(&self->fc->ref);

    akvcam_frame_private_update_planes(self);
}

akvcam_format_t akvcam_frame_format_nr(akvcam_frame_ct self)
{
    return self->format;
}

akvcam_format_t akvcam_frame_format(akvcam_frame_ct self)
{
    return akvcam_format_ref(self->format);
}

size_t akvcam_frame_size(akvcam_frame_ct self)
{
    return akvcam_format_size(self->format);
}

const char *akvcam_frame_const_data(akvcam_frame_ct self)
{
    return (char *) self->data;
}

char *akvcam_frame_data(akvcam_frame_ct self)
{
    return (char *) self->data;
}

const uint8_t *akvcam_frame_plane_const_data(akvcam_frame_ct self, size_t plane)
{
    return self->planes[plane];
}

uint8_t *akvcam_frame_plane_data(akvcam_frame_ct self, size_t plane)
{
    return self->planes[plane];
}

const uint8_t *akvcam_frame_const_line(akvcam_frame_ct self,
                                       size_t plane,
                                       size_t y)
{
    return self->planes[plane]
            + (y >> akvcam_format_height_div(self->format, plane))
            * akvcam_format_line_size(self->format, plane);
}

uint8_t *akvcam_frame_line(akvcam_frame_ct self, size_t plane, size_t y)
{
    return self->planes[plane]
            + (y >> akvcam_format_height_div(self->format, plane))
            * akvcam_format_line_size(self->format, plane);
}

bool akvcam_frame_load(akvcam_frame_t self, const char *file_name)
{
    akvcam_file_t bmp_file;
    char type[2];
    akvcam_bmp_header header;
    akvcam_bmp_image_header image_header;
    akvcam_palette_pixel palette[256];
    uint8_t *line;
    uint32_t x;
    uint32_t y;
    uint8_t pixel[4];
    bool top_down;
    size_t data_size;
    struct v4l2_fract frame_rate = {0, 0};

    akvcam_frame_private_clear(self);

    if (akvcam_strlen(file_name) < 1) {
        akpr_err("Bitmap file name not valid\n");

        return false;
    }

    bmp_file = akvcam_file_new(file_name);

    if (!akvcam_file_open(bmp_file)) {
        akpr_err("Can't open bitmap file: %s\n", file_name);

        goto akvcam_frame_load_failed;
    }

    akvcam_file_read(bmp_file, type, 2);

    if (memcmp(type, "BM", 2) != 0) {
        akpr_err("Invalid bitmap signature: %c%c\n", type[0], type[1]);

        goto akvcam_frame_load_failed;
    }

    akvcam_file_read(bmp_file,
                     (char *) &header,
                     sizeof(akvcam_bmp_header));
    akvcam_file_read(bmp_file,
                     (char *) &image_header,
                     sizeof(akvcam_bmp_image_header));


    // Fix the endianness of the used fields
    image_header.width       = le32_to_cpu(image_header.width);
    image_header.height      = le32_to_cpu(image_header.height);
    image_header.bitCount    = le16_to_cpu(image_header.bitCount);
    image_header.compression = le32_to_cpu(image_header.compression);
    header.offBits           = le32_to_cpu(header.offBits);

    top_down = (int32_t)image_header.height < 0;

    if (top_down)
        image_header.height = (uint32_t)(-(int32_t)image_header.height);

    // Read the palette if required by the format
    memset(palette, 0, sizeof(palette));

    if (image_header.bitCount <= 8) {
        uint32_t palette_offset = 14u + image_header.size;
        uint32_t palette_size = header.offBits - palette_offset;
        uint32_t n_colors = palette_size / 4u;
        uint32_t i;
        uint8_t  raw[4];

        if (n_colors > 256)
            n_colors = 256;

        for (i = 0; i < n_colors; i++) {
            akvcam_file_read(bmp_file, raw, 4);
            palette[i].r = raw[2];
            palette[i].g = raw[1];
            palette[i].b = raw[0];
        }
    }

    akvcam_file_seek(bmp_file, header.offBits, AKVCAM_FILE_SEEK_BEG);

    if (self->format) {
        frame_rate = akvcam_format_frame_rate(self->format);
        akvcam_format_delete(self->format);
    }

    self->format = akvcam_format_new(V4L2_PIX_FMT_ARGB32,
                                     image_header.width,
                                     image_header.height,
                                     &frame_rate);
    data_size = akvcam_format_size(self->format);

    if (data_size < 1) {
        akpr_err("Bitmap format is invalid\n");

        goto akvcam_frame_load_failed;
    }

    self->data = vzalloc(data_size);
    akvcam_frame_private_update_planes(self);

    if (self->fc) {
        kref_put(&self->fc->ref, akvcam_fill_parameters_free);
        self->fc = NULL;
    }

    switch (image_header.bitCount) {
        case 8:
             if (image_header.compression == 1) {
                 if (!akvcam_frame_load_rle8(self,
                                             bmp_file,
                                             &image_header,
                                             palette,
                                             top_down)) {
                     goto akvcam_frame_load_failed;
                 }
             } else if (image_header.compression == 0) {
                 uint32_t row_size = (image_header.width + 3u) & ~3u;
                 uint8_t *row_buf = vmalloc(row_size);

                 if (!row_buf) {
                     akpr_err("Failed to allocate row buffer\n");

                     goto akvcam_frame_load_failed;
                 }

                 for (y = 0; y < image_header.height; y++) {
                     uint32_t line_y = top_down? y: image_header.height - y - 1;
                     line = akvcam_frame_line(self, 0, line_y);
                     akvcam_file_read(bmp_file, row_buf, row_size);

                     for (x = 0; x < image_header.width; x++) {
                         uint8_t index = row_buf[x];
                         line[4 * x + 0] = 0xff;
                         line[4 * x + 1] = palette[index].r;
                         line[4 * x + 2] = palette[index].g;
                         line[4 * x + 3] = palette[index].b;
                     }
                 }

                 vfree(row_buf);
             } else {
                 akpr_err("Unsupported compression for 8-bit bitmap: %u\n",
                          image_header.compression);

                 goto akvcam_frame_load_failed;
             }

             break;

        case 24:
            for (y = 0; y < image_header.height; y++) {
                uint32_t line_y = top_down? y: image_header.height - y - 1;
                line = akvcam_frame_line(self, 0, line_y);

                for (x = 0; x < image_header.width; x++) {
                    akvcam_file_read(bmp_file, (char *) pixel, 3);
                    line[4 * x + 0] = 0xff;     // A
                    line[4 * x + 1] = pixel[2]; // R
                    line[4 * x + 2] = pixel[1]; // G
                    line[4 * x + 3] = pixel[0]; // B
                }
            }

            break;

        case 32:
            for (y = 0; y < image_header.height; y++) {
                uint32_t line_y = top_down? y: image_header.height - y - 1;
                line = akvcam_frame_line(self, 0, line_y);

                for (x = 0; x < image_header.width; x++) {
                    akvcam_file_read(bmp_file, (char *) pixel, 4);
                    line[4 * x + 0] = pixel[3]; // A
                    line[4 * x + 1] = pixel[2]; // R
                    line[4 * x + 2] = pixel[1]; // G
                    line[4 * x + 3] = pixel[0]; // B
                }
            }

            break;

        default:
            akpr_err("Bit count not supported in bitmap: %u\n",
                     image_header.bitCount);

            goto akvcam_frame_load_failed;
    }

    akvcam_file_delete(bmp_file);

    return true;

akvcam_frame_load_failed:
    akvcam_frame_private_clear(self);
    akvcam_file_delete(bmp_file);

    return false;
}

#define DEFINE_FILL_FUNC(size) \
    case AKVCAM_FILL_DATA_TYPES_##size: \
        akvcam_frame_private_fill_uint##size##_t(self, self->fc, color); \
        \
        if (self->fc->endianess != __BYTE_ORDER__) { \
            size_t nplanes = akvcam_format_planes(self->format); \
            size_t plane; \
            \
            for (plane = 0; plane < nplanes; ++plane) \
                akvcam_swap_data_bytes_uint##size##_t((uint##size##_t *)self->planes[plane], \
                                                      akvcam_format_plane_size(self->format, plane)); \
        } \
        \
        break;

void akvcam_frame_fill_rgba(akvcam_frame_t self, uint32_t color)
{
    int x;
    int y;
    size_t nplanes;
    size_t plane;

    if (!self->fc) {
        self->fc = akvcam_fill_parameters_new();
        akvcam_fill_parameters_configure(self->fc, self->format, self->fc->color_convert);
        akvcam_fill_parameters_configure_fill(self->fc, self->format);
    }

    switch (self->fc->fill_data_types) {
    DEFINE_FILL_FUNC(8)
    DEFINE_FILL_FUNC(16)
    DEFINE_FILL_FUNC(32)
    default:
        break;
    }

    nplanes = akvcam_format_planes(self->format);

    for (plane = 0; plane < nplanes; plane++) {
        size_t line_size = akvcam_format_line_size(self->format, plane);
        size_t pixel_size = akvcam_format_pixel_size(self->format, plane);
        uint8_t *line0 = self->planes[plane];
        uint8_t *line = line0 + pixel_size;
        size_t width = pixel_size > 0? line_size / pixel_size: 0;
        size_t height = self->fc->height >> akvcam_format_height_div(self->format, plane);

        for (x = 1; x < width; ++x) {
            memcpy(line, line0, pixel_size);
            line += pixel_size;
        }

        line = line0 + line_size;

        for (y = 1; y < height; ++y) {
            memcpy(line, line0, line_size);
            line += line_size;
        }
    }
}

static bool akvcam_frame_load_rle8(akvcam_frame_t self,
                                   akvcam_file_t bmp_file,
                                   const akvcam_bmp_image_header *image_header,
                                   const akvcam_palette_pixel *palette,
                                   bool top_down)
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint8_t *line;
    uint8_t pair[2];
    bool done = false;

    uint32_t line_y = top_down? y: image_header->height - y - 1;
    line = akvcam_frame_line(self, 0, line_y);

    while (!done) {
        if (akvcam_file_read(bmp_file, pair, 2) < 2) {
            akpr_err("RLE8: unexpected end of file\n");

            return false;
        }

        if (pair[0] != 0) {
            // Encoded mode: repeat pair[1] (índex) pair[0] times
            uint8_t count = pair[0];
            uint8_t index = pair[1];

            while (count--) {
                if (x >= image_header->width) {
                    akpr_err("RLE8: pixel out of bounds at (%u, %u)\n", x, y);

                    return false;
                }

                line[4 * x + 0] = 0xff;
                line[4 * x + 1] = palette[index].r;
                line[4 * x + 2] = palette[index].g;
                line[4 * x + 3] = palette[index].b;

                x++;
            }
        } else {
            switch (pair[1]) {
                case 0: // End of line
                    x = 0;
                    y++;

                    if (y >= image_header->height) {
                        akpr_err("RLE8: line index out of bounds\n");

                        return false;
                    }

                    uint32_t line_y = top_down? y: image_header->height - y - 1;
                    line = akvcam_frame_line(self, 0, line_y);

                    break;

                case 1: // End of picture
                    done = true;

                    break;

                case 2: { // Delta: seek to the current position
                    uint8_t delta[2];

                    if (akvcam_file_read(bmp_file, delta, 2) < 2) {
                        akpr_err("RLE8: unexpected end of file in delta\n");

                        return false;
                    }

                    x += delta[0];
                    y += delta[1];

                    if (x >= image_header->width || y >= image_header->height) {
                        akpr_err("RLE8: delta out of bounds (%u, %u)\n", x, y);

                        return false;
                    }

                    uint32_t line_y = top_down? y: image_header->height - y - 1;
                    line = akvcam_frame_line(self, 0, line_y);

                    break;
                }

                default: { // Absolute mode: literally pair[1] pixels
                    uint8_t count = pair[1];
                    uint8_t index;
                    uint8_t i;

                    for (i = 0; i < count; i++) {
                        if (akvcam_file_read(bmp_file, &index, 1) < 1) {
                            akpr_err("RLE8: unexpected end of file in absolute run\n");

                            return false;
                        }

                        if (x >= image_header->width) {
                            akpr_err("RLE8: pixel out of bounds at (%u, %u)\n", x, y);

                            return false;
                        }

                        line[4 * x + 0] = 0xff;
                        line[4 * x + 1] = palette[index].r;
                        line[4 * x + 2] = palette[index].g;
                        line[4 * x + 3] = palette[index].b;

                        x++;
                    }

                    // Seek 1 byte of padding if count is odd
                    if (count & 1)
                        akvcam_file_seek(bmp_file, 1, AKVCAM_FILE_SEEK_CUR);

                    break;
                }
            }
        }
    }

    return true;
}

void akvcam_frame_private_update_planes(akvcam_frame_t self)
{
    size_t nplanes = akvcam_format_planes(self->format);
    int i;

    for (i = 0; i < nplanes; ++i)
        self->planes[i] = self->data + akvcam_format_offset(self->format, i);
}

void akvcam_frame_private_clear(akvcam_frame_t self)
{
    if (self->format)
        akvcam_format_delete(self->format);

    self->format = akvcam_format_new(0, 0, 0, NULL);

    if (self->data) {
        vfree(self->data);
        self->data = NULL;
    }

    if (self->fc) {
        kref_put(&self->fc->ref, akvcam_fill_parameters_free);
        self->fc = NULL;
    }

    akvcam_frame_private_update_planes(self);
}

static akvcam_fill_parameters_t akvcam_fill_parameters_new(void)
{
    akvcam_fill_parameters_t self = kzalloc(sizeof(akvcam_fill_parameters), GFP_KERNEL);
    kref_init(&self->ref);

    self->color_convert = akvcam_color_convert_new();
    self->fill_type = AKVCAM_FILL_TYPE_3;
    self->fill_data_types = AKVCAM_FILL_DATA_TYPES_8;
    self->alpha_mode = AKVCAM_ALPHA_MODE_AO;

    self->endianess = __BYTE_ORDER__;

    self->width = 0;
    self->height = 0;
    self->read_width = 0;

    self->dst_width_offset_x = NULL;
    self->dst_width_offset_y = NULL;
    self->dst_width_offset_z = NULL;
    self->dst_width_offset_a = NULL;

    self->plane_xo = 0;
    self->plane_yo = 0;
    self->plane_zo = 0;
    self->plane_ao = 0;

    self->comp_xo = NULL;
    self->comp_yo = NULL;
    self->comp_zo = NULL;
    self->comp_ao = NULL;

    self->xo_offset = 0;
    self->yo_offset = 0;
    self->zo_offset = 0;
    self->ao_offset = 0;

    self->xo_shift = 0;
    self->yo_shift = 0;
    self->zo_shift = 0;
    self->ao_shift = 0;

    self->mask_xo = 0;
    self->mask_yo = 0;
    self->mask_zo = 0;
    self->mask_ao = 0;

    return self;
}

static void akvcam_fill_parameters_free(struct kref *ref)
{
    akvcam_fill_parameters_t self =
            container_of(ref, akvcam_fill_parameters, ref);

    akvcam_fill_parameters_clear_buffers(self);
    akvcam_color_convert_delete(self->color_convert);
    kfree(self);
}

static void akvcam_fill_parameters_clear_buffers(akvcam_fill_parameters_t self)
{
    if (self->dst_width_offset_x) {
        kfree(self->dst_width_offset_x);
        self->dst_width_offset_x = NULL;
    }

    if (self->dst_width_offset_y) {
        kfree(self->dst_width_offset_y);
        self->dst_width_offset_y = NULL;
    }

    if (self->dst_width_offset_z) {
        kfree(self->dst_width_offset_z);
        self->dst_width_offset_z = NULL;
    }

    if (self->dst_width_offset_a) {
        kfree(self->dst_width_offset_a);
        self->dst_width_offset_a = NULL;
    }
}

static void akvcam_fill_parameters_allocate_buffers(akvcam_fill_parameters_t self,
                                                    akvcam_format_ct format)
{
    akvcam_fill_parameters_clear_buffers(self);
    int width = akvcam_format_width(format);

    if (width > 0) {
        size_t buffer_size = width * sizeof(int);
        self->dst_width_offset_x = kzalloc(buffer_size, GFP_KERNEL);
        self->dst_width_offset_y = kzalloc(buffer_size, GFP_KERNEL);
        self->dst_width_offset_z = kzalloc(buffer_size, GFP_KERNEL);
        self->dst_width_offset_a = kzalloc(buffer_size, GFP_KERNEL);
    }
}

#define DEFINE_FILL_TYPES(size) \
    if (ospecs_depth == size) \
        self->fill_data_types = AKVCAM_FILL_DATA_TYPES_##size;

static void akvcam_fill_parameters_configure(akvcam_fill_parameters_t self,
                                             akvcam_format_ct format,
                                             akvcam_color_convert_t color_convert)
{
    akvcam_format_specs_ct ispecs = akvcam_format_specs_from_fixel_format(V4L2_PIX_FMT_ARGB32);
    akvcam_format_specs_ct ospecs = akvcam_format_specs_from_fixel_format(akvcam_format_fourcc(format));
    size_t ospecs_depth = akvcam_format_specs_depth(ospecs);
    size_t components;

    DEFINE_FILL_TYPES(8);
    DEFINE_FILL_TYPES(16);
    DEFINE_FILL_TYPES(32);
    DEFINE_FILL_TYPES(64);

    components = akvcam_format_specs_main_components(ospecs);

    switch (components) {
    case 3:
        self->fill_type =
            ospecs->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB?
                                AKVCAM_FILL_TYPE_VECTOR:
                                AKVCAM_FILL_TYPE_3;

        break;

    case 1:
        self->fill_type = AKVCAM_FILL_TYPE_1;

        break;

    default:
        break;
    }

    self->endianess = ospecs->endianness;
    akvcam_color_convert_load_matrix(color_convert, ispecs, ospecs);

    switch (ospecs->type) {
    case AKVCAM_VIDEO_FORMAT_TYPE_RGB:
        self->plane_xo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_R);
        self->plane_yo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_G);
        self->plane_zo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_B);

        self->comp_xo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_R);
        self->comp_yo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_G);
        self->comp_zo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_B);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_YUV:
        self->plane_xo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_Y);
        self->plane_yo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_U);
        self->plane_zo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_V);

        self->comp_xo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_Y);
        self->comp_yo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_U);
        self->comp_zo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_V);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_GRAY:
        self->plane_xo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_Y);
        self->comp_xo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_Y);

        break;

    default:
        break;
    }

    self->plane_ao = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_A);
    self->comp_ao = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_A);

    self->xo_offset = self->comp_xo? self->comp_xo->offset: 0;
    self->yo_offset = self->comp_yo? self->comp_yo->offset: 0;
    self->zo_offset = self->comp_zo? self->comp_zo->offset: 0;
    self->ao_offset = self->comp_ao? self->comp_ao->offset: 0;

    self->xo_shift = self->comp_xo? self->comp_xo->shift: 0;
    self->yo_shift = self->comp_yo? self->comp_yo->shift: 0;
    self->zo_shift = self->comp_zo? self->comp_zo->shift: 0;
    self->ao_shift = self->comp_ao? self->comp_ao->shift: 0;

    self->mask_xo = self->comp_xo? ~(akvcam_color_component_max(self->comp_xo) << self->comp_xo->shift): 0;
    self->mask_yo = self->comp_yo? ~(akvcam_color_component_max(self->comp_yo) << self->comp_yo->shift): 0;
    self->mask_zo = self->comp_zo? ~(akvcam_color_component_max(self->comp_zo) << self->comp_zo->shift): 0;
    self->mask_ao = self->comp_ao? ~(akvcam_color_component_max(self->comp_ao) << self->comp_ao->shift): 0;

    self->alpha_mode = akvcam_format_specs_contains(ospecs, AKVCAM_COMPONENT_TYPE_A)?
                          AKVCAM_ALPHA_MODE_AO:
                          AKVCAM_ALPHA_MODE_O;
}

static void akvcam_fill_parameters_configure_fill(akvcam_fill_parameters_t self,
                                                  akvcam_format_ct format)
{
    int x;
    akvcam_fill_parameters_allocate_buffers(self, format);
    self->width = akvcam_format_width(format);
    self->height = akvcam_format_height(format);
    self->read_width = akvcam_max(8 * akvcam_format_pixel_size(format, 0) / akvcam_format_bpp(format), 1);

    for (x = 0; x < self->width; ++x) {
        self->dst_width_offset_x[x] = self->comp_xo? (x >> self->comp_xo->width_div) * self->comp_xo->step: 0;
        self->dst_width_offset_y[x] = self->comp_yo? (x >> self->comp_yo->width_div) * self->comp_yo->step: 0;
        self->dst_width_offset_z[x] = self->comp_zo? (x >> self->comp_zo->width_div) * self->comp_zo->step: 0;
        self->dst_width_offset_a[x] = self->comp_ao? (x >> self->comp_ao->width_div) * self->comp_ao->step: 0;
    }
}
