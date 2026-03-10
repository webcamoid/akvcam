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
#include <linux/vmalloc.h>
#include <linux/videodev2.h>

#include "converter.h"
#include "color_convert.h"
#include "format.h"
#include "format_specs.h"
#include "frame.h"
#include "utils.h"

#define SCALE_EMULT 8

typedef enum
{
    AKVCAM_CONVERT_TYPE_VECTOR,
    AKVCAM_CONVERT_TYPE_1TO1,
    AKVCAM_CONVERT_TYPE_1TO3,
    AKVCAM_CONVERT_TYPE_3TO1,
    AKVCAM_CONVERT_TYPE_3TO3,
} AKVCAM_CONVERT_TYPE;

typedef enum
{
    AKVCAM_CONVERT_DATA_TYPES_8_8,
    AKVCAM_CONVERT_DATA_TYPES_8_16,
    AKVCAM_CONVERT_DATA_TYPES_8_32,
    AKVCAM_CONVERT_DATA_TYPES_16_8,
    AKVCAM_CONVERT_DATA_TYPES_16_16,
    AKVCAM_CONVERT_DATA_TYPES_16_32,
    AKVCAM_CONVERT_DATA_TYPES_32_8,
    AKVCAM_CONVERT_DATA_TYPES_32_16,
    AKVCAM_CONVERT_DATA_TYPES_32_32,
} AKVCAM_CONVERT_DATA_TYPES;

typedef enum
{
    AKVCAM_CONVERT_ALPHA_MODE_AI_AO,
    AKVCAM_CONVERT_ALPHA_MODE_AI_O,
    AKVCAM_CONVERT_ALPHA_MODE_I_AO,
    AKVCAM_CONVERT_ALPHA_MODE_I_O,
} AKVCAM_CONVERT_ALPHA_MODE;

typedef enum
{
    AKVCAM_RESIZE_MODE_KEEP,
    AKVCAM_RESIZE_MODE_UP,
    AKVCAM_RESIZE_MODE_DOWN,
} AKVCAM_RESIZE_MODE;

typedef struct
{
    int x;
    int y;
    int width;
    int height;
} akvcam_rect;

typedef akvcam_rect *akvcam_rect_t;
typedef const akvcam_rect *akvcam_rect_ct;

typedef struct
{
    akvcam_color_convert_t color_convert;

    akvcam_format_t input_format;
    akvcam_format_t output_format;
    akvcam_format_t output_convert_format;
    akvcam_frame_t output_frame;

    akvcam_rect input_rect;

    AKVCAM_YUV_COLOR_SPACE yuv_color_space;
    AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type;
    AKVCAM_SCALING_MODE scaling_mode;
    AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode;
    AKVCAM_CONVERT_TYPE convert_type;
    AKVCAM_CONVERT_DATA_TYPES convert_data_types;
    AKVCAM_CONVERT_ALPHA_MODE alpha_mode;
    AKVCAM_RESIZE_MODE resize_mode;
    bool fast_convertion;

    int from_endian;
    int to_endian;

    int xmin;
    int ymin;
    int xmax;
    int ymax;

    int input_width;
    int input_width_1;
    int input_height;

    int *src_width;
    int *src_width_1;
    int *src_width_offset_x;
    int *src_width_offset_y;
    int *src_width_offset_z;
    int *src_width_offset_a;
    int *src_height;

    int *dl_src_width_offset_x;
    int *dl_src_width_offset_y;
    int *dl_src_width_offset_z;
    int *dl_src_width_offset_a;

    int *src_width_offset_x_1;
    int *src_width_offset_y_1;
    int *src_width_offset_z_1;
    int *src_width_offset_a_1;
    int *src_height_1;

    int *dst_width_offset_x;
    int *dst_width_offset_y;
    int *dst_width_offset_z;
    int *dst_width_offset_a;

    size_t *src_height_dl_offset;
    size_t *src_height_dl_offset_1;

    uint64_t *integral_image_data_x;
    uint64_t *integral_image_data_y;
    uint64_t *integral_image_data_z;
    uint64_t *integral_image_data_a;

    int64_t *kx;
    int64_t *ky;
    uint64_t *kdl;

    int plane_xi;
    int plane_yi;
    int plane_zi;
    int plane_ai;

    akvcam_color_component_ct comp_xi;
    akvcam_color_component_ct comp_yi;
    akvcam_color_component_ct comp_zi;
    akvcam_color_component_ct comp_ai;

    int plane_xo;
    int plane_yo;
    int plane_zo;
    int plane_ao;

    akvcam_color_component_ct comp_xo;
    akvcam_color_component_ct comp_yo;
    akvcam_color_component_ct comp_zo;
    akvcam_color_component_ct comp_ao;

    size_t xi_offset;
    size_t yi_offset;
    size_t zi_offset;
    size_t ai_offset;

    size_t xo_offset;
    size_t yo_offset;
    size_t zo_offset;
    size_t ao_offset;

    size_t xi_shift;
    size_t yi_shift;
    size_t zi_shift;
    size_t ai_shift;

    size_t xo_shift;
    size_t yo_shift;
    size_t zo_shift;
    size_t ao_shift;

    uint64_t max_xi;
    uint64_t max_yi;
    uint64_t max_zi;
    uint64_t max_ai;

    uint64_t mask_xo;
    uint64_t mask_yo;
    uint64_t mask_zo;
    uint64_t mask_ao;

    uint64_t alpha_mask;
} akvcam_frame_convert_parameters;

typedef akvcam_frame_convert_parameters *akvcam_frame_convert_parameters_t;
typedef const akvcam_frame_convert_parameters *akvcam_frame_convert_parameters_ct;

struct akvcam_converter
{
    struct kref ref;
    akvcam_format_t output_format;
    akvcam_frame_convert_parameters *fc;
    size_t fc_size;
    int cache_index;
    AKVCAM_YUV_COLOR_SPACE yuv_color_space;
    AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type;
    AKVCAM_SCALING_MODE scaling_mode;
    AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode;
};

akvcam_frame_t akvcam_converter_private_convert(akvcam_converter_t self,
                                                akvcam_frame_ct frame,
                                                akvcam_format_ct output_format);
void akvcam_converter_private_convert_fast_8bits(akvcam_converter_t self,
                                                 akvcam_frame_convert_parameters_ct fc,
                                                 akvcam_frame_ct src,
                                                 akvcam_frame_t dst);
void akvcam_frame_convert_parameters_init(akvcam_frame_convert_parameters_t fc,
                                          size_t size);
void akvcam_frame_convert_parameters_copy(akvcam_frame_convert_parameters_t fc,
                                          akvcam_frame_convert_parameters_ct other,
                                          size_t size);
void akvcam_frame_convert_parameters_delete(akvcam_frame_convert_parameters_t *fc,
                                            size_t size);
void akvcam_frame_convert_parameters_configure(akvcam_frame_convert_parameters_t fc,
                                               akvcam_format_ct iformat,
                                               akvcam_format_ct oformat,
                                               akvcam_color_convert_t color_convert,
                                               AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                               AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type);
void akvcam_frame_convert_parameters_configure_scaling(akvcam_frame_convert_parameters_t fc,
                                                       akvcam_format_ct iformat,
                                                       akvcam_format_ct oformat,
                                                       AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode);
void akvcam_frame_convert_parameters_allocate_buffers(akvcam_frame_convert_parameters_t fc,
                                                      akvcam_format_ct oformat);
void akvcam_frame_convert_parameters_allocate_dl_buffers(akvcam_frame_convert_parameters_t fc,
                                                         akvcam_format_ct iformat,
                                                         akvcam_format_ct oformat);
void akvcam_frame_convert_parameters_clear_buffers(akvcam_frame_convert_parameters_t fc);
void akvcam_frame_convert_parameters_clear_dl_buffers(akvcam_frame_convert_parameters_t fc);

/* Color blending functions
 *
 * kx and ky must be in the range of [0, 2^N]
 */
static inline void akvcam_blend(int N,
                                int64_t a,
                                int64_t bx, int64_t by,
                                int64_t kx, int64_t ky,
                                int64_t *c)
{
    *c = (kx * (bx - a) + ky * (by - a) + (a << (N + 1))) >> (N + 1);
}

static inline void akvcam_blend2(int N,
                                 const int64_t *ax,
                                 const int64_t *bx, const int64_t *by,
                                 int64_t kx, int64_t ky,
                                 int64_t *c)
{
    akvcam_blend(N, ax[0], bx[0], by[0], kx, ky, c);
    akvcam_blend(N, ax[1], bx[1], by[1], kx, ky, c + 1);
}

static inline void akvcam_blend3(int N,
                                 const int64_t *ax,
                                 const int64_t *bx, const int64_t *by,
                                 int64_t kx, int64_t ky,
                                 int64_t *c)
{
    akvcam_blend(N, ax[0], bx[0], by[0], kx, ky, c);
    akvcam_blend(N, ax[1], bx[1], by[1], kx, ky, c + 1);
    akvcam_blend(N, ax[2], bx[2], by[2], kx, ky, c + 2);
}

static inline void akvcam_blend4(int N,
                                 const int64_t *ax,
                                 const int64_t *bx, const int64_t *by,
                                 int64_t kx, int64_t ky,
                                 int64_t *c)
{
    akvcam_blend(N, ax[0], bx[0], by[0], kx, ky, c);
    akvcam_blend(N, ax[1], bx[1], by[1], kx, ky, c + 1);
    akvcam_blend(N, ax[2], bx[2], by[2], kx, ky, c + 2);
    akvcam_blend(N, ax[3], bx[3], by[3], kx, ky, c + 3);
}

/* Component reading functions */

#define AKVCAM_READ1(itype) \
    static inline void akvcam_read1_##itype(akvcam_frame_convert_parameters_ct fc, \
                                            const uint8_t *src_line_x, \
                                            int x, \
                                            itype *xi) \
    { \
        int xs_x = fc->src_width_offset_x[x]; \
        *xi = *(const itype *)(src_line_x + xs_x); \
        \
        if (fc->from_endian != __BYTE_ORDER__) \
            *xi = akvcam_swap_bytes(itype, *xi); \
        \
        *xi = (*xi >> fc->xi_shift) & fc->max_xi; \
    }

#define AKVCAM_READ1A(itype) \
    static inline void akvcam_read1a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                             const uint8_t *src_line_x, \
                                             const uint8_t *src_line_a, \
                                             int x, \
                                             itype *xi, \
                                             itype *ai) \
    { \
        int xs_x = fc->src_width_offset_x[x]; \
        int xs_a = fc->src_width_offset_a[x]; \
        \
        itype xit = *(const itype *)(src_line_x + xs_x); \
        itype ait = *(const itype *)(src_line_a + xs_a); \
        \
        if (fc->from_endian != __BYTE_ORDER__) { \
            xit = akvcam_swap_bytes(itype, xit); \
            ait = akvcam_swap_bytes(itype, ait); \
        } \
        \
        *xi = (xit >> fc->xi_shift) & fc->max_xi; \
        *ai = (ait >> fc->ai_shift) & fc->max_ai; \
    }

#define AKVCAM_READ_DL1(itype) \
    static inline void akvcam_read_dl1_##itype(akvcam_frame_convert_parameters_ct fc, \
                                               const uint64_t *src_line_x, \
                                               const uint64_t *src_line_x_1, \
                                               int x, \
                                               const uint64_t *kdl, \
                                               itype *xi) \
    { \
        int xs   = fc->src_width[x]; \
        int xs_1 = fc->src_width_1[x]; \
        uint64_t k = kdl[x]; \
        \
        *xi = (src_line_x[xs] + src_line_x_1[xs_1] - src_line_x[xs_1] - src_line_x_1[xs]) / k; \
    }

#define AKVCAM_READ_DL1A(itype) \
    static inline void akvcam_read_dl1a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                const uint64_t *src_line_x, \
                                                const uint64_t *src_line_a, \
                                                const uint64_t *src_line_x_1, \
                                                const uint64_t *src_line_a_1, \
                                                int x, \
                                                const uint64_t *kdl, \
                                                itype *xi, \
                                                itype *ai) \
    { \
        int xs   = fc->src_width[x]; \
        int xs_1 = fc->src_width_1[x]; \
        uint64_t k = kdl[x]; \
        \
        *xi = (src_line_x[xs] + src_line_x_1[xs_1] - src_line_x[xs_1] - src_line_x_1[xs]) / k; \
        *ai = (src_line_a[xs] + src_line_a_1[xs_1] - src_line_a[xs_1] - src_line_a_1[xs]) / k; \
    }

#define AKVCAM_READ_UL1(itype) \
    static inline void akvcam_read_ul1_##itype(akvcam_frame_convert_parameters_ct fc, \
                                               const uint8_t *src_line_x, \
                                               const uint8_t *src_line_x_1, \
                                               int x, \
                                               int64_t ky, \
                                               itype *xi) \
    { \
        int64_t xib; \
        \
        int xs_x   = fc->src_width_offset_x[x]; \
        int xs_x_1 = fc->src_width_offset_x_1[x]; \
        \
        itype xi_  = *(const itype *)(src_line_x   + xs_x); \
        itype xi_x = *(const itype *)(src_line_x   + xs_x_1); \
        itype xi_y = *(const itype *)(src_line_x_1 + xs_x); \
        \
        if (fc->from_endian != __BYTE_ORDER__) { \
            xi_  = akvcam_swap_bytes(itype, xi_); \
            xi_x = akvcam_swap_bytes(itype, xi_x); \
            xi_y = akvcam_swap_bytes(itype, xi_y); \
        } \
        \
        xi_  = (xi_  >> fc->xi_shift) & fc->max_xi; \
        xi_x = (xi_x >> fc->xi_shift) & fc->max_xi; \
        xi_y = (xi_y >> fc->xi_shift) & fc->max_xi; \
        \
        xib = 0; \
        akvcam_blend(SCALE_EMULT, xi_, xi_x, xi_y, fc->kx[x], ky, &xib); \
        *xi = (itype) xib; \
    }

static inline void akvcam_read_f8ul1(akvcam_frame_convert_parameters_ct fc,
                                     const uint8_t *src_line_x,
                                     const uint8_t *src_line_x_1,
                                     int x,
                                     int64_t ky,
                                     uint8_t *xi)
{
    int xs_x   = fc->src_width_offset_x[x];
    int xs_x_1 = fc->src_width_offset_x_1[x];

    uint8_t xi_  = src_line_x[xs_x];
    uint8_t xi_x = src_line_x[xs_x_1];
    uint8_t xi_y = src_line_x_1[xs_x];

    int64_t xib = 0;
    akvcam_blend(SCALE_EMULT, xi_, xi_x, xi_y, fc->kx[x], ky, &xib);
    *xi = (uint8_t) xib;
}

#define AKVCAM_READ_UL1A(itype) \
    static inline void akvcam_read_ul1a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                const uint8_t *src_line_x, \
                                                const uint8_t *src_line_a, \
                                                const uint8_t *src_line_x_1, \
                                                const uint8_t *src_line_a_1, \
                                                int x, \
                                                int64_t ky, \
                                                itype *xi, \
                                                itype *ai) \
    { \
        int xs_x; \
        int xs_a; \
        int xs_x_1; \
        int xs_a_1; \
        itype xai0; \
        itype xai1; \
        itype xai_x0; \
        itype xai_x1; \
        itype xai_y0; \
        itype xai_y1; \
        int64_t xai[2]; \
        int64_t xai_x[2]; \
        int64_t xai_y[2]; \
        int64_t xaib[2]; \
        \
        xs_x   = fc->src_width_offset_x[x]; \
        xs_a   = fc->src_width_offset_a[x]; \
        xs_x_1 = fc->src_width_offset_x_1[x]; \
        xs_a_1 = fc->src_width_offset_a_1[x]; \
        \
        xai0   = *(const itype *)(src_line_x   + xs_x); \
        xai1   = *(const itype *)(src_line_a   + xs_a); \
        xai_x0 = *(const itype *)(src_line_x   + xs_x_1); \
        xai_x1 = *(const itype *)(src_line_a   + xs_a_1); \
        xai_y0 = *(const itype *)(src_line_x_1 + xs_x); \
        xai_y1 = *(const itype *)(src_line_a_1 + xs_a); \
        \
        if (fc->from_endian != __BYTE_ORDER__) { \
            xai0   = akvcam_swap_bytes(itype, xai0); \
            xai1   = akvcam_swap_bytes(itype, xai1); \
            xai_x0 = akvcam_swap_bytes(itype, xai_x0); \
            xai_x1 = akvcam_swap_bytes(itype, xai_x1); \
            xai_y0 = akvcam_swap_bytes(itype, xai_y0); \
            xai_y1 = akvcam_swap_bytes(itype, xai_y1); \
        } \
        \
        xai[0]   = (xai0   >> fc->xi_shift) & fc->max_xi; \
        xai[1]   = (xai1   >> fc->ai_shift) & fc->max_ai; \
        xai_x[0] = (xai_x0 >> fc->xi_shift) & fc->max_xi; \
        xai_x[1] = (xai_x1 >> fc->ai_shift) & fc->max_ai; \
        xai_y[0] = (xai_y0 >> fc->xi_shift) & fc->max_xi; \
        xai_y[1] = (xai_y1 >> fc->ai_shift) & fc->max_ai; \
        \
        akvcam_blend2(SCALE_EMULT, xai, xai_x, xai_y, fc->kx[x], ky, xaib); \
        *xi = (itype) xaib[0]; \
        *ai = (itype) xaib[1]; \
    }

static inline void akvcam_read_f8ul1a(akvcam_frame_convert_parameters_ct fc,
                                      const uint8_t *src_line_x,
                                      const uint8_t *src_line_a,
                                      const uint8_t *src_line_x_1,
                                      const uint8_t *src_line_a_1,
                                      int x,
                                      int64_t ky,
                                      uint8_t *xi,
                                      uint8_t *ai)
{
    int xs_x   = fc->src_width_offset_x[x];
    int xs_a   = fc->src_width_offset_a[x];
    int xs_x_1 = fc->src_width_offset_x_1[x];
    int xs_a_1 = fc->src_width_offset_a_1[x];

    int64_t xai[]   = {src_line_x[xs_x],   src_line_a[xs_a]};
    int64_t xai_x[] = {src_line_x[xs_x_1], src_line_a[xs_a_1]};
    int64_t xai_y[] = {src_line_x_1[xs_x], src_line_a_1[xs_a]};

    int64_t xaib[2];
    akvcam_blend2(SCALE_EMULT, xai, xai_x, xai_y, fc->kx[x], ky, xaib);
    *xi = (uint8_t) xaib[0];
    *ai = (uint8_t) xaib[1];
}

#define AKVCAM_READ3(itype) \
    static inline void akvcam_read3_##itype(akvcam_frame_convert_parameters_ct fc, \
                                            const uint8_t *src_line_x, \
                                            const uint8_t *src_line_y, \
                                            const uint8_t *src_line_z, \
                                            int x, \
                                            itype *xi, \
                                            itype *yi, \
                                            itype *zi) \
    { \
        int xs_x = fc->src_width_offset_x[x]; \
        int xs_y = fc->src_width_offset_y[x]; \
        int xs_z = fc->src_width_offset_z[x]; \
        \
        itype xit = *(const itype *)(src_line_x + xs_x); \
        itype yit = *(const itype *)(src_line_y + xs_y); \
        itype zit = *(const itype *)(src_line_z + xs_z); \
        \
        if (fc->from_endian != __BYTE_ORDER__) { \
            xit = akvcam_swap_bytes(itype, xit); \
            yit = akvcam_swap_bytes(itype, yit); \
            zit = akvcam_swap_bytes(itype, zit); \
        } \
        \
        *xi = (xit >> fc->xi_shift) & fc->max_xi; \
        *yi = (yit >> fc->yi_shift) & fc->max_yi; \
        *zi = (zit >> fc->zi_shift) & fc->max_zi; \
    }

#define AKVCAM_READ3A(itype) \
    static inline void akvcam_read3a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                             const uint8_t *src_line_x, \
                                             const uint8_t *src_line_y, \
                                             const uint8_t *src_line_z, \
                                             const uint8_t *src_line_a, \
                                             int x, \
                                             itype *xi, \
                                             itype *yi, \
                                             itype *zi, \
                                             itype *ai) \
    { \
        int xs_x = fc->src_width_offset_x[x]; \
        int xs_y = fc->src_width_offset_y[x]; \
        int xs_z = fc->src_width_offset_z[x]; \
        int xs_a = fc->src_width_offset_a[x]; \
        \
        itype xit = *(const itype *)(src_line_x + xs_x); \
        itype yit = *(const itype *)(src_line_y + xs_y); \
        itype zit = *(const itype *)(src_line_z + xs_z); \
        itype ait = *(const itype *)(src_line_a + xs_a); \
        \
        if (fc->from_endian != __BYTE_ORDER__) { \
            xit = akvcam_swap_bytes(itype, xit); \
            yit = akvcam_swap_bytes(itype, yit); \
            zit = akvcam_swap_bytes(itype, zit); \
            ait = akvcam_swap_bytes(itype, ait); \
        } \
        \
        *xi = (xit >> fc->xi_shift) & fc->max_xi; \
        *yi = (yit >> fc->yi_shift) & fc->max_yi; \
        *zi = (zit >> fc->zi_shift) & fc->max_zi; \
        *ai = (ait >> fc->ai_shift) & fc->max_ai; \
    }

#define AKVCAM_READ_DL3(itype) \
    static inline void akvcam_read_dl3_##itype(akvcam_frame_convert_parameters_ct fc, \
                                               const uint64_t *src_line_x, \
                                               const uint64_t *src_line_y, \
                                               const uint64_t *src_line_z, \
                                               const uint64_t *src_line_x_1, \
                                               const uint64_t *src_line_y_1, \
                                               const uint64_t *src_line_z_1, \
                                               int x, \
                                               const uint64_t *kdl, \
                                               itype *xi, \
                                               itype *yi, \
                                               itype *zi) \
    { \
        int xs   = fc->src_width[x]; \
        int xs_1 = fc->src_width_1[x]; \
        uint64_t k = kdl[x]; \
        \
        *xi = (src_line_x[xs] + src_line_x_1[xs_1] - src_line_x[xs_1] - src_line_x_1[xs]) / k; \
        *yi = (src_line_y[xs] + src_line_y_1[xs_1] - src_line_y[xs_1] - src_line_y_1[xs]) / k; \
        *zi = (src_line_z[xs] + src_line_z_1[xs_1] - src_line_z[xs_1] - src_line_z_1[xs]) / k; \
    }

#define AKVCAM_READ_DL3A(itype) \
    static inline void akvcam_read_dl3a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                const uint64_t *src_line_x, \
                                                const uint64_t *src_line_y, \
                                                const uint64_t *src_line_z, \
                                                const uint64_t *src_line_a, \
                                                const uint64_t *src_line_x_1, \
                                                const uint64_t *src_line_y_1, \
                                                const uint64_t *src_line_z_1, \
                                                const uint64_t *src_line_a_1, \
                                                int x, \
                                                const uint64_t *kdl, \
                                                itype *xi, \
                                                itype *yi, \
                                                itype *zi, \
                                                itype *ai) \
    { \
        int xs   = fc->src_width[x]; \
        int xs_1 = fc->src_width_1[x]; \
        uint64_t k = kdl[x]; \
        \
        *xi = (src_line_x[xs] + src_line_x_1[xs_1] - src_line_x[xs_1] - src_line_x_1[xs]) / k; \
        *yi = (src_line_y[xs] + src_line_y_1[xs_1] - src_line_y[xs_1] - src_line_y_1[xs]) / k; \
        *zi = (src_line_z[xs] + src_line_z_1[xs_1] - src_line_z[xs_1] - src_line_z_1[xs]) / k; \
        *ai = (src_line_a[xs] + src_line_a_1[xs_1] - src_line_a[xs_1] - src_line_a_1[xs]) / k; \
    }

#define AKVCAM_READ_UL3(itype) \
    static inline void akvcam_read_ul3_##itype(akvcam_frame_convert_parameters_ct fc, \
                                               const uint8_t *src_line_x, \
                                               const uint8_t *src_line_y, \
                                               const uint8_t *src_line_z, \
                                               const uint8_t *src_line_x_1, \
                                               const uint8_t *src_line_y_1, \
                                               const uint8_t *src_line_z_1, \
                                               int x, \
                                               int64_t ky, \
                                               itype *xi, \
                                               itype *yi, \
                                               itype *zi) \
    { \
        int xs_x; \
        int xs_y; \
        int xs_z; \
        int xs_x_1; \
        int xs_y_1; \
        int xs_z_1; \
        itype xyzi0; \
        itype xyzi1; \
        itype xyzi2; \
        itype xyzi_x0; \
        itype xyzi_x1; \
        itype xyzi_x2; \
        itype xyzi_y0; \
        itype xyzi_y1; \
        itype xyzi_y2; \
        int64_t xyzi[3]; \
        int64_t xyzi_x[3]; \
        int64_t xyzi_y[3]; \
        int64_t xyzib[3]; \
        \
        xs_x   = fc->src_width_offset_x[x]; \
        xs_y   = fc->src_width_offset_y[x]; \
        xs_z   = fc->src_width_offset_z[x]; \
        xs_x_1 = fc->src_width_offset_x_1[x]; \
        xs_y_1 = fc->src_width_offset_y_1[x]; \
        xs_z_1 = fc->src_width_offset_z_1[x]; \
        \
        xyzi0   = *(const itype *)(src_line_x   + xs_x); \
        xyzi1   = *(const itype *)(src_line_y   + xs_y); \
        xyzi2   = *(const itype *)(src_line_z   + xs_z); \
        xyzi_x0 = *(const itype *)(src_line_x   + xs_x_1); \
        xyzi_x1 = *(const itype *)(src_line_y   + xs_y_1); \
        xyzi_x2 = *(const itype *)(src_line_z   + xs_z_1); \
        xyzi_y0 = *(const itype *)(src_line_x_1 + xs_x); \
        xyzi_y1 = *(const itype *)(src_line_y_1 + xs_y); \
        xyzi_y2 = *(const itype *)(src_line_z_1 + xs_z); \
        \
        if (fc->from_endian != __BYTE_ORDER__) { \
            xyzi0   = akvcam_swap_bytes(itype, xyzi0); \
            xyzi1   = akvcam_swap_bytes(itype, xyzi1); \
            xyzi2   = akvcam_swap_bytes(itype, xyzi2); \
            xyzi_x0 = akvcam_swap_bytes(itype, xyzi_x0); \
            xyzi_x1 = akvcam_swap_bytes(itype, xyzi_x1); \
            xyzi_x2 = akvcam_swap_bytes(itype, xyzi_x2); \
            xyzi_y0 = akvcam_swap_bytes(itype, xyzi_y0); \
            xyzi_y1 = akvcam_swap_bytes(itype, xyzi_y1); \
            xyzi_y2 = akvcam_swap_bytes(itype, xyzi_y2); \
        } \
        \
        xyzi[0]   = (xyzi0   >> fc->xi_shift) & fc->max_xi; \
        xyzi[1]   = (xyzi1   >> fc->yi_shift) & fc->max_yi; \
        xyzi[2]   = (xyzi2   >> fc->zi_shift) & fc->max_zi; \
        xyzi_x[0] = (xyzi_x0 >> fc->xi_shift) & fc->max_xi; \
        xyzi_x[1] = (xyzi_x1 >> fc->yi_shift) & fc->max_yi; \
        xyzi_x[2] = (xyzi_x2 >> fc->zi_shift) & fc->max_zi; \
        xyzi_y[0] = (xyzi_y0 >> fc->xi_shift) & fc->max_xi; \
        xyzi_y[1] = (xyzi_y1 >> fc->yi_shift) & fc->max_yi; \
        xyzi_y[2] = (xyzi_y2 >> fc->zi_shift) & fc->max_zi; \
        \
        akvcam_blend3(SCALE_EMULT, xyzi, xyzi_x, xyzi_y, fc->kx[x], ky, xyzib); \
        *xi = (itype) xyzib[0]; \
        *yi = (itype) xyzib[1]; \
        *zi = (itype) xyzib[2]; \
    }

static inline void akvcam_read_f8ul3(akvcam_frame_convert_parameters_ct fc,
                                     const uint8_t *src_line_x,
                                     const uint8_t *src_line_y,
                                     const uint8_t *src_line_z,
                                     const uint8_t *src_line_x_1,
                                     const uint8_t *src_line_y_1,
                                     const uint8_t *src_line_z_1,
                                     int x,
                                     int64_t ky,
                                     uint8_t *xi,
                                     uint8_t *yi,
                                     uint8_t *zi)
{
    int xs_x   = fc->src_width_offset_x[x];
    int xs_y   = fc->src_width_offset_y[x];
    int xs_z   = fc->src_width_offset_z[x];
    int xs_x_1 = fc->src_width_offset_x_1[x];
    int xs_y_1 = fc->src_width_offset_y_1[x];
    int xs_z_1 = fc->src_width_offset_z_1[x];

    int64_t xyzi[]   = {src_line_x[xs_x],   src_line_y[xs_y],   src_line_z[xs_z]};
    int64_t xyzi_x[] = {src_line_x[xs_x_1], src_line_y[xs_y_1], src_line_z[xs_z_1]};
    int64_t xyzi_y[] = {src_line_x_1[xs_x], src_line_y_1[xs_y], src_line_z_1[xs_z]};

    int64_t xyzib[3];
    akvcam_blend3(SCALE_EMULT, xyzi, xyzi_x, xyzi_y, fc->kx[x], ky, xyzib);
    *xi = (uint8_t) xyzib[0];
    *yi = (uint8_t) xyzib[1];
    *zi = (uint8_t) xyzib[2];
}

#define AKVCAM_READ_UL3A(itype) \
    static inline void akvcam_read_ul3a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                const uint8_t *src_line_x, \
                                                const uint8_t *src_line_y, \
                                                const uint8_t *src_line_z, \
                                                const uint8_t *src_line_a, \
                                                const uint8_t *src_line_x_1, \
                                                const uint8_t *src_line_y_1, \
                                                const uint8_t *src_line_z_1, \
                                                const uint8_t *src_line_a_1, \
                                                int x, \
                                                int64_t ky, \
                                                itype *xi, \
                                                itype *yi, \
                                                itype *zi, \
                                                itype *ai) \
    { \
        int xs_x; \
        int xs_y; \
        int xs_z; \
        int xs_a; \
        int xs_x_1; \
        int xs_y_1; \
        int xs_z_1; \
        int xs_a_1; \
        itype xyzai0; \
        itype xyzai1; \
        itype xyzai2; \
        itype xyzai3; \
        itype xyzai_x0; \
        itype xyzai_x1; \
        itype xyzai_x2; \
        itype xyzai_x3; \
        itype xyzai_y0; \
        itype xyzai_y1; \
        itype xyzai_y2; \
        itype xyzai_y3; \
        int64_t xyzai[4]; \
        int64_t xyzai_x[4]; \
        int64_t xyzai_y[4]; \
        int64_t xyzaib[4]; \
        \
        xs_x   = fc->src_width_offset_x[x]; \
        xs_y   = fc->src_width_offset_y[x]; \
        xs_z   = fc->src_width_offset_z[x]; \
        xs_a   = fc->src_width_offset_a[x]; \
        xs_x_1 = fc->src_width_offset_x_1[x]; \
        xs_y_1 = fc->src_width_offset_y_1[x]; \
        xs_z_1 = fc->src_width_offset_z_1[x]; \
        xs_a_1 = fc->src_width_offset_a_1[x]; \
        \
        xyzai0   = *(const itype *)(src_line_x   + xs_x); \
        xyzai1   = *(const itype *)(src_line_y   + xs_y); \
        xyzai2   = *(const itype *)(src_line_z   + xs_z); \
        xyzai3   = *(const itype *)(src_line_a   + xs_a); \
        xyzai_x0 = *(const itype *)(src_line_x   + xs_x_1); \
        xyzai_x1 = *(const itype *)(src_line_y   + xs_y_1); \
        xyzai_x2 = *(const itype *)(src_line_z   + xs_z_1); \
        xyzai_x3 = *(const itype *)(src_line_a   + xs_a_1); \
        xyzai_y0 = *(const itype *)(src_line_x_1 + xs_x); \
        xyzai_y1 = *(const itype *)(src_line_y_1 + xs_y); \
        xyzai_y2 = *(const itype *)(src_line_z_1 + xs_z); \
        xyzai_y3 = *(const itype *)(src_line_a_1 + xs_a); \
        \
        if (fc->from_endian != __BYTE_ORDER__) { \
            xyzai0   = akvcam_swap_bytes(itype, xyzai0); \
            xyzai1   = akvcam_swap_bytes(itype, xyzai1); \
            xyzai2   = akvcam_swap_bytes(itype, xyzai2); \
            xyzai3   = akvcam_swap_bytes(itype, xyzai3); \
            xyzai_x0 = akvcam_swap_bytes(itype, xyzai_x0); \
            xyzai_x1 = akvcam_swap_bytes(itype, xyzai_x1); \
            xyzai_x2 = akvcam_swap_bytes(itype, xyzai_x2); \
            xyzai_x3 = akvcam_swap_bytes(itype, xyzai_x3); \
            xyzai_y0 = akvcam_swap_bytes(itype, xyzai_y0); \
            xyzai_y1 = akvcam_swap_bytes(itype, xyzai_y1); \
            xyzai_y2 = akvcam_swap_bytes(itype, xyzai_y2); \
            xyzai_y3 = akvcam_swap_bytes(itype, xyzai_y3); \
        } \
        \
        xyzai[0]   = (xyzai0   >> fc->xi_shift) & fc->max_xi; \
        xyzai[1]   = (xyzai1   >> fc->yi_shift) & fc->max_yi; \
        xyzai[2]   = (xyzai2   >> fc->zi_shift) & fc->max_zi; \
        xyzai[3]   = (xyzai3   >> fc->ai_shift) & fc->max_ai; \
        xyzai_x[0] = (xyzai_x0 >> fc->xi_shift) & fc->max_xi; \
        xyzai_x[1] = (xyzai_x1 >> fc->yi_shift) & fc->max_yi; \
        xyzai_x[2] = (xyzai_x2 >> fc->zi_shift) & fc->max_zi; \
        xyzai_x[3] = (xyzai_x3 >> fc->ai_shift) & fc->max_ai; \
        xyzai_y[0] = (xyzai_y0 >> fc->xi_shift) & fc->max_xi; \
        xyzai_y[1] = (xyzai_y1 >> fc->yi_shift) & fc->max_yi; \
        xyzai_y[2] = (xyzai_y2 >> fc->zi_shift) & fc->max_zi; \
        xyzai_y[3] = (xyzai_y3 >> fc->ai_shift) & fc->max_ai; \
        \
        akvcam_blend4(SCALE_EMULT, xyzai, xyzai_x, xyzai_y, fc->kx[x], ky, xyzaib); \
        *xi = (itype) xyzaib[0]; \
        *yi = (itype) xyzaib[1]; \
        *zi = (itype) xyzaib[2]; \
        *ai = (itype) xyzaib[3]; \
    }

static inline void akvcam_read_f8ul3a(akvcam_frame_convert_parameters_ct fc,
                                      const uint8_t *src_line_x,
                                      const uint8_t *src_line_y,
                                      const uint8_t *src_line_z,
                                      const uint8_t *src_line_a,
                                      const uint8_t *src_line_x_1,
                                      const uint8_t *src_line_y_1,
                                      const uint8_t *src_line_z_1,
                                      const uint8_t *src_line_a_1,
                                      int x,
                                      int64_t ky,
                                      uint8_t *xi,
                                      uint8_t *yi,
                                      uint8_t *zi,
                                      uint8_t *ai)
{
    int xs_x   = fc->src_width_offset_x[x];
    int xs_y   = fc->src_width_offset_y[x];
    int xs_z   = fc->src_width_offset_z[x];
    int xs_a   = fc->src_width_offset_a[x];
    int xs_x_1 = fc->src_width_offset_x_1[x];
    int xs_y_1 = fc->src_width_offset_y_1[x];
    int xs_z_1 = fc->src_width_offset_z_1[x];
    int xs_a_1 = fc->src_width_offset_a_1[x];

    int64_t xyzai[]   = {src_line_x[xs_x],   src_line_y[xs_y],
                         src_line_z[xs_z],   src_line_a[xs_a]};
    int64_t xyzai_x[] = {src_line_x[xs_x_1], src_line_y[xs_y_1],
                         src_line_z[xs_z_1], src_line_a[xs_a_1]};
    int64_t xyzai_y[] = {src_line_x_1[xs_x], src_line_y_1[xs_y],
                         src_line_z_1[xs_z], src_line_a_1[xs_a]};

    int64_t xyzaib[4];
    akvcam_blend4(SCALE_EMULT, xyzai, xyzai_x, xyzai_y, fc->kx[x], ky, xyzaib);
    *xi = (uint8_t) xyzaib[0];
    *yi = (uint8_t) xyzaib[1];
    *zi = (uint8_t) xyzaib[2];
    *ai = (uint8_t) xyzaib[3];
}

AKVCAM_READ1(uint8_t)
AKVCAM_READ1(uint16_t)
AKVCAM_READ1(uint32_t)

AKVCAM_READ1A(uint8_t)
AKVCAM_READ1A(uint16_t)
AKVCAM_READ1A(uint32_t)

AKVCAM_READ_DL1(uint8_t)
AKVCAM_READ_DL1(uint16_t)
AKVCAM_READ_DL1(uint32_t)

AKVCAM_READ_DL1A(uint8_t)
AKVCAM_READ_DL1A(uint16_t)
AKVCAM_READ_DL1A(uint32_t)

AKVCAM_READ_UL1(uint8_t)
AKVCAM_READ_UL1(uint16_t)
AKVCAM_READ_UL1(uint32_t)

AKVCAM_READ_UL1A(uint8_t)
AKVCAM_READ_UL1A(uint16_t)
AKVCAM_READ_UL1A(uint32_t)

AKVCAM_READ3(uint8_t)
AKVCAM_READ3(uint16_t)
AKVCAM_READ3(uint32_t)

AKVCAM_READ3A(uint8_t)
AKVCAM_READ3A(uint16_t)
AKVCAM_READ3A(uint32_t)

AKVCAM_READ_DL3(uint8_t)
AKVCAM_READ_DL3(uint16_t)
AKVCAM_READ_DL3(uint32_t)

AKVCAM_READ_DL3A(uint8_t)
AKVCAM_READ_DL3A(uint16_t)
AKVCAM_READ_DL3A(uint32_t)

AKVCAM_READ_UL3(uint8_t)
AKVCAM_READ_UL3(uint16_t)
AKVCAM_READ_UL3(uint32_t)

AKVCAM_READ_UL3A(uint8_t)
AKVCAM_READ_UL3A(uint16_t)
AKVCAM_READ_UL3A(uint32_t)

/* Component writing functions */

#define AKVCAM_WRITE1(otype) \
    static inline void akvcam_write1_##otype(akvcam_frame_convert_parameters_ct fc, \
                                             uint8_t *dst_line_x, \
                                             int x, \
                                             otype xo) \
    { \
        int xd_x = fc->dst_width_offset_x[x]; \
        otype *xo_ = (otype *)(dst_line_x + xd_x); \
        *xo_ = (*xo_ & (otype)(fc->mask_xo)) | ((otype)(xo) << fc->xo_shift); \
    }

#define AKVCAM_WRITE1A_AO(otype) \
    static inline void akvcam_write1a_ao_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                 uint8_t *dst_line_x, \
                                                 uint8_t *dst_line_a, \
                                                 int x, \
                                                 otype xo, \
                                                 otype ao) \
    { \
        int xd_x = fc->dst_width_offset_x[x]; \
        int xd_a = fc->dst_width_offset_a[x]; \
        \
        otype *xo_ = (otype *)(dst_line_x + xd_x); \
        otype *ao_ = (otype *)(dst_line_a + xd_a); \
        \
        *xo_ = (*xo_ & (otype)(fc->mask_xo)) | ((otype)(xo) << fc->xo_shift); \
        *ao_ = (*ao_ & (otype)(fc->mask_ao)) | ((otype)(ao) << fc->ao_shift); \
    }

#define AKVCAM_WRITE1A(otype) \
    static inline void akvcam_write1a_##otype(akvcam_frame_convert_parameters_ct fc, \
                                              uint8_t *dst_line_x, \
                                              uint8_t *dst_line_a, \
                                              int x, \
                                              otype xo) \
    { \
        int xd_x = fc->dst_width_offset_x[x]; \
        int xd_a = fc->dst_width_offset_a[x]; \
        \
        otype *xo_ = (otype *)(dst_line_x + xd_x); \
        otype *ao_ = (otype *)(dst_line_a + xd_a); \
        \
        *xo_ = (*xo_ & (otype)(fc->mask_xo)) | ((otype)(xo) << fc->xo_shift); \
        *ao_ = *ao_ | (otype)(fc->alpha_mask); \
    }

#define AKVCAM_WRITE3(otype) \
    static inline void akvcam_write3_##otype(akvcam_frame_convert_parameters_ct fc, \
                                             uint8_t *dst_line_x, \
                                             uint8_t *dst_line_y, \
                                             uint8_t *dst_line_z, \
                                             int x, \
                                             otype xo, \
                                             otype yo, \
                                             otype zo) \
    { \
        int xd_x = fc->dst_width_offset_x[x]; \
        int xd_y = fc->dst_width_offset_y[x]; \
        int xd_z = fc->dst_width_offset_z[x]; \
        \
        otype *xo_ = (otype *)(dst_line_x + xd_x); \
        otype *yo_ = (otype *)(dst_line_y + xd_y); \
        otype *zo_ = (otype *)(dst_line_z + xd_z); \
        \
        *xo_ = (*xo_ & (otype)(fc->mask_xo)) | ((otype)(xo) << fc->xo_shift); \
        *yo_ = (*yo_ & (otype)(fc->mask_yo)) | ((otype)(yo) << fc->yo_shift); \
        *zo_ = (*zo_ & (otype)(fc->mask_zo)) | ((otype)(zo) << fc->zo_shift); \
    }

#define AKVCAM_WRITE3A_AO(otype) \
    static inline void akvcam_write3a_ao_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                 uint8_t *dst_line_x, \
                                                 uint8_t *dst_line_y, \
                                                 uint8_t *dst_line_z, \
                                                 uint8_t *dst_line_a, \
                                                 int x, \
                                                 otype xo, \
                                                 otype yo, \
                                                 otype zo, \
                                                 otype ao) \
    { \
        int xd_x = fc->dst_width_offset_x[x]; \
        int xd_y = fc->dst_width_offset_y[x]; \
        int xd_z = fc->dst_width_offset_z[x]; \
        int xd_a = fc->dst_width_offset_a[x]; \
        \
        otype *xo_ = (otype *)(dst_line_x + xd_x); \
        otype *yo_ = (otype *)(dst_line_y + xd_y); \
        otype *zo_ = (otype *)(dst_line_z + xd_z); \
        otype *ao_ = (otype *)(dst_line_a + xd_a); \
        \
        *xo_ = (*xo_ & (otype)(fc->mask_xo)) | ((otype)(xo) << fc->xo_shift); \
        *yo_ = (*yo_ & (otype)(fc->mask_yo)) | ((otype)(yo) << fc->yo_shift); \
        *zo_ = (*zo_ & (otype)(fc->mask_zo)) | ((otype)(zo) << fc->zo_shift); \
        *ao_ = (*ao_ & (otype)(fc->mask_ao)) | ((otype)(ao) << fc->ao_shift); \
    }

#define AKVCAM_WRITE3A(otype) \
    static inline void akvcam_write3a_##otype(akvcam_frame_convert_parameters_ct fc, \
                                              uint8_t *dst_line_x, \
                                              uint8_t *dst_line_y, \
                                              uint8_t *dst_line_z, \
                                              uint8_t *dst_line_a, \
                                              int x, \
                                              otype xo, \
                                              otype yo, \
                                              otype zo) \
    { \
        int xd_x = fc->dst_width_offset_x[x]; \
        int xd_y = fc->dst_width_offset_y[x]; \
        int xd_z = fc->dst_width_offset_z[x]; \
        int xd_a = fc->dst_width_offset_a[x]; \
        \
        otype *xo_ = (otype *)(dst_line_x + xd_x); \
        otype *yo_ = (otype *)(dst_line_y + xd_y); \
        otype *zo_ = (otype *)(dst_line_z + xd_z); \
        otype *ao_ = (otype *)(dst_line_a + xd_a); \
        \
        *xo_ = (*xo_ & (otype)(fc->mask_xo)) | ((otype)(xo) << fc->xo_shift); \
        *yo_ = (*yo_ & (otype)(fc->mask_yo)) | ((otype)(yo) << fc->yo_shift); \
        *zo_ = (*zo_ & (otype)(fc->mask_zo)) | ((otype)(zo) << fc->zo_shift); \
        *ao_ = *ao_ | (otype)(fc->alpha_mask); \
    }

AKVCAM_WRITE1(uint8_t)
AKVCAM_WRITE1(uint16_t)
AKVCAM_WRITE1(uint32_t)

AKVCAM_WRITE1A_AO(uint8_t)
AKVCAM_WRITE1A_AO(uint16_t)
AKVCAM_WRITE1A_AO(uint32_t)

AKVCAM_WRITE1A(uint8_t)
AKVCAM_WRITE1A(uint16_t)
AKVCAM_WRITE1A(uint32_t)

AKVCAM_WRITE3(uint8_t)
AKVCAM_WRITE3(uint16_t)
AKVCAM_WRITE3(uint32_t)

AKVCAM_WRITE3A_AO(uint8_t)
AKVCAM_WRITE3A_AO(uint16_t)
AKVCAM_WRITE3A_AO(uint32_t)

AKVCAM_WRITE3A(uint8_t)
AKVCAM_WRITE3A(uint16_t)
AKVCAM_WRITE3A(uint32_t)

/* Integral image functions */

#define AKVCAM_INTEGRAL_IMAGE_1(itype) \
    static inline void akvcam_integral_image_1_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                       akvcam_frame_ct src) \
    { \
        uint64_t *dst_line_x   = fc->integral_image_data_x; \
        uint64_t *dst_line_x_1 = dst_line_x + fc->input_width_1; \
        int y; \
        \
        for (y = 0; y < fc->input_height; ++y) { \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, y) + fc->xi_offset; \
            uint64_t sum_x = 0; \
            int x; \
            \
            for (x = 0; x < fc->input_width; ++x) { \
                int xs_x = fc->dl_src_width_offset_x[x]; \
                itype xi = *(const itype *)(src_line_x + xs_x); \
                int x_1; \
                \
                if (fc->from_endian != __BYTE_ORDER__) \
                    xi = akvcam_swap_bytes(itype, xi); \
                \
                sum_x += (xi >> fc->xi_shift) & fc->max_xi; \
                x_1 = x + 1; \
                dst_line_x_1[x_1] = sum_x + dst_line_x[x_1]; \
            } \
            \
            dst_line_x   += fc->input_width_1; \
            dst_line_x_1 += fc->input_width_1; \
        } \
    }

#define AKVCAM_INTEGRAL_IMAGE_1A(itype) \
    static inline void akvcam_integral_image_1a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                        akvcam_frame_ct src) \
    { \
        uint64_t *dst_line_x   = fc->integral_image_data_x; \
        uint64_t *dst_line_a   = fc->integral_image_data_a; \
        uint64_t *dst_line_x_1 = dst_line_x + fc->input_width_1; \
        uint64_t *dst_line_a_1 = dst_line_a + fc->input_width_1; \
        int y; \
        \
        for (y = 0; y < fc->input_height; ++y) { \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, y) + fc->xi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, y) + fc->ai_offset; \
            uint64_t sum_x = 0; \
            uint64_t sum_a = 0; \
            int x; \
            \
            for (x = 0; x < fc->input_width; ++x) { \
                int xs_x = fc->dl_src_width_offset_x[x]; \
                int xs_a = fc->dl_src_width_offset_a[x]; \
                itype xi = *(const itype *)(src_line_x + xs_x); \
                itype ai = *(const itype *)(src_line_a + xs_a); \
                int x_1; \
                \
                if (fc->from_endian != __BYTE_ORDER__) { \
                    xi = akvcam_swap_bytes(itype, xi); \
                    ai = akvcam_swap_bytes(itype, ai); \
                } \
                \
                sum_x += (xi >> fc->xi_shift) & fc->max_xi; \
                sum_a += (ai >> fc->ai_shift) & fc->max_ai; \
                x_1 = x + 1; \
                dst_line_x_1[x_1] = sum_x + dst_line_x[x_1]; \
                dst_line_a_1[x_1] = sum_a + dst_line_a[x_1]; \
            } \
            \
            dst_line_x   += fc->input_width_1; \
            dst_line_a   += fc->input_width_1; \
            dst_line_x_1 += fc->input_width_1; \
            dst_line_a_1 += fc->input_width_1; \
        } \
    }

#define AKVCAM_INTEGRAL_IMAGE_3(itype) \
    static inline void akvcam_integral_image_3_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                       akvcam_frame_ct src) \
    { \
        uint64_t *dst_line_x   = fc->integral_image_data_x; \
        uint64_t *dst_line_y   = fc->integral_image_data_y; \
        uint64_t *dst_line_z   = fc->integral_image_data_z; \
        uint64_t *dst_line_x_1 = dst_line_x + fc->input_width_1; \
        uint64_t *dst_line_y_1 = dst_line_y + fc->input_width_1; \
        uint64_t *dst_line_z_1 = dst_line_z + fc->input_width_1; \
        int y; \
        \
        for (y = 0; y < fc->input_height; ++y) { \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, y) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, y) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, y) + fc->zi_offset; \
            uint64_t sum_x = 0; \
            uint64_t sum_y = 0; \
            uint64_t sum_z = 0; \
            int x; \
            \
            for (x = 0; x < fc->input_width; ++x) { \
                int xs_x = fc->dl_src_width_offset_x[x]; \
                int xs_y = fc->dl_src_width_offset_y[x]; \
                int xs_z = fc->dl_src_width_offset_z[x]; \
                itype xi = *(const itype *)(src_line_x + xs_x); \
                itype yi = *(const itype *)(src_line_y + xs_y); \
                itype zi = *(const itype *)(src_line_z + xs_z); \
                int x_1; \
                \
                if (fc->from_endian != __BYTE_ORDER__) { \
                    xi = akvcam_swap_bytes(itype, xi); \
                    yi = akvcam_swap_bytes(itype, yi); \
                    zi = akvcam_swap_bytes(itype, zi); \
                } \
                \
                sum_x += (xi >> fc->xi_shift) & fc->max_xi; \
                sum_y += (yi >> fc->yi_shift) & fc->max_yi; \
                sum_z += (zi >> fc->zi_shift) & fc->max_zi; \
                x_1 = x + 1; \
                dst_line_x_1[x_1] = sum_x + dst_line_x[x_1]; \
                dst_line_y_1[x_1] = sum_y + dst_line_y[x_1]; \
                dst_line_z_1[x_1] = sum_z + dst_line_z[x_1]; \
            } \
            \
            dst_line_x   += fc->input_width_1; \
            dst_line_y   += fc->input_width_1; \
            dst_line_z   += fc->input_width_1; \
            dst_line_x_1 += fc->input_width_1; \
            dst_line_y_1 += fc->input_width_1; \
            dst_line_z_1 += fc->input_width_1; \
        } \
    }

#define AKVCAM_INTEGRAL_IMAGE_3A(itype) \
    static inline void akvcam_integral_image_3a_##itype(akvcam_frame_convert_parameters_ct fc, \
                                                        akvcam_frame_ct src) \
    { \
        uint64_t *dst_line_x   = fc->integral_image_data_x; \
        uint64_t *dst_line_y   = fc->integral_image_data_y; \
        uint64_t *dst_line_z   = fc->integral_image_data_z; \
        uint64_t *dst_line_a   = fc->integral_image_data_a; \
        uint64_t *dst_line_x_1 = dst_line_x + fc->input_width_1; \
        uint64_t *dst_line_y_1 = dst_line_y + fc->input_width_1; \
        uint64_t *dst_line_z_1 = dst_line_z + fc->input_width_1; \
        uint64_t *dst_line_a_1 = dst_line_a + fc->input_width_1; \
        int y; \
        \
        for (y = 0; y < fc->input_height; ++y) { \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, y) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, y) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, y) + fc->zi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, y) + fc->ai_offset; \
            uint64_t sum_x = 0; \
            uint64_t sum_y = 0; \
            uint64_t sum_z = 0; \
            uint64_t sum_a = 0; \
            int x; \
            \
            for (x = 0; x < fc->input_width; ++x) { \
                int xs_x = fc->dl_src_width_offset_x[x]; \
                int xs_y = fc->dl_src_width_offset_y[x]; \
                int xs_z = fc->dl_src_width_offset_z[x]; \
                int xs_a = fc->dl_src_width_offset_a[x]; \
                itype xi = *(const itype *)(src_line_x + xs_x); \
                itype yi = *(const itype *)(src_line_y + xs_y); \
                itype zi = *(const itype *)(src_line_z + xs_z); \
                itype ai = *(const itype *)(src_line_a + xs_a); \
                int x_1; \
                \
                if (fc->from_endian != __BYTE_ORDER__) { \
                    xi = akvcam_swap_bytes(itype, xi); \
                    yi = akvcam_swap_bytes(itype, yi); \
                    zi = akvcam_swap_bytes(itype, zi); \
                    ai = akvcam_swap_bytes(itype, ai); \
                } \
                \
                sum_x += (xi >> fc->xi_shift) & fc->max_xi; \
                sum_y += (yi >> fc->yi_shift) & fc->max_yi; \
                sum_z += (zi >> fc->zi_shift) & fc->max_zi; \
                sum_a += (ai >> fc->ai_shift) & fc->max_ai; \
                x_1 = x + 1; \
                dst_line_x_1[x_1] = sum_x + dst_line_x[x_1]; \
                dst_line_y_1[x_1] = sum_y + dst_line_y[x_1]; \
                dst_line_z_1[x_1] = sum_z + dst_line_z[x_1]; \
                dst_line_a_1[x_1] = sum_a + dst_line_a[x_1]; \
            } \
            \
            dst_line_x   += fc->input_width_1; \
            dst_line_y   += fc->input_width_1; \
            dst_line_z   += fc->input_width_1; \
            dst_line_a   += fc->input_width_1; \
            dst_line_x_1 += fc->input_width_1; \
            dst_line_y_1 += fc->input_width_1; \
            dst_line_z_1 += fc->input_width_1; \
            dst_line_a_1 += fc->input_width_1; \
        } \
    }

#define akvcam_integral_image_1(itype, fc, src) \
    akvcam_integral_image_1_##itype(fc, src)
#define akvcam_integral_image_1a(itype, fc, src) \
    akvcam_integral_image_1a_##itype(fc, src)
#define akvcam_integral_image_3(itype, fc, src) \
    akvcam_integral_image_3_##itype(fc, src)
#define akvcam_integral_image_3a(itype, fc, src) \
    akvcam_integral_image_3a_##itype(fc, src)

AKVCAM_INTEGRAL_IMAGE_1(uint8_t)
AKVCAM_INTEGRAL_IMAGE_1(uint16_t)
AKVCAM_INTEGRAL_IMAGE_1(uint32_t)

AKVCAM_INTEGRAL_IMAGE_1A(uint8_t)
AKVCAM_INTEGRAL_IMAGE_1A(uint16_t)
AKVCAM_INTEGRAL_IMAGE_1A(uint32_t)

AKVCAM_INTEGRAL_IMAGE_3(uint8_t)
AKVCAM_INTEGRAL_IMAGE_3(uint16_t)
AKVCAM_INTEGRAL_IMAGE_3(uint32_t)

AKVCAM_INTEGRAL_IMAGE_3A(uint8_t)
AKVCAM_INTEGRAL_IMAGE_3A(uint16_t)
AKVCAM_INTEGRAL_IMAGE_3A(uint32_t)

/* Fast conversion functions */

// Conversion functions for 3 components to 3 components formats
#define AKVCAM_CONVERT_3TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_3to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                               akvcam_frame_ct src, \
                                                                               akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3_##itype(fc, \
                                     src_line_x, \
                                     src_line_y, \
                                     src_line_z, \
                                     x, \
                                     &xi, \
                                     &yi, \
                                     &zi); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, \
                                      dst_line_y, \
                                      dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3to3(akvcam_frame_convert_parameters_ct fc,
                                                                    akvcam_frame_ct src,
                                                                    akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_3TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_3to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3_##itype(fc, \
                                     src_line_x, \
                                     src_line_y, \
                                     src_line_z, \
                                     x, \
                                     &xi, \
                                     &yi, \
                                     &zi); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, \
                                       dst_line_y, \
                                       dst_line_z, \
                                       dst_line_a, \
                                       x, \
                                       (otype)(xo), \
                                       (otype)(yo), \
                                       (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3to3a(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[i]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_3ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_3ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3a_##itype(fc, \
                                      src_line_x, \
                                      src_line_y, \
                                      src_line_z, \
                                      src_line_a, \
                                      x, \
                                      &xi, \
                                      &yi, \
                                      &zi, \
                                      &ai); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, \
                                      dst_line_y, \
                                      dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3ato3(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];
            uint8_t ai = src_line_a[fc->src_width_offset_a[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_3ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_3ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3a_##itype(fc, \
                                      src_line_x, \
                                      src_line_y, \
                                      src_line_z, \
                                      src_line_a, \
                                      x, \
                                      &xi, \
                                      &yi, \
                                      &zi, \
                                      &ai); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, \
                                          dst_line_y, \
                                          dst_line_z, \
                                          dst_line_a, \
                                          x, \
                                          (otype)(xo), \
                                          (otype)(yo), \
                                          (otype)(zo), \
                                          (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];
            uint8_t ai = src_line_a[fc->src_width_offset_a[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[i]] = ai;
        }
    }
}

AKVCAM_CONVERT_3TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_3TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_3TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_3TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_3TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_3TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_3TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_3TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_3TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_3TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_3TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_3TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_3TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_3TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_3TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_3TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_3TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_3TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_3ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_3ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_3ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_3ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_3ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_3ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_3ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_3ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_3ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_3ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_3ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_3ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_3ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_3ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_3ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_3ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_3ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_3ATO3A(uint32_t, uint32_t)

// Conversion functions for 3 components to 3 components formats
// (same color space)

#define AKVCAM_CONVERT_V3TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_v3to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3_##itype(fc, \
                                     src_line_x, \
                                     src_line_y, \
                                     src_line_z, \
                                     x, \
                                     &xi, \
                                     &yi, \
                                     &zi); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, \
                                      dst_line_y, \
                                      dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_v3to3(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            dst_line_x[fc->dst_width_offset_x[x]] = src_line_x[fc->src_width_offset_x[x]];
            dst_line_y[fc->dst_width_offset_y[x]] = src_line_y[fc->src_width_offset_y[x]];
            dst_line_z[fc->dst_width_offset_z[x]] = src_line_z[fc->src_width_offset_z[x]];
        }
    }
}

#define AKVCAM_CONVERT_V3TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_v3to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3_##itype(fc, \
                                     src_line_x, \
                                     src_line_y, \
                                     src_line_z, \
                                     x, \
                                     &xi, \
                                     &yi, \
                                     &zi); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, \
                                       dst_line_y, \
                                       dst_line_z, \
                                       dst_line_a, \
                                       x, \
                                       (otype)(xo), \
                                       (otype)(yo), \
                                       (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_v3to3a(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            dst_line_x[fc->dst_width_offset_x[x]] = src_line_x[fc->src_width_offset_x[x]];
            dst_line_y[fc->dst_width_offset_y[x]] = src_line_y[fc->src_width_offset_y[x]];
            dst_line_z[fc->dst_width_offset_z[x]] = src_line_z[fc->src_width_offset_z[x]];
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_V3ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_v3ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3a_##itype(fc, \
                                      src_line_x, \
                                      src_line_y, \
                                      src_line_z, \
                                      src_line_a, \
                                      x, \
                                      &xi, \
                                      &yi, \
                                      &zi, \
                                      &ai); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, \
                                      dst_line_y, \
                                      dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_v3ato3(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            int64_t xi = src_line_x[fc->src_width_offset_x[i]];
            int64_t yi = src_line_y[fc->src_width_offset_y[i]];
            int64_t zi = src_line_z[fc->src_width_offset_z[i]];
            uint8_t ai = src_line_a[fc->src_width_offset_a[i]];

            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xi, &yi, &zi);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xi);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yi);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zi);
        }
    }
}

#define AKVCAM_CONVERT_V3ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_v3ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read3a_##itype(fc, \
                                      src_line_x, \
                                      src_line_y, \
                                      src_line_z, \
                                      src_line_a, \
                                      x, \
                                      &xi, \
                                      &yi, \
                                      &zi, \
                                      &ai); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, \
                                          dst_line_y, \
                                          dst_line_z, \
                                          dst_line_a, \
                                          x, \
                                          (otype)(xo), \
                                          (otype)(yo), \
                                          (otype)(zo), \
                                          (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_v3ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            dst_line_x[fc->dst_width_offset_x[x]] = src_line_x[fc->src_width_offset_x[x]];
            dst_line_y[fc->dst_width_offset_y[x]] = src_line_y[fc->src_width_offset_y[x]];
            dst_line_z[fc->dst_width_offset_z[x]] = src_line_z[fc->src_width_offset_z[x]];
            dst_line_a[fc->dst_width_offset_a[x]] = src_line_a[fc->src_width_offset_a[x]];
        }
    }
}

AKVCAM_CONVERT_V3TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_V3TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_V3TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_V3TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_V3TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_V3TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_V3TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_V3TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_V3TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_V3TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_V3TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_V3TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_V3TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_V3TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_V3TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_V3TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_V3TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_V3TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_V3ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_V3ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_V3ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_V3ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_V3ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_V3ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_V3ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_V3ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_V3ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_V3ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_V3ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_V3ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_V3ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_V3ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_V3ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_V3ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_V3ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_V3ATO3A(uint32_t, uint32_t)

// Conversion functions for 3 components to 1 component formats

#define AKVCAM_CONVERT_3TO1(itype, otype) \
    static inline void akvcam_converter_private_convert_3to1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                               akvcam_frame_ct src, \
                                                                               akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read3_##itype(fc, \
                                     src_line_x, \
                                     src_line_y, \
                                     src_line_z, \
                                     x, \
                                     &xi, \
                                     &yi, \
                                     &zi); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1_##otype(fc, \
                                      dst_line_x, \
                                      x, \
                                      (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3to1(akvcam_frame_convert_parameters_ct fc,
                                                                    akvcam_frame_ct src,
                                                                    akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];

            int64_t xo = 0;

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
        }
    }
}

#define AKVCAM_CONVERT_3TO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_3to1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read3_##itype(fc, \
                                     src_line_x, \
                                     src_line_y, \
                                     src_line_z, \
                                     x, \
                                     &xi, \
                                     &yi, \
                                     &zi); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1a_##otype(fc, \
                                       dst_line_x, \
                                       dst_line_a, \
                                       x, \
                                       (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3to1a(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];

            int64_t xo = 0;

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[i]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_3ATO1(itype, otype) \
    static inline void akvcam_converter_private_convert_3ato1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read3a_##itype(fc, \
                                      src_line_x, \
                                      src_line_y, \
                                      src_line_z, \
                                      src_line_a, \
                                      x, \
                                      &xi, \
                                      &yi, \
                                      &zi, \
                                      &ai); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo); \
                \
                akvcam_write1_##otype(fc, \
                                      dst_line_x, \
                                      x, \
                                      (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3ato1(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];
            uint8_t ai = src_line_a[fc->src_width_offset_a[i]];

            int64_t xo = 0;

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);
            akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
        }
    }
}

#define AKVCAM_CONVERT_3ATO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_3ato1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset; \
            const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read3a_##itype(fc, \
                                      src_line_x, \
                                      src_line_y, \
                                      src_line_z, \
                                      src_line_a, \
                                      x, \
                                      &xi, \
                                      &yi, \
                                      &zi, \
                                      &ai); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1a_ao_##otype(fc, \
                                          dst_line_x, \
                                          dst_line_a, \
                                          x, \
                                          (otype)(xo), \
                                          (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_3ato1a(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_y = akvcam_frame_const_line(src, fc->plane_yi, ys) + fc->yi_offset;
        const uint8_t *src_line_z = akvcam_frame_const_line(src, fc->plane_zi, ys) + fc->zi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t yi = src_line_y[fc->src_width_offset_y[i]];
            uint8_t zi = src_line_z[fc->src_width_offset_z[i]];
            uint8_t ai = src_line_a[fc->src_width_offset_a[i]];

            int64_t xo = 0;

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[i]] = ai;
        }
    }
}

AKVCAM_CONVERT_3TO1(uint8_t, uint8_t)
AKVCAM_CONVERT_3TO1(uint8_t, uint16_t)
AKVCAM_CONVERT_3TO1(uint8_t, uint32_t)
AKVCAM_CONVERT_3TO1(uint16_t, uint8_t)
AKVCAM_CONVERT_3TO1(uint16_t, uint16_t)
AKVCAM_CONVERT_3TO1(uint16_t, uint32_t)
AKVCAM_CONVERT_3TO1(uint32_t, uint8_t)
AKVCAM_CONVERT_3TO1(uint32_t, uint16_t)
AKVCAM_CONVERT_3TO1(uint32_t, uint32_t)

AKVCAM_CONVERT_3TO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_3TO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_3TO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_3TO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_3TO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_3TO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_3TO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_3TO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_3TO1A(uint32_t, uint32_t)

AKVCAM_CONVERT_3ATO1(uint8_t, uint8_t)
AKVCAM_CONVERT_3ATO1(uint8_t, uint16_t)
AKVCAM_CONVERT_3ATO1(uint8_t, uint32_t)
AKVCAM_CONVERT_3ATO1(uint16_t, uint8_t)
AKVCAM_CONVERT_3ATO1(uint16_t, uint16_t)
AKVCAM_CONVERT_3ATO1(uint16_t, uint32_t)
AKVCAM_CONVERT_3ATO1(uint32_t, uint8_t)
AKVCAM_CONVERT_3ATO1(uint32_t, uint16_t)
AKVCAM_CONVERT_3ATO1(uint32_t, uint32_t)

AKVCAM_CONVERT_3ATO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_3ATO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_3ATO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_3ATO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_3ATO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_3ATO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_3ATO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_3ATO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_3ATO1A(uint32_t, uint32_t)

// Conversion functions for 1 component to 3 components formats

#define AKVCAM_CONVERT_1TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_1to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                               akvcam_frame_ct src, \
                                                                               akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read1_##itype(fc, src_line_x, x, &xi); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, \
                                      dst_line_y, \
                                      dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1to3(akvcam_frame_convert_parameters_ct fc,
                                                                    akvcam_frame_ct src,
                                                                    akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_1TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_1to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read1_##itype(fc, src_line_x, x, &xi); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, \
                                       dst_line_y, \
                                       dst_line_z, \
                                       dst_line_a, \
                                       x, \
                                       (otype)(xo), \
                                       (otype)(yo), \
                                       (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1to3a(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[i]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_1ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_1ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read1a_##itype(fc, src_line_x, src_line_a, x, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, \
                                      dst_line_y, \
                                      dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1ato3(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t ai = src_line_a[fc->src_width_offset_a[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_1ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_1ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read1a_##itype(fc, src_line_x, src_line_a, x, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, \
                                          dst_line_y, \
                                          dst_line_z, \
                                          dst_line_a, \
                                          x, \
                                          (otype)(xo), \
                                          (otype)(yo), \
                                          (otype)(zo), \
                                          (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            uint8_t xi = src_line_x[fc->src_width_offset_x[i]];
            uint8_t ai = src_line_a[fc->src_width_offset_a[i]];

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[i]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[i]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[i]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[i]] = ai;
        }
    }
}

AKVCAM_CONVERT_1TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_1TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_1TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_1TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_1TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_1TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_1TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_1TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_1TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_1TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_1TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_1TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_1TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_1TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_1TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_1TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_1TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_1TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_1ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_1ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_1ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_1ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_1ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_1ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_1ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_1ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_1ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_1ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_1ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_1ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_1ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_1ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_1ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_1ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_1ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_1ATO3A(uint32_t, uint32_t)

// Conversion functions for 1 component to 1 component formats

#define AKVCAM_CONVERT_1TO1(itype, otype) \
    static inline void akvcam_converter_private_convert_1to1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                               akvcam_frame_ct src, \
                                                                               akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read1_##itype(fc, src_line_x, x, &xi); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1to1(akvcam_frame_convert_parameters_ct fc,
                                                                    akvcam_frame_ct src,
                                                                    akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x)
            dst_line_x[fc->dst_width_offset_x[x]] = src_line_x[fc->src_width_offset_x[x]];
    }
}

#define AKVCAM_CONVERT_1TO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_1to1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read1_##itype(fc, src_line_x, x, &xi); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_write1a_##otype(fc, dst_line_x, dst_line_a, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1to1a(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            dst_line_x[fc->dst_width_offset_x[x]] = src_line_x[fc->src_width_offset_x[x]];
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_1ATO1(itype, otype) \
    static inline void akvcam_converter_private_convert_1ato1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read1a_##itype(fc, src_line_x, src_line_a, x, &xi, &ai); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo); \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1ato1(akvcam_frame_convert_parameters_ct fc,
                                                                     akvcam_frame_ct src,
                                                                     akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int i;

        for (i = fc->xmin; i < fc->xmax; ++i) {
            dst_line_x[fc->dst_width_offset_x[i]] =
                (uint8_t)((uint16_t)(src_line_x[fc->src_width_offset_x[i]])
                * (uint16_t)(src_line_a[fc->src_width_offset_a[i]])
                / 255);
        }
    }
}

#define AKVCAM_CONVERT_1ATO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_1ato1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys = fc->src_height[y]; \
            \
            const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset; \
            const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read1a_##itype(fc, src_line_x, src_line_a, x, &xi, &ai); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_write1a_ao_##otype(fc, dst_line_x, dst_line_a, x, \
                (otype)(xo), (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_1ato1a(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys = fc->src_height[y];

        const uint8_t *src_line_x = akvcam_frame_const_line(src, fc->plane_xi, ys) + fc->xi_offset;
        const uint8_t *src_line_a = akvcam_frame_const_line(src, fc->plane_ai, ys) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            dst_line_x[fc->dst_width_offset_x[x]] = src_line_x[fc->src_width_offset_x[x]];
            dst_line_a[fc->dst_width_offset_a[x]] = src_line_a[fc->src_width_offset_a[x]];
        }
    }
}

AKVCAM_CONVERT_1TO1(uint8_t, uint8_t)
AKVCAM_CONVERT_1TO1(uint8_t, uint16_t)
AKVCAM_CONVERT_1TO1(uint8_t, uint32_t)
AKVCAM_CONVERT_1TO1(uint16_t, uint8_t)
AKVCAM_CONVERT_1TO1(uint16_t, uint16_t)
AKVCAM_CONVERT_1TO1(uint16_t, uint32_t)
AKVCAM_CONVERT_1TO1(uint32_t, uint8_t)
AKVCAM_CONVERT_1TO1(uint32_t, uint16_t)
AKVCAM_CONVERT_1TO1(uint32_t, uint32_t)

AKVCAM_CONVERT_1TO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_1TO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_1TO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_1TO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_1TO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_1TO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_1TO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_1TO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_1TO1A(uint32_t, uint32_t)

AKVCAM_CONVERT_1ATO1(uint8_t, uint8_t)
AKVCAM_CONVERT_1ATO1(uint8_t, uint16_t)
AKVCAM_CONVERT_1ATO1(uint8_t, uint32_t)
AKVCAM_CONVERT_1ATO1(uint16_t, uint8_t)
AKVCAM_CONVERT_1ATO1(uint16_t, uint16_t)
AKVCAM_CONVERT_1ATO1(uint16_t, uint32_t)
AKVCAM_CONVERT_1ATO1(uint32_t, uint8_t)
AKVCAM_CONVERT_1ATO1(uint32_t, uint16_t)
AKVCAM_CONVERT_1ATO1(uint32_t, uint32_t)

AKVCAM_CONVERT_1ATO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_1ATO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_1ATO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_1ATO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_1ATO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_1ATO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_1ATO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_1ATO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_1ATO1A(uint32_t, uint32_t)

/* Linear downscaling conversion funtions */

// Conversion functions for 3 components to 3 components formats (downscale)

#define AKVCAM_CONVERT_DL3TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, kdl, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3to3(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3_uint8_t(fc,
                                    src_line_x, src_line_y, src_line_z,
                                    src_line_x_1, src_line_y_1, src_line_z_1,
                                    x, kdl, &xi, &yi, &zi);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL3TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, kdl, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                       x, \
                                       (otype)(xo), \
                                       (otype)(yo), \
                                       (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3to3a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3_uint8_t(fc,
                                    src_line_x, src_line_y, src_line_z,
                                    src_line_x_1, src_line_y_1, src_line_z_1,
                                    x, kdl, &xi, &yi, &zi);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL3ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, kdl, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3ato3(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3a_uint8_t(fc,
                                     src_line_x, src_line_y, src_line_z, src_line_a,
                                     src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                                     x, kdl, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL3ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, kdl, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                          x, \
                                          (otype)(xo), \
                                          (otype)(yo), \
                                          (otype)(zo), \
                                          (otype)(ai)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3a_uint8_t(fc,
                                     src_line_x, src_line_y, src_line_z, src_line_a,
                                     src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                                     x, kdl, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }

        kdl += fc->xmax;
    }
}

AKVCAM_CONVERT_DL3TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_DL3TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_DL3ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_DL3ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3ATO3A(uint32_t, uint32_t)

// Conversion functions for 3 components to 3 components formats
// (same color space)

#define AKVCAM_CONVERT_DLV3TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_dlv3to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, kdl, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dlv3to3(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3_uint8_t(fc,
                                    src_line_x, src_line_y, src_line_z,
                                    src_line_x_1, src_line_y_1, src_line_z_1,
                                    x, kdl, &xi, &yi, &zi);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DLV3TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_dlv3to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, kdl, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                       x, \
                                       (otype)(xo), \
                                       (otype)(yo), \
                                       (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dlv3to3a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3_uint8_t(fc,
                                    src_line_x, src_line_y, src_line_z,
                                    src_line_x_1, src_line_y_1, src_line_z_1,
                                    x, kdl, &xi, &yi, &zi);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DLV3ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_dlv3ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, kdl, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, (otype)(xo), (otype)(yo), (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dlv3ato3(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3a_uint8_t(fc,
                                     src_line_x, src_line_y, src_line_z, src_line_a,
                                     src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                                     x, kdl, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DLV3ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_dlv3ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                    akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, kdl, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                          x, \
                                          (otype)(xo), (otype)(yo), (otype)(zo), (otype)(ai)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dlv3ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                         akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl3a_uint8_t(fc,
                                     src_line_x, src_line_y, src_line_z, src_line_a,
                                     src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                                     x, kdl, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }

        kdl += fc->xmax;
    }
}

AKVCAM_CONVERT_DLV3TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_DLV3TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_DLV3TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_DLV3TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_DLV3TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_DLV3TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_DLV3TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_DLV3TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_DLV3TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_DLV3TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_DLV3TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_DLV3TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_DLV3TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_DLV3TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_DLV3TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_DLV3TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_DLV3TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_DLV3TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_DLV3ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_DLV3ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_DLV3ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_DLV3ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_DLV3ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_DLV3ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_DLV3ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_DLV3ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_DLV3ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_DLV3ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_DLV3ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_DLV3ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_DLV3ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_DLV3ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_DLV3ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_DLV3ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_DLV3ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_DLV3ATO3A(uint32_t, uint32_t)

// Conversion functions for 3 components to 1 components formats

#define AKVCAM_CONVERT_DL3TO1(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3to1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_dl3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, kdl, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3to1(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            int64_t xo = 0;

            akvcam_read_dl3_uint8_t(fc,
                                    src_line_x, src_line_y, src_line_z,
                                    src_line_x_1, src_line_y_1, src_line_z_1,
                                    x, kdl, &xi, &yi, &zi);
            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL3TO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3to1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_dl3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, kdl, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1a_##otype(fc, dst_line_x, dst_line_a, x, (otype)(xo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3to1a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;

            akvcam_read_dl3_uint8_t(fc,
                                    src_line_x, src_line_y, src_line_z,
                                    src_line_x_1, src_line_y_1, src_line_z_1,
                                    x, kdl, &xi, &yi, &zi);

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL3ATO1(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3ato1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_dl3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, kdl, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo); \
                \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3ato1(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;

            akvcam_read_dl3a_uint8_t(fc,
                                     src_line_x, src_line_y, src_line_z, src_line_a,
                                     src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                                     x, kdl, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);
            akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL3ATO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl3ato1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset; \
            const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset; \
            const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_dl3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, kdl, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1a_ao_##otype(fc, dst_line_x, dst_line_a, x, (otype)(xo), (otype)(ai)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl3ato1a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_y   = fc->integral_image_data_y + y_offset;
        const uint64_t *src_line_z   = fc->integral_image_data_z + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_y_1 = fc->integral_image_data_y + y1_offset;
        const uint64_t *src_line_z_1 = fc->integral_image_data_z + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;

            akvcam_read_dl3a_uint8_t(fc,
                                     src_line_x, src_line_y, src_line_z, src_line_a,
                                     src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                                     x, kdl, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }

        kdl += fc->xmax;
    }
}

AKVCAM_CONVERT_DL3TO1(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3TO1(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3TO1(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3TO1(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3TO1(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3TO1(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3TO1(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3TO1(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3TO1(uint32_t, uint32_t)

AKVCAM_CONVERT_DL3TO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3TO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3TO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3TO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3TO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3TO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3TO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3TO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3TO1A(uint32_t, uint32_t)

AKVCAM_CONVERT_DL3ATO1(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3ATO1(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3ATO1(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3ATO1(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3ATO1(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3ATO1(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3ATO1(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3ATO1(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3ATO1(uint32_t, uint32_t)

AKVCAM_CONVERT_DL3ATO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_DL3ATO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_DL3ATO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_DL3ATO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL3ATO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL3ATO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL3ATO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL3ATO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL3ATO1A(uint32_t, uint32_t)

// Conversion functions for 1 components to 3 components formats

#define AKVCAM_CONVERT_DL1TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl1_##itype(fc, \
                                        src_line_x, src_line_x_1, \
                                        x, kdl, &xi); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), (otype)(yo), (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1to3(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl1_uint8_t(fc,
                                    src_line_x, src_line_x_1,
                                    x, kdl, &xi);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL1TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl1_##itype(fc, \
                                        src_line_x, src_line_x_1, \
                                        x, kdl, &xi); \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                       x, \
                                       (otype)(xo), (otype)(yo), (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1to3a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl1_uint8_t(fc,
                                    src_line_x, src_line_x_1,
                                    x, kdl, &xi);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL1ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl1a_##itype(fc, \
                                         src_line_x, src_line_a, \
                                         src_line_x_1, src_line_a_1, \
                                         x, kdl, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_3_3(fc->color_convert, xo, yo, zo, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), (otype)(yo), (otype)(zo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1ato3(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl1a_uint8_t(fc,
                                     src_line_x, src_line_a,
                                     src_line_x_1, src_line_a_1,
                                     x, kdl, &xi, &ai);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_3_3(fc->color_convert, xo, yo, zo, ai, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL1ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_dl1a_##itype(fc, \
                                         src_line_x, src_line_a, \
                                         src_line_x_1, src_line_a_1, \
                                         x, kdl, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                          x, \
                                          (otype)(xo), (otype)(yo), (otype)(zo), \
                                          (otype)(ai)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_dl1a_uint8_t(fc,
                                     src_line_x, src_line_a,
                                     src_line_x_1, src_line_a_1,
                                     x, kdl, &xi, &ai);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }

        kdl += fc->xmax;
    }
}

AKVCAM_CONVERT_DL1TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_DL1TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_DL1TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_DL1TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_DL1TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_DL1TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_DL1TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_DL1TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_DL1ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_DL1ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_DL1ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_DL1ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_DL1ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_DL1ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_DL1ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_DL1ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1ATO3A(uint32_t, uint32_t)

// Conversion functions for 1 components to 1 components formats

#define AKVCAM_CONVERT_DL1TO1(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1to1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                int64_t xo = 0; \
                akvcam_read_dl1_##itype(fc, \
                                        src_line_x, src_line_x_1, \
                                        x, kdl, &xi); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1to1(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            int64_t xo = 0;
            akvcam_read_dl1_uint8_t(fc,
                                    src_line_x, src_line_x_1,
                                    x, kdl, &xi);
            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);
            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL1TO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1to1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                int64_t xo = 0; \
                akvcam_read_dl1_##itype(fc, \
                                        src_line_x, src_line_x_1, \
                                        x, kdl, &xi); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_write1a_##otype(fc, dst_line_x, dst_line_a, x, (otype)(xo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1to1a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            int64_t xo = 0;
            akvcam_read_dl1_uint8_t(fc,
                                    src_line_x, src_line_x_1,
                                    x, kdl, &xi);
            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);
            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL1ATO1(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1ato1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                int64_t xo = 0; \
                akvcam_read_dl1a_##itype(fc, \
                                         src_line_x, src_line_a, \
                                         src_line_x_1, src_line_a_1, \
                                         x, kdl, &xi, &ai); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo); \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1ato1(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;
            int64_t xo = 0;

            akvcam_read_dl1a_uint8_t(fc,
                                     src_line_x, src_line_a,
                                     src_line_x_1, src_line_a_1,
                                     x, kdl, &xi, &ai);

            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);
            akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo);
            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }

        kdl += fc->xmax;
    }
}

#define AKVCAM_CONVERT_DL1ATO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_dl1ato1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_t dst) \
    { \
        const uint64_t *kdl = fc->kdl; \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int y_offset  = fc->src_height_dl_offset[y]; \
            int y1_offset = fc->src_height_dl_offset_1[y]; \
            \
            const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset; \
            const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset; \
            const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset; \
            const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                int64_t xo = 0; \
                akvcam_read_dl1a_##itype(fc, \
                src_line_x, src_line_a, \
                src_line_x_1, src_line_a_1, \
                x, kdl, &xi, &ai); \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_write1a_ao_##otype(fc, dst_line_x, dst_line_a, x, (otype)(xo), (otype)(ai)); \
            } \
            \
            kdl += fc->xmax; \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_dl1ato1a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_t dst)
{
    const uint64_t *kdl = fc->kdl;
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int y_offset  = fc->src_height_dl_offset[y];
        int y1_offset = fc->src_height_dl_offset_1[y];

        const uint64_t *src_line_x   = fc->integral_image_data_x + y_offset;
        const uint64_t *src_line_a   = fc->integral_image_data_a + y_offset;
        const uint64_t *src_line_x_1 = fc->integral_image_data_x + y1_offset;
        const uint64_t *src_line_a_1 = fc->integral_image_data_a + y1_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;
            int64_t xo = 0;
            akvcam_read_dl1a_uint8_t(fc,
                                     src_line_x, src_line_a,
                                     src_line_x_1, src_line_a_1,
                                     x, kdl, &xi, &ai);
            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);
            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }

        kdl += fc->xmax;
    }
}

AKVCAM_CONVERT_DL1TO1(uint8_t,  uint8_t)
AKVCAM_CONVERT_DL1TO1(uint8_t,  uint16_t)
AKVCAM_CONVERT_DL1TO1(uint8_t,  uint32_t)
AKVCAM_CONVERT_DL1TO1(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1TO1(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1TO1(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1TO1(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1TO1(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1TO1(uint32_t, uint32_t)

AKVCAM_CONVERT_DL1TO1A(uint8_t,  uint8_t)
AKVCAM_CONVERT_DL1TO1A(uint8_t,  uint16_t)
AKVCAM_CONVERT_DL1TO1A(uint8_t,  uint32_t)
AKVCAM_CONVERT_DL1TO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1TO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1TO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1TO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1TO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1TO1A(uint32_t, uint32_t)

AKVCAM_CONVERT_DL1ATO1(uint8_t,  uint8_t)
AKVCAM_CONVERT_DL1ATO1(uint8_t,  uint16_t)
AKVCAM_CONVERT_DL1ATO1(uint8_t,  uint32_t)
AKVCAM_CONVERT_DL1ATO1(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1ATO1(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1ATO1(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1ATO1(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1ATO1(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1ATO1(uint32_t, uint32_t)

AKVCAM_CONVERT_DL1ATO1A(uint8_t,  uint8_t)
AKVCAM_CONVERT_DL1ATO1A(uint8_t,  uint16_t)
AKVCAM_CONVERT_DL1ATO1A(uint8_t,  uint32_t)
AKVCAM_CONVERT_DL1ATO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_DL1ATO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_DL1ATO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_DL1ATO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_DL1ATO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_DL1ATO1A(uint32_t, uint32_t)

/* Linear upscaling conversion funtions */

// Conversion functions for 3 components to 3 components formats

#define AKVCAM_CONVERT_UL3TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, ky, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3to3(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3(fc,
                              src_line_x, src_line_y, src_line_z,
                              src_line_x_1, src_line_y_1, src_line_z_1,
                              x, ky, &xi, &yi, &zi);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_UL3TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, ky, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                       x, \
                                       (otype)(xo), \
                                       (otype)(yo), \
                                       (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3to3a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3(fc,
                              src_line_x, src_line_y, src_line_z,
                              src_line_x_1, src_line_y_1, src_line_z_1,
                              x, ky, &xi, &yi, &zi);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_UL3ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, ky, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3ato3(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3a(fc,
                               src_line_x, src_line_y, src_line_z, src_line_a,
                               src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                               x, ky, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_UL3ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_ct src, \
                                                                                   akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, ky, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                          x, \
                                          (otype)(xo), \
                                          (otype)(yo), \
                                          (otype)(zo), \
                                          (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_ct src,
                                                                        akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3a(fc,
                               src_line_x, src_line_y, src_line_z, src_line_a,
                               src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                               x, ky, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_matrix(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }
    }
}

AKVCAM_CONVERT_UL3TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_UL3TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_UL3ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_UL3ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3ATO3A(uint32_t, uint32_t)

// Conversion functions for 3 components to 3 components formats
// (same color space)

#define AKVCAM_CONVERT_ULV3TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_ulv3to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, ky, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ulv3to3(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3(fc,
                              src_line_x, src_line_y, src_line_z,
                              src_line_x_1, src_line_y_1, src_line_z_1,
                              x, ky, &xi, &yi, &zi);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_ULV3TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_ulv3to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_ct src, \
                                                                                   akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3_##itype(fc, \
                                        src_line_x, src_line_y, src_line_z, \
                                        src_line_x_1, src_line_y_1, src_line_z_1, \
                                        x, ky, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                                       dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                       x, \
                                       (otype)(xo), \
                                       (otype)(yo), \
                                       (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ulv3to3a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_ct src,
                                                                        akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3(fc,
                              src_line_x, src_line_y, src_line_z,
                              src_line_x_1, src_line_y_1, src_line_z_1,
                              x, ky, &xi, &yi, &zi);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_ULV3ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_ulv3ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_ct src, \
                                                                                   akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, ky, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                                      dst_line_x, dst_line_y, dst_line_z, \
                                      x, \
                                      (otype)(xo), \
                                      (otype)(yo), \
                                      (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ulv3ato3(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_ct src,
                                                                        akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3a(fc,
                               src_line_x, src_line_y, src_line_z, src_line_a,
                               src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                               x, ky, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, ai, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_ULV3ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_ulv3ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                    akvcam_frame_ct src, \
                                                                                    akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul3a_##itype(fc, \
                                         src_line_x, src_line_y, src_line_z, src_line_a, \
                                         src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                                         x, ky, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                                          dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                                          x, \
                                          (otype)(xo), \
                                          (otype)(yo), \
                                          (otype)(zo), \
                                          (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ulv3ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                         akvcam_frame_ct src,
                                                                         akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul3a(fc,
                               src_line_x, src_line_y, src_line_z, src_line_a,
                               src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                               x, ky, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_vector(fc->color_convert, xi, yi, zi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }
    }
}

AKVCAM_CONVERT_ULV3TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_ULV3TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_ULV3TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_ULV3TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_ULV3TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_ULV3TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_ULV3TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_ULV3TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_ULV3TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_ULV3TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_ULV3TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_ULV3TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_ULV3TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_ULV3TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_ULV3TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_ULV3TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_ULV3TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_ULV3TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_ULV3ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_ULV3ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_ULV3ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_ULV3ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_ULV3ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_ULV3ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_ULV3ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_ULV3ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_ULV3ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_ULV3ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_ULV3ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_ULV3ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_ULV3ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_ULV3ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_ULV3ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_ULV3ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_ULV3ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_ULV3ATO3A(uint32_t, uint32_t)

// Conversion functions for 3 components to 1 components formats

#define AKVCAM_CONVERT_UL3TO1(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3to1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                akvcam_frame_ct src, \
                                                                                akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul3_##itype(fc, \
                src_line_x, src_line_y, src_line_z, \
                src_line_x_1, src_line_y_1, src_line_z_1, \
                x, ky, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3to1(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;

            akvcam_read_f8ul3(fc,
                              src_line_x, src_line_y, src_line_z,
                              src_line_x_1, src_line_y_1, src_line_z_1,
                              x, ky, &xi, &yi, &zi);

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }
    }
}

#define AKVCAM_CONVERT_UL3TO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3to1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul3_##itype(fc, \
                src_line_x, src_line_y, src_line_z, \
                src_line_x_1, src_line_y_1, src_line_z_1, \
                x, ky, &xi, &yi, &zi); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1a_##otype(fc, dst_line_x, dst_line_a, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3to1a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;

            int64_t xo = 0;

            akvcam_read_f8ul3(fc,
                              src_line_x, src_line_y, src_line_z,
                              src_line_x_1, src_line_y_1, src_line_z_1,
                              x, ky, &xi, &yi, &zi);

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_UL3ATO1(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3ato1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul3a_##itype(fc, \
                src_line_x, src_line_y, src_line_z, src_line_a, \
                src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                x, ky, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo); \
                \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3ato1(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;

            akvcam_read_f8ul3a(fc,
                               src_line_x, src_line_y, src_line_z, src_line_a,
                               src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                               x, ky, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);
            akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }
    }
}

#define AKVCAM_CONVERT_UL3ATO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul3ato1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_ct src, \
                                                                                   akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset; \
            const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset; \
            const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype yi; \
                itype zi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul3a_##itype(fc, \
                src_line_x, src_line_y, src_line_z, src_line_a, \
                src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1, \
                x, ky, &xi, &yi, &zi, &ai); \
                \
                akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo); \
                \
                akvcam_write1a_ao_##otype(fc, dst_line_x, dst_line_a, x, \
                (otype)(xo), (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul3ato1a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_ct src,
                                                                        akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_y   = akvcam_frame_const_line(src, fc->plane_yi, ys)   + fc->yi_offset;
        const uint8_t *src_line_z   = akvcam_frame_const_line(src, fc->plane_zi, ys)   + fc->zi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_y_1 = akvcam_frame_const_line(src, fc->plane_yi, ys_1) + fc->yi_offset;
        const uint8_t *src_line_z_1 = akvcam_frame_const_line(src, fc->plane_zi, ys_1) + fc->zi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t yi;
            uint8_t zi;
            uint8_t ai;

            int64_t xo = 0;

            akvcam_read_f8ul3a(fc,
                               src_line_x, src_line_y, src_line_z, src_line_a,
                               src_line_x_1, src_line_y_1, src_line_z_1, src_line_a_1,
                               x, ky, &xi, &yi, &zi, &ai);

            akvcam_color_convert_apply_point_3_1(fc->color_convert, xi, yi, zi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }
    }
}

AKVCAM_CONVERT_UL3TO1(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3TO1(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3TO1(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3TO1(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3TO1(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3TO1(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3TO1(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3TO1(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3TO1(uint32_t, uint32_t)

AKVCAM_CONVERT_UL3TO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3TO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3TO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3TO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3TO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3TO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3TO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3TO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3TO1A(uint32_t, uint32_t)

AKVCAM_CONVERT_UL3ATO1(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3ATO1(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3ATO1(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3ATO1(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3ATO1(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3ATO1(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3ATO1(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3ATO1(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3ATO1(uint32_t, uint32_t)

AKVCAM_CONVERT_UL3ATO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL3ATO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL3ATO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL3ATO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL3ATO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL3ATO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL3ATO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL3ATO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL3ATO1A(uint32_t, uint32_t)

// Conversion functions for 1 components to 3 components formats

#define AKVCAM_CONVERT_UL1TO3(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1to3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul1_##itype(fc, src_line_x, src_line_x_1, x, ky, &xi); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                dst_line_x, dst_line_y, dst_line_z, \
                x, \
                (otype)(xo), \
                (otype)(yo), \
                (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1to3(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul1(fc, src_line_x, src_line_x_1, x, ky, &xi);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_UL1TO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1to3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul1_##itype(fc, src_line_x, src_line_x_1, x, ky, &xi); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3a_##otype(fc, \
                dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                x, \
                (otype)(xo), \
                (otype)(yo), \
                (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1to3a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul1(fc, src_line_x, src_line_x_1, x, ky, &xi);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_UL1ATO3(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1ato3_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul1a_##itype(fc, \
                src_line_x, src_line_a, \
                src_line_x_1, src_line_a_1, \
                x, ky, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                akvcam_color_convert_apply_alpha_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3_##otype(fc, \
                dst_line_x, dst_line_y, dst_line_z, \
                x, \
                (otype)(xo), \
                (otype)(yo), \
                (otype)(zo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1ato3(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul1a(fc,
                               src_line_x, src_line_a,
                               src_line_x_1, src_line_a_1,
                               x, ky, &xi, &ai);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);
            akvcam_color_convert_apply_alpha_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
        }
    }
}

#define AKVCAM_CONVERT_UL1ATO3A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1ato3a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_ct src, \
                                                                                   akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset; \
            uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                int64_t yo = 0; \
                int64_t zo = 0; \
                \
                akvcam_read_ul1a_##itype(fc, \
                src_line_x, src_line_a, \
                src_line_x_1, src_line_a_1, \
                x, ky, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo); \
                \
                akvcam_write3a_ao_##otype(fc, \
                dst_line_x, dst_line_y, dst_line_z, dst_line_a, \
                x, \
                (otype)(xo), \
                (otype)(yo), \
                (otype)(zo), \
                (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1ato3a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_ct src,
                                                                        akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_y = akvcam_frame_line(dst, fc->plane_yo, y) + fc->yo_offset;
        uint8_t *dst_line_z = akvcam_frame_line(dst, fc->plane_zo, y) + fc->zo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;

            int64_t xo = 0;
            int64_t yo = 0;
            int64_t zo = 0;

            akvcam_read_f8ul1a(fc,
                               src_line_x, src_line_a,
                               src_line_x_1, src_line_a_1,
                               x, ky, &xi, &ai);

            akvcam_color_convert_apply_point_1_3(fc->color_convert, xi, &xo, &yo, &zo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_y[fc->dst_width_offset_y[x]] = (uint8_t)(yo);
            dst_line_z[fc->dst_width_offset_z[x]] = (uint8_t)(zo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }
    }
}

AKVCAM_CONVERT_UL1TO3(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1TO3(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1TO3(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1TO3(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1TO3(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1TO3(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1TO3(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1TO3(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1TO3(uint32_t, uint32_t)

AKVCAM_CONVERT_UL1TO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1TO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1TO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1TO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1TO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1TO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1TO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1TO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1TO3A(uint32_t, uint32_t)

AKVCAM_CONVERT_UL1ATO3(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1ATO3(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1ATO3(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1ATO3(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1ATO3(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1ATO3(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1ATO3(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1ATO3(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1ATO3(uint32_t, uint32_t)

AKVCAM_CONVERT_UL1ATO3A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1ATO3A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1ATO3A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1ATO3A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1ATO3A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1ATO3A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1ATO3A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1ATO3A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1ATO3A(uint32_t, uint32_t)

// Conversion functions for 1 components to 1 components formats

#define AKVCAM_CONVERT_UL1TO1(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1to1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                 akvcam_frame_ct src, \
                                                                                 akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul1_##itype(fc, src_line_x, src_line_x_1, x, ky, &xi); \
                \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1to1(akvcam_frame_convert_parameters_ct fc,
                                                                      akvcam_frame_ct src,
                                                                      akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;

            int64_t xo = 0;

            akvcam_read_f8ul1(fc, src_line_x, src_line_x_1, x, ky, &xi);

            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }
    }
}

#define AKVCAM_CONVERT_UL1TO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1to1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul1_##itype(fc, src_line_x, src_line_x_1, x, ky, &xi); \
                \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                \
                akvcam_write1a_##otype(fc, dst_line_x, dst_line_a, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1to1a(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;

            int64_t xo = 0;

            akvcam_read_f8ul1(fc, src_line_x, src_line_x_1, x, ky, &xi);

            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = 0xff;
        }
    }
}

#define AKVCAM_CONVERT_UL1ATO1(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1ato1_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                  akvcam_frame_ct src, \
                                                                                  akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul1a_##itype(fc, \
                src_line_x, src_line_a, \
                src_line_x_1, src_line_a_1, \
                x, ky, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo); \
                \
                akvcam_write1_##otype(fc, dst_line_x, x, (otype)(xo)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1ato1(akvcam_frame_convert_parameters_ct fc,
                                                                       akvcam_frame_ct src,
                                                                       akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;

            int64_t xo = 0;

            akvcam_read_f8ul1a(fc,
                               src_line_x, src_line_a,
                               src_line_x_1, src_line_a_1,
                               x, ky, &xi, &ai);

            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);
            akvcam_color_convert_apply_alpha_1(fc->color_convert, ai, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
        }
    }
}

#define AKVCAM_CONVERT_UL1ATO1A(itype, otype) \
    static inline void akvcam_converter_private_convert_ul1ato1a_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                   akvcam_frame_ct src, \
                                                                                   akvcam_frame_t dst) \
    { \
        int y; \
        \
        for (y = fc->ymin; y < fc->ymax; ++y) { \
            int ys   = fc->src_height[y]; \
            int ys_1 = fc->src_height_1[y]; \
            \
            const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset; \
            const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset; \
            const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset; \
            const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset; \
            \
            uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset; \
            uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset; \
            \
            int ky = fc->ky[y]; \
            int x; \
            \
            for (x = fc->xmin; x < fc->xmax; ++x) { \
                itype xi; \
                itype ai; \
                \
                int64_t xo = 0; \
                \
                akvcam_read_ul1a_##itype(fc, \
                src_line_x, src_line_a, \
                src_line_x_1, src_line_a_1, \
                x, ky, &xi, &ai); \
                \
                akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo); \
                \
                akvcam_write1a_ao_##otype(fc, dst_line_x, dst_line_a, x, \
                (otype)(xo), (otype)(ai)); \
            } \
        } \
    }

static inline void akvcam_converter_private_convert_fast_8bits_ul1ato1a(akvcam_frame_convert_parameters_ct fc,
                                                                        akvcam_frame_ct src,
                                                                        akvcam_frame_t dst)
{
    int y;

    for (y = fc->ymin; y < fc->ymax; ++y) {
        int ys   = fc->src_height[y];
        int ys_1 = fc->src_height_1[y];

        const uint8_t *src_line_x   = akvcam_frame_const_line(src, fc->plane_xi, ys)   + fc->xi_offset;
        const uint8_t *src_line_a   = akvcam_frame_const_line(src, fc->plane_ai, ys)   + fc->ai_offset;
        const uint8_t *src_line_x_1 = akvcam_frame_const_line(src, fc->plane_xi, ys_1) + fc->xi_offset;
        const uint8_t *src_line_a_1 = akvcam_frame_const_line(src, fc->plane_ai, ys_1) + fc->ai_offset;

        uint8_t *dst_line_x = akvcam_frame_line(dst, fc->plane_xo, y) + fc->xo_offset;
        uint8_t *dst_line_a = akvcam_frame_line(dst, fc->plane_ao, y) + fc->ao_offset;

        int ky = fc->ky[y];
        int x;

        for (x = fc->xmin; x < fc->xmax; ++x) {
            uint8_t xi;
            uint8_t ai;

            int64_t xo = 0;

            akvcam_read_f8ul1a(fc,
                               src_line_x, src_line_a,
                               src_line_x_1, src_line_a_1,
                               x, ky, &xi, &ai);

            akvcam_color_convert_apply_point_1_1(fc->color_convert, xi, &xo);

            dst_line_x[fc->dst_width_offset_x[x]] = (uint8_t)(xo);
            dst_line_a[fc->dst_width_offset_a[x]] = ai;
        }
    }
}

AKVCAM_CONVERT_UL1TO1(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1TO1(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1TO1(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1TO1(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1TO1(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1TO1(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1TO1(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1TO1(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1TO1(uint32_t, uint32_t)

AKVCAM_CONVERT_UL1TO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1TO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1TO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1TO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1TO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1TO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1TO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1TO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1TO1A(uint32_t, uint32_t)

AKVCAM_CONVERT_UL1ATO1(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1ATO1(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1ATO1(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1ATO1(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1ATO1(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1ATO1(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1ATO1(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1ATO1(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1ATO1(uint32_t, uint32_t)

AKVCAM_CONVERT_UL1ATO1A(uint8_t, uint8_t)
AKVCAM_CONVERT_UL1ATO1A(uint8_t, uint16_t)
AKVCAM_CONVERT_UL1ATO1A(uint8_t, uint32_t)
AKVCAM_CONVERT_UL1ATO1A(uint16_t, uint8_t)
AKVCAM_CONVERT_UL1ATO1A(uint16_t, uint16_t)
AKVCAM_CONVERT_UL1ATO1A(uint16_t, uint32_t)
AKVCAM_CONVERT_UL1ATO1A(uint32_t, uint8_t)
AKVCAM_CONVERT_UL1ATO1A(uint32_t, uint16_t)
AKVCAM_CONVERT_UL1ATO1A(uint32_t, uint32_t)

#define CONVERT_FUNC(icomponents, ocomponents, itype, otype) \
        static inline void akvcam_converter_private_convert_func_##icomponents##to##ocomponents##_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                                                    akvcam_frame_ct src, \
                                                                                                                    akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_##icomponents##ato##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_##icomponents##ato##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_##icomponents##to##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_##icomponents##to##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            }; \
        }

#define CONVERT_FAST_FUNC(icomponents, ocomponents) \
        static inline void akvcam_converter_private_convert_func_fast_8bits_##icomponents##to##ocomponents(akvcam_frame_convert_parameters_ct fc, \
                                                                                                           akvcam_frame_ct src, \
                                                                                                           akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_fast_8bits_##icomponents##ato##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_fast_8bits_##icomponents##ato##ocomponents(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_fast_8bits_##icomponents##to##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_fast_8bits_##icomponents##to##ocomponents(fc, src, dst); \
                break; \
            }; \
        }

#define CONVERTV_FUNC(icomponents, ocomponents, itype, otype) \
        static inline void akvcam_converter_private_convert_func_v##icomponents##to##ocomponents##_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                                                     akvcam_frame_ct src, \
                                                                                                                     akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_v##icomponents##ato##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_v##icomponents##ato##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_v##icomponents##to##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_v##icomponents##to##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            }; \
        }

#define CONVERT_FASTV_FUNC(icomponents, ocomponents) \
        static inline void akvcam_converter_private_convert_func_fast_8bits_v##icomponents##to##ocomponents(akvcam_frame_convert_parameters_ct fc, \
                                                                                                            akvcam_frame_ct src, \
                                                                                                            akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_fast_8bits_v##icomponents##ato##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_fast_8bits_v##icomponents##ato##ocomponents(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_fast_8bits_v##icomponents##to##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_fast_8bits_v##icomponents##to##ocomponents(fc, src, dst); \
                break; \
            }; \
        }

#define CONVERTDL_FUNC(icomponents, ocomponents, itype, otype) \
        static inline void akvcam_converter_private_convert_func_dl##icomponents##to##ocomponents##_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                                                      akvcam_frame_ct src, \
                                                                                                                      akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_integral_image_##icomponents##a(itype, fc, src); \
                break; \
            default: \
                akvcam_integral_image_##icomponents(itype, fc, src); \
                break; \
            } \
            \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_dl##icomponents##ato##ocomponents##a_##itype##_##otype(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_dl##icomponents##ato##ocomponents##_##itype##_##otype(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_dl##icomponents##to##ocomponents##a_##itype##_##otype(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_dl##icomponents##to##ocomponents##_##itype##_##otype(fc, dst); \
                break; \
            }; \
        }

#define CONVERT_FASTDL_FUNC(icomponents, ocomponents) \
        static inline void akvcam_converter_private_convert_func_fast_8bits_dl##icomponents##to##ocomponents(akvcam_frame_convert_parameters_ct fc, \
                                                                                                             akvcam_frame_ct src, \
                                                                                                             akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_integral_image_##icomponents##a(uint8_t, fc, src); \
                break; \
            default: \
                akvcam_integral_image_##icomponents(uint8_t, fc, src); \
                break; \
            } \
            \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_fast_8bits_dl##icomponents##ato##ocomponents##a(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_fast_8bits_dl##icomponents##ato##ocomponents(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_fast_8bits_dl##icomponents##to##ocomponents##a(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_fast_8bits_dl##icomponents##to##ocomponents(fc, dst); \
                break; \
            }; \
        }

#define CONVERTDLV_FUNC(icomponents, ocomponents, itype, otype) \
        static inline void akvcam_converter_private_convert_func_dlv##icomponents##to##ocomponents##_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                                                       akvcam_frame_ct src, \
                                                                                                                       akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_integral_image_##icomponents##a(itype, fc, src); \
                break; \
            default: \
                akvcam_integral_image_##icomponents(itype, fc, src); \
                break; \
            } \
            \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_dlv##icomponents##ato##ocomponents##a_##itype##_##otype(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_dlv##icomponents##ato##ocomponents##_##itype##_##otype(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_dlv##icomponents##to##ocomponents##a_##itype##_##otype(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_dlv##icomponents##to##ocomponents##_##itype##_##otype(fc, dst); \
                break; \
            }; \
        }

#define CONVERT_FASTDLV_FUNC(icomponents, ocomponents) \
        static inline void akvcam_converter_private_convert_func_fast_8bits_dlv##icomponents##to##ocomponents(akvcam_frame_convert_parameters_ct fc, \
                                                                                                              akvcam_frame_ct src, \
                                                                                                              akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_integral_image_##icomponents##a(uint8_t, fc, src); \
                break; \
            default: \
                akvcam_integral_image_##icomponents(uint8_t, fc, src); \
                break; \
            } \
            \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_fast_8bits_dlv##icomponents##ato##ocomponents##a(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_fast_8bits_dlv##icomponents##ato##ocomponents(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_fast_8bits_dlv##icomponents##to##ocomponents##a(fc, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_fast_8bits_dlv##icomponents##to##ocomponents(fc, dst); \
                break; \
            }; \
        }

#define CONVERTUL_FUNC(icomponents, ocomponents, itype, otype) \
        static inline void akvcam_converter_private_convert_func_ul##icomponents##to##ocomponents##_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                                                      akvcam_frame_ct src, \
                                                                                                                      akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_ul##icomponents##ato##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_ul##icomponents##ato##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_ul##icomponents##to##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_ul##icomponents##to##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            }; \
        }

#define CONVERT_FASTUL_FUNC(icomponents, ocomponents) \
        static inline void akvcam_converter_private_convert_func_fast_8bits_ul##icomponents##to##ocomponents(akvcam_frame_convert_parameters_ct fc, \
                                                                                                             akvcam_frame_ct src, \
                                                                                                             akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_fast_8bits_ul##icomponents##ato##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_fast_8bits_ul##icomponents##ato##ocomponents(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_fast_8bits_ul##icomponents##to##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_fast_8bits_ul##icomponents##to##ocomponents(fc, src, dst); \
                break; \
            }; \
        }

#define CONVERTULV_FUNC(icomponents, ocomponents, itype, otype) \
        static inline void akvcam_converter_private_convert_func_ulv##icomponents##to##ocomponents##_##itype##_##otype(akvcam_frame_convert_parameters_ct fc, \
                                                                                                                       akvcam_frame_ct src, \
                                                                                                                       akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_ulv##icomponents##ato##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_ulv##icomponents##ato##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_ulv##icomponents##to##ocomponents##a_##itype##_##otype(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_ulv##icomponents##to##ocomponents##_##itype##_##otype(fc, src, dst); \
                break; \
            }; \
        }

#define CONVERT_FASTULV_FUNC(icomponents, ocomponents) \
        static inline void akvcam_converter_private_convert_func_fast_8bits_ulv##icomponents##to##ocomponents(akvcam_frame_convert_parameters_ct fc, \
                                                                                                              akvcam_frame_ct src, \
                                                                                                              akvcam_frame_t dst) \
        { \
            switch (fc->alpha_mode) { \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_AO: \
                akvcam_converter_private_convert_fast_8bits_ulv##icomponents##ato##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_AI_O: \
                akvcam_converter_private_convert_fast_8bits_ulv##icomponents##ato##ocomponents(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_AO: \
                akvcam_converter_private_convert_fast_8bits_ulv##icomponents##to##ocomponents##a(fc, src, dst); \
                break; \
            case AKVCAM_CONVERT_ALPHA_MODE_I_O: \
                akvcam_converter_private_convert_fast_8bits_ulv##icomponents##to##ocomponents(fc, src, dst); \
                break; \
            }; \
        }

// Dispatch macros by type for template functions
#define akvcam_converter_private_convert_func_3to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_3to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_3to1(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_3to1_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_1to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_1to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_1to1(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_1to1_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_v3to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_v3to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_dl3to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_dl3to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_dl3to1(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_dl3to1_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_dl1to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_dl1to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_dl1to1(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_dl1to1_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_dlv3to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_dlv3to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_ul3to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_ul3to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_ul3to1(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_ul3to1_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_ul1to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_ul1to3_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_ul1to1(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_ul1to1_##itype##_##otype(fc, src, dst)
#define akvcam_converter_private_convert_func_ulv3to3(itype, otype, fc, src, dst) \
    akvcam_converter_private_convert_func_ulv3to3_##itype##_##otype(fc, src, dst)

#define CONVERT_TEMPLATE_FUNC(itype, otype) \
        static inline void akvcam_converter_private_convert_##itype##_##otype(akvcam_converter_ct self, \
                                                                              akvcam_frame_convert_parameters_ct fc, \
                                                                              akvcam_frame_ct src, \
                                                                              akvcam_frame_t dst) \
        { \
            if (self->scaling_mode == AKVCAM_SCALING_MODE_LINEAR \
                && fc->resize_mode == AKVCAM_RESIZE_MODE_UP) { \
                switch (fc->convert_type) { \
                case AKVCAM_CONVERT_TYPE_VECTOR: \
                    akvcam_converter_private_convert_func_ulv3to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_3TO3: \
                    akvcam_converter_private_convert_func_ul3to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_3TO1: \
                    akvcam_converter_private_convert_func_ul3to1(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_1TO3: \
                    akvcam_converter_private_convert_func_ul1to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_1TO1: \
                    akvcam_converter_private_convert_func_ul1to1(itype, otype, fc, src, dst); \
                    break; \
                } \
            } else if (self->scaling_mode == AKVCAM_SCALING_MODE_LINEAR \
                       && fc->resize_mode == AKVCAM_RESIZE_MODE_DOWN) { \
                switch (fc->convert_type) { \
                case AKVCAM_CONVERT_TYPE_VECTOR: \
                    akvcam_converter_private_convert_func_dlv3to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_3TO3: \
                    akvcam_converter_private_convert_func_dl3to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_3TO1: \
                    akvcam_converter_private_convert_func_dl3to1(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_1TO3: \
                    akvcam_converter_private_convert_func_dl1to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_1TO1: \
                    akvcam_converter_private_convert_func_dl1to1(itype, otype, fc, src, dst); \
                    break; \
                } \
            } else { \
                switch (fc->convert_type) { \
                case AKVCAM_CONVERT_TYPE_VECTOR: \
                    akvcam_converter_private_convert_func_v3to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_3TO3: \
                    akvcam_converter_private_convert_func_3to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_3TO1: \
                    akvcam_converter_private_convert_func_3to1(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_1TO3: \
                    akvcam_converter_private_convert_func_1to3(itype, otype, fc, src, dst); \
                    break; \
                case AKVCAM_CONVERT_TYPE_1TO1: \
                    akvcam_converter_private_convert_func_1to1(itype, otype, fc, src, dst); \
                    break; \
                } \
            } \
        }

CONVERT_FUNC(3, 3, uint8_t, uint8_t)
CONVERT_FUNC(3, 3, uint8_t, uint16_t)
CONVERT_FUNC(3, 3, uint8_t, uint32_t)
CONVERT_FUNC(3, 3, uint16_t, uint8_t)
CONVERT_FUNC(3, 3, uint16_t, uint16_t)
CONVERT_FUNC(3, 3, uint16_t, uint32_t)
CONVERT_FUNC(3, 3, uint32_t, uint8_t)
CONVERT_FUNC(3, 3, uint32_t, uint16_t)
CONVERT_FUNC(3, 3, uint32_t, uint32_t)
CONVERT_FUNC(3, 1, uint8_t, uint8_t)
CONVERT_FUNC(3, 1, uint8_t, uint16_t)
CONVERT_FUNC(3, 1, uint8_t, uint32_t)
CONVERT_FUNC(3, 1, uint16_t, uint8_t)
CONVERT_FUNC(3, 1, uint16_t, uint16_t)
CONVERT_FUNC(3, 1, uint16_t, uint32_t)
CONVERT_FUNC(3, 1, uint32_t, uint8_t)
CONVERT_FUNC(3, 1, uint32_t, uint16_t)
CONVERT_FUNC(3, 1, uint32_t, uint32_t)
CONVERT_FUNC(1, 3, uint8_t, uint8_t)
CONVERT_FUNC(1, 3, uint8_t, uint16_t)
CONVERT_FUNC(1, 3, uint8_t, uint32_t)
CONVERT_FUNC(1, 3, uint16_t, uint8_t)
CONVERT_FUNC(1, 3, uint16_t, uint16_t)
CONVERT_FUNC(1, 3, uint16_t, uint32_t)
CONVERT_FUNC(1, 3, uint32_t, uint8_t)
CONVERT_FUNC(1, 3, uint32_t, uint16_t)
CONVERT_FUNC(1, 3, uint32_t, uint32_t)
CONVERT_FUNC(1, 1, uint8_t, uint8_t)
CONVERT_FUNC(1, 1, uint8_t, uint16_t)
CONVERT_FUNC(1, 1, uint8_t, uint32_t)
CONVERT_FUNC(1, 1, uint16_t, uint8_t)
CONVERT_FUNC(1, 1, uint16_t, uint16_t)
CONVERT_FUNC(1, 1, uint16_t, uint32_t)
CONVERT_FUNC(1, 1, uint32_t, uint8_t)
CONVERT_FUNC(1, 1, uint32_t, uint16_t)
CONVERT_FUNC(1, 1, uint32_t, uint32_t)

CONVERTV_FUNC(3, 3, uint8_t, uint8_t)
CONVERTV_FUNC(3, 3, uint8_t, uint16_t)
CONVERTV_FUNC(3, 3, uint8_t, uint32_t)
CONVERTV_FUNC(3, 3, uint16_t, uint8_t)
CONVERTV_FUNC(3, 3, uint16_t, uint16_t)
CONVERTV_FUNC(3, 3, uint16_t, uint32_t)
CONVERTV_FUNC(3, 3, uint32_t, uint8_t)
CONVERTV_FUNC(3, 3, uint32_t, uint16_t)
CONVERTV_FUNC(3, 3, uint32_t, uint32_t)

CONVERTDL_FUNC(3, 3, uint8_t, uint8_t)
CONVERTDL_FUNC(3, 3, uint8_t, uint16_t)
CONVERTDL_FUNC(3, 3, uint8_t, uint32_t)
CONVERTDL_FUNC(3, 3, uint16_t, uint8_t)
CONVERTDL_FUNC(3, 3, uint16_t, uint16_t)
CONVERTDL_FUNC(3, 3, uint16_t, uint32_t)
CONVERTDL_FUNC(3, 3, uint32_t, uint8_t)
CONVERTDL_FUNC(3, 3, uint32_t, uint16_t)
CONVERTDL_FUNC(3, 3, uint32_t, uint32_t)
CONVERTDL_FUNC(3, 1, uint8_t, uint8_t)
CONVERTDL_FUNC(3, 1, uint8_t, uint16_t)
CONVERTDL_FUNC(3, 1, uint8_t, uint32_t)
CONVERTDL_FUNC(3, 1, uint16_t, uint8_t)
CONVERTDL_FUNC(3, 1, uint16_t, uint16_t)
CONVERTDL_FUNC(3, 1, uint16_t, uint32_t)
CONVERTDL_FUNC(3, 1, uint32_t, uint8_t)
CONVERTDL_FUNC(3, 1, uint32_t, uint16_t)
CONVERTDL_FUNC(3, 1, uint32_t, uint32_t)
CONVERTDL_FUNC(1, 3, uint8_t, uint8_t)
CONVERTDL_FUNC(1, 3, uint8_t, uint16_t)
CONVERTDL_FUNC(1, 3, uint8_t, uint32_t)
CONVERTDL_FUNC(1, 3, uint16_t, uint8_t)
CONVERTDL_FUNC(1, 3, uint16_t, uint16_t)
CONVERTDL_FUNC(1, 3, uint16_t, uint32_t)
CONVERTDL_FUNC(1, 3, uint32_t, uint8_t)
CONVERTDL_FUNC(1, 3, uint32_t, uint16_t)
CONVERTDL_FUNC(1, 3, uint32_t, uint32_t)
CONVERTDL_FUNC(1, 1, uint8_t, uint8_t)
CONVERTDL_FUNC(1, 1, uint8_t, uint16_t)
CONVERTDL_FUNC(1, 1, uint8_t, uint32_t)
CONVERTDL_FUNC(1, 1, uint16_t, uint8_t)
CONVERTDL_FUNC(1, 1, uint16_t, uint16_t)
CONVERTDL_FUNC(1, 1, uint16_t, uint32_t)
CONVERTDL_FUNC(1, 1, uint32_t, uint8_t)
CONVERTDL_FUNC(1, 1, uint32_t, uint16_t)
CONVERTDL_FUNC(1, 1, uint32_t, uint32_t)

CONVERTDLV_FUNC(3, 3, uint8_t, uint8_t)
CONVERTDLV_FUNC(3, 3, uint8_t, uint16_t)
CONVERTDLV_FUNC(3, 3, uint8_t, uint32_t)
CONVERTDLV_FUNC(3, 3, uint16_t, uint8_t)
CONVERTDLV_FUNC(3, 3, uint16_t, uint16_t)
CONVERTDLV_FUNC(3, 3, uint16_t, uint32_t)
CONVERTDLV_FUNC(3, 3, uint32_t, uint8_t)
CONVERTDLV_FUNC(3, 3, uint32_t, uint16_t)
CONVERTDLV_FUNC(3, 3, uint32_t, uint32_t)

CONVERTUL_FUNC(3, 3, uint8_t, uint8_t)
CONVERTUL_FUNC(3, 3, uint8_t, uint16_t)
CONVERTUL_FUNC(3, 3, uint8_t, uint32_t)
CONVERTUL_FUNC(3, 3, uint16_t, uint8_t)
CONVERTUL_FUNC(3, 3, uint16_t, uint16_t)
CONVERTUL_FUNC(3, 3, uint16_t, uint32_t)
CONVERTUL_FUNC(3, 3, uint32_t, uint8_t)
CONVERTUL_FUNC(3, 3, uint32_t, uint16_t)
CONVERTUL_FUNC(3, 3, uint32_t, uint32_t)
CONVERTUL_FUNC(3, 1, uint8_t, uint8_t)
CONVERTUL_FUNC(3, 1, uint8_t, uint16_t)
CONVERTUL_FUNC(3, 1, uint8_t, uint32_t)
CONVERTUL_FUNC(3, 1, uint16_t, uint8_t)
CONVERTUL_FUNC(3, 1, uint16_t, uint16_t)
CONVERTUL_FUNC(3, 1, uint16_t, uint32_t)
CONVERTUL_FUNC(3, 1, uint32_t, uint8_t)
CONVERTUL_FUNC(3, 1, uint32_t, uint16_t)
CONVERTUL_FUNC(3, 1, uint32_t, uint32_t)
CONVERTUL_FUNC(1, 3, uint8_t, uint8_t)
CONVERTUL_FUNC(1, 3, uint8_t, uint16_t)
CONVERTUL_FUNC(1, 3, uint8_t, uint32_t)
CONVERTUL_FUNC(1, 3, uint16_t, uint8_t)
CONVERTUL_FUNC(1, 3, uint16_t, uint16_t)
CONVERTUL_FUNC(1, 3, uint16_t, uint32_t)
CONVERTUL_FUNC(1, 3, uint32_t, uint8_t)
CONVERTUL_FUNC(1, 3, uint32_t, uint16_t)
CONVERTUL_FUNC(1, 3, uint32_t, uint32_t)
CONVERTUL_FUNC(1, 1, uint8_t, uint8_t)
CONVERTUL_FUNC(1, 1, uint8_t, uint16_t)
CONVERTUL_FUNC(1, 1, uint8_t, uint32_t)
CONVERTUL_FUNC(1, 1, uint16_t, uint8_t)
CONVERTUL_FUNC(1, 1, uint16_t, uint16_t)
CONVERTUL_FUNC(1, 1, uint16_t, uint32_t)
CONVERTUL_FUNC(1, 1, uint32_t, uint8_t)
CONVERTUL_FUNC(1, 1, uint32_t, uint16_t)
CONVERTUL_FUNC(1, 1, uint32_t, uint32_t)

CONVERTULV_FUNC(3, 3, uint8_t, uint8_t)
CONVERTULV_FUNC(3, 3, uint8_t, uint16_t)
CONVERTULV_FUNC(3, 3, uint8_t, uint32_t)
CONVERTULV_FUNC(3, 3, uint16_t, uint8_t)
CONVERTULV_FUNC(3, 3, uint16_t, uint16_t)
CONVERTULV_FUNC(3, 3, uint16_t, uint32_t)
CONVERTULV_FUNC(3, 3, uint32_t, uint8_t)
CONVERTULV_FUNC(3, 3, uint32_t, uint16_t)
CONVERTULV_FUNC(3, 3, uint32_t, uint32_t)

CONVERT_FAST_FUNC(3, 3)
CONVERT_FAST_FUNC(3, 1)
CONVERT_FAST_FUNC(1, 3)
CONVERT_FAST_FUNC(1, 1)
CONVERT_FASTV_FUNC(3, 3)
CONVERT_FASTDL_FUNC(3, 3)
CONVERT_FASTDL_FUNC(3, 1)
CONVERT_FASTDL_FUNC(1, 3)
CONVERT_FASTDL_FUNC(1, 1)
CONVERT_FASTDLV_FUNC(3, 3)
CONVERT_FASTUL_FUNC(3, 3)
CONVERT_FASTUL_FUNC(3, 1)
CONVERT_FASTUL_FUNC(1, 3)
CONVERT_FASTUL_FUNC(1, 1)
CONVERT_FASTULV_FUNC(3, 3)

CONVERT_TEMPLATE_FUNC(uint8_t, uint8_t)
CONVERT_TEMPLATE_FUNC(uint8_t, uint16_t)
CONVERT_TEMPLATE_FUNC(uint8_t, uint32_t)
CONVERT_TEMPLATE_FUNC(uint16_t, uint8_t)
CONVERT_TEMPLATE_FUNC(uint16_t, uint16_t)
CONVERT_TEMPLATE_FUNC(uint16_t, uint32_t)
CONVERT_TEMPLATE_FUNC(uint32_t, uint8_t)
CONVERT_TEMPLATE_FUNC(uint32_t, uint16_t)
CONVERT_TEMPLATE_FUNC(uint32_t, uint32_t)

typedef struct
{
    AKVCAM_SCALING_MODE scaling;
    char  str[32];
} akvcam_converter_scaling_strings, *akvcam_converter_scaling_strings_t;

typedef struct
{
    AKVCAM_ASPECT_RATIO_MODE aspect_ratio;
    char  str[32];
} akvcam_convert_aspect_ratio_strings, *akvcam_convert_aspect_ratio_strings_t;

akvcam_converter_t akvcam_converter_new(void)
{
    akvcam_converter_t self = kzalloc(sizeof(struct akvcam_converter), GFP_KERNEL);
    kref_init(&self->ref);
    self->output_format = akvcam_format_new(0, 0, 0, NULL);
    self->fc = NULL;
    self->fc_size = 0;
    self->cache_index = 0;
    self->yuv_color_space = AKVCAM_YUV_COLOR_SPACE_ITUR_BT601;
    self->yuv_color_space_type = AKVCAM_YUV_COLOR_SPACE_TYPE_STUDIO_SWING;
    self->scaling_mode = AKVCAM_SCALING_MODE_FAST;
    self->aspect_ratio_mode = AKVCAM_ASPECT_RATIO_MODE_IGNORE;

    return self;
}

akvcam_converter_t akvcam_converter_new_copy(akvcam_converter_ct other)
{
    akvcam_converter_t self = kzalloc(sizeof(struct akvcam_converter), GFP_KERNEL);
    kref_init(&self->ref);
    self->output_format = akvcam_format_new_copy(other->output_format);
    self->fc = NULL;
    self->fc_size = 0;
    self->cache_index = 0;
    self->yuv_color_space = other->yuv_color_space;
    self->yuv_color_space_type = other->yuv_color_space_type;
    self->scaling_mode = other->scaling_mode;
    self->aspect_ratio_mode = other->aspect_ratio_mode;

    return self;
}

static void akvcam_converter_free(struct kref *ref)
{
    akvcam_converter_t self = container_of(ref, struct akvcam_converter, ref);
    akvcam_frame_convert_parameters_delete(&self->fc, self->fc_size);
    akvcam_format_delete(self->output_format);
    kfree(self);
}

void akvcam_converter_delete(akvcam_converter_t self)
{
    if (self)
        kref_put(&self->ref, akvcam_converter_free);
}

akvcam_converter_t akvcam_converter_ref(akvcam_converter_t self)
{
    if (self)
        kref_get(&self->ref);

    return self;
}

void akvcam_converter_copy(akvcam_converter_t self, akvcam_converter_ct other)
{
    if (!self)
        return;

    if (other) {
        akvcam_format_copy(self->output_format, other->output_format);
        self->yuv_color_space = other->yuv_color_space;
        self->yuv_color_space_type = other->yuv_color_space_type;
        self->scaling_mode = other->scaling_mode;
        self->aspect_ratio_mode = other->aspect_ratio_mode;
    } else {
        if (self->output_format)
            akvcam_format_delete(self->output_format);

        self->output_format = akvcam_format_new(0, 0, 0, NULL);
        self->yuv_color_space = AKVCAM_YUV_COLOR_SPACE_ITUR_BT601;
        self->yuv_color_space_type = AKVCAM_YUV_COLOR_SPACE_TYPE_STUDIO_SWING;
        self->scaling_mode = AKVCAM_SCALING_MODE_FAST;
        self->aspect_ratio_mode = AKVCAM_ASPECT_RATIO_MODE_IGNORE;
    }
}

akvcam_format_t akvcam_converter_output_format(akvcam_converter_ct self)
{
    return akvcam_format_new_copy(self->output_format);
}

void akvcam_converter_set_output_format(akvcam_converter_t self,
                                        akvcam_format_ct format)
{
    akvcam_format_copy(self->output_format, format);
}

AKVCAM_YUV_COLOR_SPACE akvcam_converter_yuv_color_space(akvcam_converter_ct self)
{
    return self->yuv_color_space;
}

void akvcam_converter_set_yuv_color_space(akvcam_converter_t self,
                                          AKVCAM_YUV_COLOR_SPACE yuv_color_space)
{
    self->yuv_color_space = yuv_color_space;
}

AKVCAM_YUV_COLOR_SPACE_TYPE akvcam_converter_yuv_color_space_type(akvcam_converter_ct self)
{
    return self->yuv_color_space_type;
}

void akvcam_converter_set_yuv_color_space_type(akvcam_converter_t self,
                                               AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type)
{
    self->yuv_color_space_type = yuv_color_space_type;
}

AKVCAM_SCALING_MODE akvcam_converter_scaling_mode(akvcam_converter_ct self)
{
    return self->scaling_mode;
}

void akvcam_converter_set_scaling_mode(akvcam_converter_t self,
                                       AKVCAM_SCALING_MODE scaling_mode)
{
    self->scaling_mode = scaling_mode;
}

AKVCAM_ASPECT_RATIO_MODE akvcam_converter_aspect_ratio_mode(akvcam_converter_ct self)
{
    return self->aspect_ratio_mode;
}

void akvcam_converter_set_aspect_ratio_mode(akvcam_converter_t self,
                                            AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode)
{
    self->aspect_ratio_mode = aspect_ratio_mode;
}

void akvcam_converter_set_cache_index(akvcam_converter_t self,
                                      int index)
{
    self->cache_index = index;
}

bool akvcam_converter_begin(akvcam_converter_t self)
{
    self->cache_index = 0;

    return true;
}

void akvcam_converter_end(akvcam_converter_t self)
{
    self->cache_index = 0;
}

akvcam_frame_t akvcam_converter_convert(akvcam_converter_t self,
                                        akvcam_frame_ct frame)
{
    if (!frame)
        return NULL;

    akvcam_format_t format = akvcam_frame_format_nr(frame);

    if (akvcam_format_fourcc(format) == akvcam_format_fourcc(self->output_format)
        && akvcam_format_width(format) == akvcam_format_width(self->output_format)
        && akvcam_format_height(format) == akvcam_format_height(self->output_format)) {

        return akvcam_frame_new_copy(frame);
    }

    return akvcam_converter_private_convert(self, frame, self->output_format);
}

void akvcam_converter_reset(akvcam_converter_t self)
{
    akvcam_frame_convert_parameters_delete(&self->fc, self->fc_size);
    self->fc_size = 0;
}

const char *akvcam_converter_scaling_mode_to_string(AKVCAM_SCALING_MODE scaling_mode)
{
    size_t i;
    static char scaling_str[AKVCAM_MAX_STRING_SIZE];
    static const akvcam_converter_scaling_strings scaling_strings[] = {
        {AKVCAM_SCALING_MODE_FAST  , "Fast"  },
        {AKVCAM_SCALING_MODE_LINEAR, "Linear"},
        {-1                        , ""      },
    };

    memset(scaling_str, 0, AKVCAM_MAX_STRING_SIZE);

    for (i = 0; scaling_strings[i].scaling >= 0; i++)
        if (scaling_strings[i].scaling == scaling_mode) {
            snprintf(scaling_str,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s",
                     scaling_strings[i].str);

            return scaling_str;
        }

    snprintf(scaling_str, AKVCAM_MAX_STRING_SIZE, "AKVCAM_SCALING_MODE(%d)", scaling_mode);

    return scaling_str;
}

const char *akvcam_converter_aspect_ratio_mode_to_string(AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode)
{
    size_t i;
    static char aspect_ratio_str[AKVCAM_MAX_STRING_SIZE];
    static const akvcam_convert_aspect_ratio_strings aspect_ratio_strings[] = {
        {AKVCAM_ASPECT_RATIO_MODE_IGNORE   , "Ignore"   },
        {AKVCAM_ASPECT_RATIO_MODE_KEEP     , "Keep"     },
        {AKVCAM_ASPECT_RATIO_MODE_EXPANDING, "Expanding"},
        {AKVCAM_ASPECT_RATIO_MODE_FIT      , "Fit"      },
        {-1                                , ""         },
    };

    memset(aspect_ratio_str, 0, AKVCAM_MAX_STRING_SIZE);

    for (i = 0; aspect_ratio_strings[i].aspect_ratio >= 0; i++)
        if (aspect_ratio_strings[i].aspect_ratio == aspect_ratio_mode) {
            snprintf(aspect_ratio_str,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s",
                     aspect_ratio_strings[i].str);

            return aspect_ratio_str;
        }

    snprintf(aspect_ratio_str, AKVCAM_MAX_STRING_SIZE, "AKVCAM_ASPECT_RATIO_MODE(%d)", aspect_ratio_mode);

    return aspect_ratio_str;
}

#define DEFINE_CONVERT_FUNC(isize, osize) \
    case AKVCAM_CONVERT_DATA_TYPES_##isize##_##osize: \
        akvcam_converter_private_convert_uint##isize##_t_uint##osize##_t(self, \
                                                                         fc, \
                                                                         frame, \
                                                                         fc->output_frame); \
        \
        if (fc->to_endian != __BYTE_ORDER__) \
            akvcam_swap_data_bytes_uint##osize##_t((uint##osize##_t *)akvcam_frame_data(fc->output_frame), \
                                                   akvcam_frame_size(fc->output_frame)); \
        \
        break;

akvcam_frame_t akvcam_converter_private_convert(akvcam_converter_t self,
                                                akvcam_frame_ct frame,
                                                akvcam_format_ct output_format)
{
    static const int max_cache_alloc = 1 << 16;

    if (self->cache_index >= max_cache_alloc)
        return NULL;

    if (self->cache_index >= self->fc_size) {
        static const int cache_block_size = 8;
        size_t new_size = akvcam_bound(cache_block_size,
                                       self->cache_index + cache_block_size,
                                       max_cache_alloc);
        akvcam_frame_convert_parameters_t fc =
                kzalloc(new_size * sizeof(akvcam_frame_convert_parameters),
                        GFP_KERNEL);
        akvcam_frame_convert_parameters_init(fc, new_size);

        if (self->fc) {
            akvcam_frame_convert_parameters_copy(fc, self->fc, self->fc_size);
            akvcam_frame_convert_parameters_delete(&self->fc, self->fc_size);
        }

        self->fc = fc;
        self->fc_size = new_size;
    }

    akvcam_frame_convert_parameters_t fc = self->fc + self->cache_index;
    akvcam_format_t frame_format = akvcam_frame_format_nr(frame);

    if (!akvcam_format_is_same_format(frame_format, fc->input_format)
        || !akvcam_format_is_same_format(output_format, fc->output_format)
        || self->yuv_color_space != fc->yuv_color_space
        || self->yuv_color_space_type != fc->yuv_color_space_type
        || self->scaling_mode != fc->scaling_mode
        || self->aspect_ratio_mode != fc->aspect_ratio_mode) {
        akvcam_frame_convert_parameters_configure(fc,
                                                  frame_format,
                                                  output_format,
                                                  fc->color_convert,
                                                  self->yuv_color_space,
                                                  self->yuv_color_space_type);
        akvcam_frame_convert_parameters_configure_scaling(fc,
                                                          frame_format,
                                                          output_format,
                                                          self->aspect_ratio_mode);

        if (fc->input_format)
            akvcam_format_delete(fc->input_format);

        fc->input_format = akvcam_format_new_copy(frame_format);

        if (fc->output_format)
            akvcam_format_delete(fc->output_format);

        fc->output_format = akvcam_format_new_copy(output_format);
        fc->yuv_color_space = self->yuv_color_space;
        fc->yuv_color_space_type = self->yuv_color_space_type;
        fc->scaling_mode = self->scaling_mode;
        fc->aspect_ratio_mode = self->aspect_ratio_mode;
    }


    if (akvcam_format_is_same_format(fc->output_convert_format, frame_format)) {
        self->cache_index++;

        return akvcam_frame_new_copy(frame);
    }

    if (fc->fast_convertion) {
        akvcam_converter_private_convert_fast_8bits(self,
                                                    fc,
                                                    frame,
                                                    fc->output_frame);
    } else {
        switch (fc->convert_data_types) {
        DEFINE_CONVERT_FUNC(8 , 8 )
        DEFINE_CONVERT_FUNC(8 , 16)
        DEFINE_CONVERT_FUNC(8 , 32)
        DEFINE_CONVERT_FUNC(16, 8 )
        DEFINE_CONVERT_FUNC(16, 16)
        DEFINE_CONVERT_FUNC(16, 32)
        DEFINE_CONVERT_FUNC(32, 8 )
        DEFINE_CONVERT_FUNC(32, 16)
        DEFINE_CONVERT_FUNC(32, 32)
        }
    }

    self->cache_index++;

    return akvcam_frame_new_copy(fc->output_frame);
}

void akvcam_converter_private_convert_fast_8bits(akvcam_converter_t self,
                                                 akvcam_frame_convert_parameters_ct fc,
                                                 akvcam_frame_ct src,
                                                 akvcam_frame_t dst)
{
    if (self->scaling_mode == AKVCAM_SCALING_MODE_LINEAR
        && fc->resize_mode == AKVCAM_RESIZE_MODE_UP) {
        switch (fc->convert_type) {
        case AKVCAM_CONVERT_TYPE_VECTOR:
            akvcam_converter_private_convert_fast_8bits_ulv3to3(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_3TO3:
            akvcam_converter_private_convert_fast_8bits_ul3to3(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_3TO1:
            akvcam_converter_private_convert_fast_8bits_ul3to1(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_1TO3:
            akvcam_converter_private_convert_fast_8bits_ul1to3(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_1TO1:
            akvcam_converter_private_convert_fast_8bits_ul1to1(fc, src, dst);
            break;
        }
    } else if (self->scaling_mode == AKVCAM_SCALING_MODE_LINEAR
               && fc->resize_mode == AKVCAM_RESIZE_MODE_DOWN) {
        switch (fc->convert_type) {
        case AKVCAM_CONVERT_TYPE_VECTOR:
            akvcam_converter_private_convert_fast_8bits_dlv3to3(fc, dst);
            break;
        case AKVCAM_CONVERT_TYPE_3TO3:
            akvcam_converter_private_convert_fast_8bits_dl3to3(fc, dst);
            break;
        case AKVCAM_CONVERT_TYPE_3TO1:
            akvcam_converter_private_convert_fast_8bits_dl3to1(fc, dst);
            break;
        case AKVCAM_CONVERT_TYPE_1TO3:
            akvcam_converter_private_convert_fast_8bits_dl1to3(fc, dst);
            break;
        case AKVCAM_CONVERT_TYPE_1TO1:
            akvcam_converter_private_convert_fast_8bits_dl1to1(fc, dst);
            break;
        }
    } else {
        switch (fc->convert_type) {
        case AKVCAM_CONVERT_TYPE_VECTOR:
            akvcam_converter_private_convert_fast_8bits_v3to3(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_3TO3:
            akvcam_converter_private_convert_fast_8bits_3to3(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_3TO1:
            akvcam_converter_private_convert_fast_8bits_3to1(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_1TO3:
            akvcam_converter_private_convert_fast_8bits_1to3(fc, src, dst);
            break;
        case AKVCAM_CONVERT_TYPE_1TO1:
            akvcam_converter_private_convert_fast_8bits_1to1(fc, src, dst);
            break;
        }
    }
}

void akvcam_frame_convert_parameters_init(akvcam_frame_convert_parameters_t fc,
                                          size_t size)
{
    static const akvcam_frame_convert_parameters akvcam_fc_initializer = {
        .color_convert = NULL,

        .input_format = NULL,
        .output_format = NULL,
        .output_convert_format = NULL,
        .output_frame = NULL,

        .yuv_color_space = AKVCAM_YUV_COLOR_SPACE_ITUR_BT601,
        .yuv_color_space_type = AKVCAM_YUV_COLOR_SPACE_TYPE_STUDIO_SWING,
        .scaling_mode = AKVCAM_SCALING_MODE_FAST,
        .aspect_ratio_mode = AKVCAM_ASPECT_RATIO_MODE_IGNORE,
        .convert_type = AKVCAM_CONVERT_TYPE_3TO3,
        .convert_data_types = AKVCAM_CONVERT_DATA_TYPES_8_8,
        .alpha_mode = AKVCAM_CONVERT_ALPHA_MODE_AI_AO,
        .resize_mode = AKVCAM_RESIZE_MODE_KEEP,

        .fast_convertion = false,

        .from_endian = __BYTE_ORDER__,
        .to_endian = __BYTE_ORDER__,

        .xmin = 0,
        .ymin = 0,
        .xmax = 0,
        .ymax = 0,

        .input_width = 0,
        .input_width_1 = 0,
        .input_height = 0,

        .src_width = NULL,
        .src_width_1 = NULL,
        .src_width_offset_x = NULL,
        .src_width_offset_y = NULL,
        .src_width_offset_z = NULL,
        .src_width_offset_a = NULL,
        .src_height = NULL,

        .dl_src_width_offset_x = NULL,
        .dl_src_width_offset_y = NULL,
        .dl_src_width_offset_z = NULL,
        .dl_src_width_offset_a = NULL,

        .src_width_offset_x_1 = NULL,
        .src_width_offset_y_1 = NULL,
        .src_width_offset_z_1 = NULL,
        .src_width_offset_a_1 = NULL,
        .src_height_1 = NULL,

        .dst_width_offset_x = NULL,
        .dst_width_offset_y = NULL,
        .dst_width_offset_z = NULL,
        .dst_width_offset_a = NULL,

        .src_height_dl_offset = NULL,
        .src_height_dl_offset_1 = NULL,

        .integral_image_data_x = NULL,
        .integral_image_data_y = NULL,
        .integral_image_data_z = NULL,
        .integral_image_data_a = NULL,

        .kx = NULL,
        .ky = NULL,
        .kdl = NULL,

        .plane_xi = 0,
        .plane_yi = 0,
        .plane_zi = 0,
        .plane_ai = 0,

        .comp_xi = NULL,
        .comp_yi = NULL,
        .comp_zi = NULL,
        .comp_ai = NULL,

        .plane_xo = 0,
        .plane_yo = 0,
        .plane_zo = 0,
        .plane_ao = 0,

        .comp_xo = NULL,
        .comp_yo = NULL,
        .comp_zo = NULL,
        .comp_ao = NULL,

        .xi_offset = 0,
        .yi_offset = 0,
        .zi_offset = 0,
        .ai_offset = 0,

        .xo_offset = 0,
        .yo_offset = 0,
        .zo_offset = 0,
        .ao_offset = 0,

        .xi_shift = 0,
        .yi_shift = 0,
        .zi_shift = 0,
        .ai_shift = 0,

        .xo_shift = 0,
        .yo_shift = 0,
        .zo_shift = 0,
        .ao_shift = 0,

        .max_xi = 0,
        .max_yi = 0,
        .max_zi = 0,
        .max_ai = 0,

        .mask_xo = 0,
        .mask_yo = 0,
        .mask_zo = 0,
        .mask_ao = 0,

        .alpha_mask = 0,
    };
    size_t i;

    for (i = 0; i < size; ++i) {
        akvcam_frame_convert_parameters_t fci = fc + i;

        if (fci->color_convert)
            akvcam_color_convert_delete(fci->color_convert);

        if (fci->input_format)
            akvcam_format_delete(fci->input_format);

        if (fci->output_format)
            akvcam_format_delete(fci->output_format);

        if (fci->output_convert_format)
            akvcam_format_delete(fci->output_convert_format);

        if (fci->output_frame)
            akvcam_frame_delete(fci->output_frame);

        memcpy(fci,
               &akvcam_fc_initializer,
               sizeof(akvcam_frame_convert_parameters));

        fci->color_convert = akvcam_color_convert_new();
        fci->input_format = akvcam_format_new(0, 0, 0, NULL);
        fci->output_format = akvcam_format_new(0, 0, 0, NULL);
        fci->output_convert_format = akvcam_format_new(0, 0, 0, NULL);
        fci->output_frame = akvcam_frame_new(NULL);
    }
}

void akvcam_frame_convert_parameters_copy(akvcam_frame_convert_parameters_t fc,
                                          akvcam_frame_convert_parameters_ct other,
                                          size_t size)
{
    size_t i;

    for (i = 0; i < size; ++i) {
        akvcam_frame_convert_parameters_t fci = fc + i;
        akvcam_frame_convert_parameters_ct otheri = other + i;

        if (fci->color_convert)
            akvcam_color_convert_delete(fci->color_convert);

        if (fci->input_format)
            akvcam_format_delete(fci->input_format);

        if (fci->output_format)
            akvcam_format_delete(fci->output_format);

        if (fci->output_convert_format)
            akvcam_format_delete(fci->output_convert_format);

        if (fci->output_frame)
            akvcam_frame_delete(fci->output_frame);

        memcpy(fci,
               otheri,
               sizeof(akvcam_frame_convert_parameters));

        fci->color_convert = akvcam_color_convert_new_copy(otheri->color_convert);
        fci->input_format = akvcam_format_new_copy(otheri->input_format);
        fci->output_format = akvcam_format_new_copy(otheri->output_format);
        fci->output_convert_format = akvcam_format_new_copy(otheri->output_convert_format);
        fci->output_frame = akvcam_frame_new_copy(otheri->output_frame);
    }
}

void akvcam_frame_convert_parameters_delete(akvcam_frame_convert_parameters_t *fc,
                                            size_t size)
{
    size_t i;

    if (fc && *fc) {
        for (i = 0; i < size; ++i) {
            akvcam_frame_convert_parameters_t fci = *fc + i;

            if (fci->color_convert)
                akvcam_color_convert_delete(fci->color_convert);

            if (fci->input_format)
                akvcam_format_delete(fci->input_format);

            if (fci->output_format)
                akvcam_format_delete(fci->output_format);

            if (fci->output_convert_format)
                akvcam_format_delete(fci->output_convert_format);

            if (fci->output_frame)
                akvcam_frame_delete(fci->output_frame);
        }

        kfree(*fc);
        *fc = NULL;
    }
}

#define DEFINE_CONVERT_TYPES(isize, osize) \
    if (akvcam_format_specs_depth(ispecs) == isize && akvcam_format_specs_depth(ospecs) == osize) \
        fc->convert_data_types = AKVCAM_CONVERT_DATA_TYPES_##isize##_##osize;

void akvcam_frame_convert_parameters_configure(akvcam_frame_convert_parameters_t fc,
                                               akvcam_format_ct iformat,
                                               akvcam_format_ct oformat,
                                               akvcam_color_convert_t color_convert,
                                               AKVCAM_YUV_COLOR_SPACE yuv_color_space,
                                               AKVCAM_YUV_COLOR_SPACE_TYPE yuv_color_space_type)
{
    __u32 ifourcc = akvcam_format_fourcc(iformat);
    __u32 ofourcc = akvcam_format_fourcc(oformat);

    akvcam_format_specs_ct ispecs =
            akvcam_format_specs_from_fixel_format(ifourcc);

    if (ofourcc == 0)
        ofourcc = ifourcc;

    akvcam_format_specs_ct ospecs =
            akvcam_format_specs_from_fixel_format(ofourcc);

    DEFINE_CONVERT_TYPES(8, 8);
    DEFINE_CONVERT_TYPES(8, 16);
    DEFINE_CONVERT_TYPES(8, 32);
    DEFINE_CONVERT_TYPES(16, 8);
    DEFINE_CONVERT_TYPES(16, 16);
    DEFINE_CONVERT_TYPES(16, 32);
    DEFINE_CONVERT_TYPES(32, 8);
    DEFINE_CONVERT_TYPES(32, 16);
    DEFINE_CONVERT_TYPES(32, 32);

    size_t icomponents = akvcam_format_specs_main_components(ispecs);
    size_t ocomponents = akvcam_format_specs_main_components(ospecs);

    if (icomponents == 3 && ispecs->type == ospecs->type)
        fc->convert_type = AKVCAM_CONVERT_TYPE_VECTOR;
    else if (icomponents == 3 && ocomponents == 3)
        fc->convert_type = AKVCAM_CONVERT_TYPE_3TO3;
    else if (icomponents == 3 && ocomponents == 1)
        fc->convert_type = AKVCAM_CONVERT_TYPE_3TO1;
    else if (icomponents == 1 && ocomponents == 3)
        fc->convert_type = AKVCAM_CONVERT_TYPE_1TO3;
    else if (icomponents == 1 && ocomponents == 1)
        fc->convert_type = AKVCAM_CONVERT_TYPE_1TO1;

    fc->from_endian = ispecs->endianness;
    fc->to_endian = ospecs->endianness;
    akvcam_color_convert_set_yuv_color_space(color_convert,
                                             yuv_color_space);
    akvcam_color_convert_set_yuv_color_space_type(color_convert,
                                                  yuv_color_space_type);
    akvcam_color_convert_load_matrix(color_convert, ispecs, ospecs);

    fc->comp_xi = NULL;
    fc->comp_yi = NULL;
    fc->comp_zi = NULL;
    fc->comp_ai = NULL;

    switch (ispecs->type) {
    case AKVCAM_VIDEO_FORMAT_TYPE_RGB:
        fc->plane_xi = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_R);
        fc->plane_yi = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_G);
        fc->plane_zi = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_B);

        fc->comp_xi = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_R);
        fc->comp_yi = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_G);
        fc->comp_zi = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_B);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_YUV:
        fc->plane_xi = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_Y);
        fc->plane_yi = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_U);
        fc->plane_zi = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_V);

        fc->comp_xi = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_Y);
        fc->comp_yi = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_U);
        fc->comp_zi = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_V);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_GRAY:
        fc->plane_xi = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_Y);
        fc->comp_xi = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_Y);

        break;

    default:
        break;
    }

    fc->plane_ai = akvcam_format_specs_component_plane(ispecs, AKVCAM_COMPONENT_TYPE_A);
    fc->comp_ai = akvcam_format_specs_component(ispecs, AKVCAM_COMPONENT_TYPE_A);

    fc->comp_xo = NULL;
    fc->comp_yo = NULL;
    fc->comp_zo = NULL;
    fc->comp_ao = NULL;

    switch (ospecs->type) {
    case AKVCAM_VIDEO_FORMAT_TYPE_RGB:
        fc->plane_xo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_R);
        fc->plane_yo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_G);
        fc->plane_zo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_B);

        fc->comp_xo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_R);
        fc->comp_yo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_G);
        fc->comp_zo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_B);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_YUV:
        fc->plane_xo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_Y);
        fc->plane_yo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_U);
        fc->plane_zo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_V);

        fc->comp_xo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_Y);
        fc->comp_yo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_U);
        fc->comp_zo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_V);

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_GRAY:
        fc->plane_xo = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_Y);
        fc->comp_xo = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_Y);

        break;

    default:
        break;
    }

    fc->plane_ao = akvcam_format_specs_component_plane(ospecs, AKVCAM_COMPONENT_TYPE_A);
    fc->comp_ao = akvcam_format_specs_component(ospecs, AKVCAM_COMPONENT_TYPE_A);

    fc->xi_offset = fc->comp_xi? fc->comp_xi->offset: 0;
    fc->yi_offset = fc->comp_yi? fc->comp_yi->offset: 0;
    fc->zi_offset = fc->comp_zi? fc->comp_zi->offset: 0;
    fc->ai_offset = fc->comp_ai? fc->comp_ai->offset: 0;

    fc->xo_offset = fc->comp_xo? fc->comp_xo->offset: 0;
    fc->yo_offset = fc->comp_yo? fc->comp_yo->offset: 0;
    fc->zo_offset = fc->comp_zo? fc->comp_zo->offset: 0;
    fc->ao_offset = fc->comp_ao? fc->comp_ao->offset: 0;

    fc->xi_shift = fc->comp_xi? fc->comp_xi->shift: 0;
    fc->yi_shift = fc->comp_yi? fc->comp_yi->shift: 0;
    fc->zi_shift = fc->comp_zi? fc->comp_zi->shift: 0;
    fc->ai_shift = fc->comp_ai? fc->comp_ai->shift: 0;

    fc->xo_shift = fc->comp_xo? fc->comp_xo->shift: 0;
    fc->yo_shift = fc->comp_yo? fc->comp_yo->shift: 0;
    fc->zo_shift = fc->comp_zo? fc->comp_zo->shift: 0;
    fc->ao_shift = fc->comp_ao? fc->comp_ao->shift: 0;

    fc->max_xi = akvcam_color_component_max(fc->comp_xi);
    fc->max_yi = akvcam_color_component_max(fc->comp_yi);
    fc->max_zi = akvcam_color_component_max(fc->comp_zi);
    fc->max_ai = akvcam_color_component_max(fc->comp_ai);

    fc->mask_xo = ~(akvcam_color_component_max(fc->comp_xo) << fc->xo_shift);
    fc->mask_yo = ~(akvcam_color_component_max(fc->comp_yo) << fc->yo_shift);
    fc->mask_zo = ~(akvcam_color_component_max(fc->comp_zo) << fc->zo_shift);
    fc->alpha_mask = akvcam_color_component_max(fc->comp_ao) << fc->ao_shift;
    fc->mask_ao = ~fc->alpha_mask;

    bool has_alpha_in = akvcam_format_specs_contains(ispecs,
                                                     AKVCAM_COMPONENT_TYPE_A);
    bool has_alpha_out = akvcam_format_specs_contains(ospecs,
                                                      AKVCAM_COMPONENT_TYPE_A);

    if (has_alpha_in && has_alpha_out)
        fc->alpha_mode = AKVCAM_CONVERT_ALPHA_MODE_AI_AO;
    else if (has_alpha_in && !has_alpha_out)
        fc->alpha_mode = AKVCAM_CONVERT_ALPHA_MODE_AI_O;
    else if (!has_alpha_in && has_alpha_out)
        fc->alpha_mode = AKVCAM_CONVERT_ALPHA_MODE_I_AO;
    else if (!has_alpha_in && !has_alpha_out)
        fc->alpha_mode = AKVCAM_CONVERT_ALPHA_MODE_I_O;

    fc->fast_convertion = akvcam_format_specs_is_fast(ispecs)
                          && akvcam_format_specs_is_fast(ospecs);
}

void akvcam_frame_convert_parameters_configure_scaling(akvcam_frame_convert_parameters_t fc,
                                                       akvcam_format_ct iformat,
                                                       akvcam_format_ct oformat,
                                                       AKVCAM_ASPECT_RATIO_MODE aspect_ratio_mode)
{
    akvcam_rect irect = {
        0,
        0,
        akvcam_format_width(iformat),
        akvcam_format_height(iformat)
    };

    int output_convert_format_fourcc = akvcam_format_fourcc(oformat);

    if (output_convert_format_fourcc == 0)
        output_convert_format_fourcc = akvcam_format_fourcc(iformat);

    int output_convert_format_width = akvcam_format_width(oformat);
    int output_convert_format_height = akvcam_format_height(oformat);

    int width = output_convert_format_width > 1?
                    output_convert_format_width:
                    irect.width;
    int height = output_convert_format_height > 1?
                     output_convert_format_height:
                     irect.height;
    int owidth = width;
    int oheight = height;

    if (aspect_ratio_mode == AKVCAM_ASPECT_RATIO_MODE_KEEP
        || aspect_ratio_mode == AKVCAM_ASPECT_RATIO_MODE_FIT) {
        int w = height * irect.width / irect.height;
        int h = width * irect.height / irect.width;

        if (w > width)
            w = width;
        else if (h > height)
            h = height;

        owidth = w;
        oheight = h;

        if (aspect_ratio_mode == AKVCAM_ASPECT_RATIO_MODE_KEEP) {
            width = owidth;
            height = oheight;
        }
    }

    if (fc->output_convert_format)
        akvcam_format_delete(fc->output_convert_format);

    struct v4l2_fract frame_rate = akvcam_format_frame_rate(iformat);
    fc->output_convert_format =
            akvcam_format_new(output_convert_format_fourcc,
                              width,
                              height,
                              &frame_rate);

    fc->xmin = (width - owidth) / 2;
    fc->ymin = (height - oheight) / 2;
    fc->xmax = (width + owidth) / 2;
    fc->ymax = (height + oheight) / 2;

    if (owidth > irect.width
        || oheight > irect.height)
        fc->resize_mode = AKVCAM_RESIZE_MODE_UP;
    else if (owidth < irect.width
             || oheight < irect.height)
        fc->resize_mode = AKVCAM_RESIZE_MODE_DOWN;
    else
        fc->resize_mode = AKVCAM_RESIZE_MODE_KEEP;

    if (aspect_ratio_mode == AKVCAM_ASPECT_RATIO_MODE_EXPANDING) {
        int w = irect.height * owidth / oheight;
        int h = irect.width * oheight / owidth;

        if (w > irect.width)
            w = irect.width;

        if (h > irect.height)
            h = irect.height;

        int x = irect.x + (irect.width - w) / 2;
        int y = irect.y + (irect.height - h) / 2;
        irect.x = x;
        irect.y = y;
        irect.width = w;
        irect.height = h;
    }

    akvcam_frame_convert_parameters_allocate_buffers(fc,
                                                     fc->output_convert_format);
    int iformat_width = akvcam_format_width(iformat);
    int iformat_height = akvcam_format_height(iformat);

    int wi_1 = akvcam_max(1, irect.width - 1);
    int wo_1 = akvcam_max(1, owidth - 1);

#define x_src_to_dst(v) ((((v) - irect.x) * wo_1 + fc->xmin * wi_1) / wi_1)
#define x_dst_to_src(v) ((((v) - fc->xmin) * wi_1 + irect.x * wo_1) / wo_1)

    for (int x = 0; x < output_convert_format_width; ++x) {
        int xs = x_dst_to_src(x);
        int xs_1 = x_dst_to_src(akvcam_min(x + 1, output_convert_format_width - 1));
        int xmin = x_src_to_dst(xs);
        int xmax = x_src_to_dst(xs + 1);

        fc->src_width[x] = xs;
        fc->src_width_1[x] = akvcam_min(x_dst_to_src(x + 1), iformat_width);
        fc->src_width_offset_x[x] = fc->comp_xi? (xs >> fc->comp_xi->width_div) * fc->comp_xi->step: 0;
        fc->src_width_offset_y[x] = fc->comp_yi? (xs >> fc->comp_yi->width_div) * fc->comp_yi->step: 0;
        fc->src_width_offset_z[x] = fc->comp_zi? (xs >> fc->comp_zi->width_div) * fc->comp_zi->step: 0;
        fc->src_width_offset_a[x] = fc->comp_ai? (xs >> fc->comp_ai->width_div) * fc->comp_ai->step: 0;

        fc->src_width_offset_x_1[x] = fc->comp_xi? (xs_1 >> fc->comp_xi->width_div) * fc->comp_xi->step: 0;
        fc->src_width_offset_y_1[x] = fc->comp_yi? (xs_1 >> fc->comp_yi->width_div) * fc->comp_yi->step: 0;
        fc->src_width_offset_z_1[x] = fc->comp_zi? (xs_1 >> fc->comp_zi->width_div) * fc->comp_zi->step: 0;
        fc->src_width_offset_a_1[x] = fc->comp_ai? (xs_1 >> fc->comp_ai->width_div) * fc->comp_ai->step: 0;

        fc->dst_width_offset_x[x] = fc->comp_xo? (x >> fc->comp_xo->width_div) * fc->comp_xo->step: 0;
        fc->dst_width_offset_y[x] = fc->comp_yo? (x >> fc->comp_yo->width_div) * fc->comp_yo->step: 0;
        fc->dst_width_offset_z[x] = fc->comp_zo? (x >> fc->comp_zo->width_div) * fc->comp_zo->step: 0;
        fc->dst_width_offset_a[x] = fc->comp_ao? (x >> fc->comp_ao->width_div) * fc->comp_ao->step: 0;

        if (xmax > xmin)
            fc->kx[x] = SCALE_EMULT * (x - xmin) / (xmax - xmin);
        else
            fc->kx[x] = 0;
    }


    int hi_1 = akvcam_max(1, irect.height - 1);
    int ho_1 = akvcam_max(1, oheight - 1);

#define y_src_to_dst(v) ((((v) - irect.y) * ho_1 + fc->ymin * hi_1) / hi_1)
#define y_dst_to_src(v) ((((v) - fc->ymin) * hi_1 + irect.y * ho_1) / ho_1)

    for (int y = 0; y < output_convert_format_height; ++y) {
        if (fc->resize_mode == AKVCAM_RESIZE_MODE_DOWN) {
            fc->src_height[y] = y_dst_to_src(y);
            fc->src_height_1[y] = akvcam_min(y_dst_to_src(y + 1), iformat_height);
        } else {
            int ys = y_dst_to_src(y);
            int ys_1 = y_dst_to_src(akvcam_min(y + 1, output_convert_format_height - 1));
            int ymin = y_src_to_dst(ys);
            int ymax = y_src_to_dst(ys + 1);

            fc->src_height[y] = ys;
            fc->src_height_1[y] = ys_1;

            if (ymax > ymin)
                fc->ky[y] = SCALE_EMULT * (y - ymin) / (ymax - ymin);
            else
                fc->ky[y] = 0;
        }
    }

    fc->input_width = iformat_width;
    fc->input_width_1 = iformat_width + 1;
    fc->input_height = iformat_height;

    akvcam_frame_convert_parameters_clear_dl_buffers(fc);

    if (fc->resize_mode == AKVCAM_RESIZE_MODE_DOWN) {
        akvcam_frame_convert_parameters_allocate_dl_buffers(fc,
                                                            iformat,
                                                            fc->output_convert_format);

        for (int x = 0; x < iformat_width; ++x) {
            fc->dl_src_width_offset_x[x] = fc->comp_xi? (x >> fc->comp_xi->width_div) * fc->comp_xi->step: 0;
            fc->dl_src_width_offset_y[x] = fc->comp_yi? (x >> fc->comp_yi->width_div) * fc->comp_yi->step: 0;
            fc->dl_src_width_offset_z[x] = fc->comp_zi? (x >> fc->comp_zi->width_div) * fc->comp_zi->step: 0;
            fc->dl_src_width_offset_a[x] = fc->comp_ai? (x >> fc->comp_ai->width_div) * fc->comp_ai->step: 0;
        }

        for (int y = 0; y < output_convert_format_height; ++y) {
            int ys = fc->src_height[y];
            int ys_1 = fc->src_height_1[y];

            fc->src_height_dl_offset[y] = (size_t) ys * fc->input_width_1;
            fc->src_height_dl_offset_1[y] = (size_t) ys_1 * fc->input_width_1;

            int diff_y = ys_1 - ys;
            uint64_t *line = fc->kdl + (size_t) y * output_convert_format_width;

            for (int x = 0; x < output_convert_format_width; ++x) {
                int diff_x = fc->src_width_1[x] - fc->src_width[x];
                int area = diff_x * diff_y;
                line[x] = area > 0? area: 1;
            }
        }
    }

    if (fc->output_frame)
        akvcam_frame_delete(fc->output_frame);

    fc->output_frame = akvcam_frame_new(fc->output_convert_format);

    if (aspect_ratio_mode == AKVCAM_ASPECT_RATIO_MODE_FIT)
        akvcam_frame_fill_rgba(fc->output_frame, akvcam_xyza(0, 0, 0, 0));
}

void akvcam_frame_convert_parameters_allocate_buffers(akvcam_frame_convert_parameters_t fc,
                                                      akvcam_format_ct oformat)
{
    akvcam_frame_convert_parameters_clear_buffers(fc);
    int width = akvcam_format_width(oformat);
    int height = akvcam_format_height(oformat);

    fc->src_width = vzalloc(width * sizeof(int));
    fc->src_width_1 = vzalloc(width * sizeof(int));
    fc->src_width_offset_x = vzalloc(width * sizeof(int));
    fc->src_width_offset_y = vzalloc(width * sizeof(int));
    fc->src_width_offset_z = vzalloc(width * sizeof(int));
    fc->src_width_offset_a = vzalloc(width * sizeof(int));
    fc->src_height = vzalloc(height * sizeof(int));

    fc->src_width_offset_x_1 = vzalloc(width * sizeof(int));
    fc->src_width_offset_y_1 = vzalloc(width * sizeof(int));
    fc->src_width_offset_z_1 = vzalloc(width * sizeof(int));
    fc->src_width_offset_a_1 = vzalloc(width * sizeof(int));
    fc->src_height_1 = vzalloc(height * sizeof(int));

    fc->dst_width_offset_x = vzalloc(width * sizeof(int));
    fc->dst_width_offset_y = vzalloc(width * sizeof(int));
    fc->dst_width_offset_z = vzalloc(width * sizeof(int));
    fc->dst_width_offset_a = vzalloc(width * sizeof(int));

    fc->kx = vzalloc(width * sizeof(int64_t));
    fc->ky = vzalloc(height * sizeof(int64_t));
}

void akvcam_frame_convert_parameters_allocate_dl_buffers(akvcam_frame_convert_parameters_t fc,
                                                         akvcam_format_ct iformat,
                                                         akvcam_format_ct oformat)
{
    size_t iwidth = akvcam_format_width(iformat);
    size_t iheight = akvcam_format_height(iformat);
    size_t owidth  = akvcam_format_width(oformat);
    size_t oheight = akvcam_format_height(oformat);
    size_t width_1 = iwidth + 1;
    size_t height_1 = iheight + 1;
    size_t integral_image_size = width_1 * height_1;

    fc->integral_image_data_x = vzalloc(integral_image_size * sizeof(uint64_t));
    fc->integral_image_data_y = vzalloc(integral_image_size * sizeof(uint64_t));
    fc->integral_image_data_z = vzalloc(integral_image_size * sizeof(uint64_t));
    fc->integral_image_data_a = vzalloc(integral_image_size * sizeof(uint64_t));

    size_t kdl_size = (size_t) owidth * oheight;
    fc->kdl = vzalloc(kdl_size * sizeof(uint64_t));
    memset(fc->kdl, 0, kdl_size * sizeof(uint64_t));

    fc->src_height_dl_offset = vzalloc(oheight * sizeof(size_t));
    fc->src_height_dl_offset_1 = vzalloc(oheight * sizeof(size_t));

    fc->dl_src_width_offset_x = vzalloc(iwidth * sizeof(int));
    fc->dl_src_width_offset_y = vzalloc(iwidth * sizeof(int));
    fc->dl_src_width_offset_z = vzalloc(iwidth * sizeof(int));
    fc->dl_src_width_offset_a = vzalloc(iwidth * sizeof(int));
}

void akvcam_frame_convert_parameters_clear_buffers(akvcam_frame_convert_parameters_t fc)
{
    if (fc->src_width) {
        vfree(fc->src_width);
        fc->src_width = NULL;
    }

    if (fc->src_width_1) {
        vfree(fc->src_width_1);
        fc->src_width_1 = NULL;
    }

    if (fc->src_width_offset_x) {
        vfree(fc->src_width_offset_x);
        fc->src_width_offset_x = NULL;
    }

    if (fc->src_width_offset_y) {
        vfree(fc->src_width_offset_y);
        fc->src_width_offset_y = NULL;
    }

    if (fc->src_width_offset_z) {
        vfree(fc->src_width_offset_z);
        fc->src_width_offset_z = NULL;
    }

    if (fc->src_width_offset_a) {
        vfree(fc->src_width_offset_a);
        fc->src_width_offset_a = NULL;
    }

    if (fc->src_height) {
        vfree(fc->src_height);
        fc->src_height = NULL;
    }

    if (fc->src_width_offset_x_1) {
        vfree(fc->src_width_offset_x_1);
        fc->src_width_offset_x_1 = NULL;
    }

    if (fc->src_width_offset_y_1) {
        vfree(fc->src_width_offset_y_1);
        fc->src_width_offset_y_1 = NULL;
    }

    if (fc->src_width_offset_z_1) {
        vfree(fc->src_width_offset_z_1);
        fc->src_width_offset_z_1 = NULL;
    }

    if (fc->src_width_offset_a_1) {
        vfree(fc->src_width_offset_a_1);
        fc->src_width_offset_a_1 = NULL;
    }

    if (fc->src_height_1) {
        vfree(fc->src_height_1);
        fc->src_height_1 = NULL;
    }

    if (fc->dst_width_offset_x) {
        vfree(fc->dst_width_offset_x);
        fc->dst_width_offset_x = NULL;
    }

    if (fc->dst_width_offset_y) {
        vfree(fc->dst_width_offset_y);
        fc->dst_width_offset_y = NULL;
    }

    if (fc->dst_width_offset_z) {
        vfree(fc->dst_width_offset_z);
        fc->dst_width_offset_z = NULL;
    }

    if (fc->dst_width_offset_a) {
        vfree(fc->dst_width_offset_a);
        fc->dst_width_offset_a = NULL;
    }

    if (fc->kx) {
        vfree(fc->kx);
        fc->kx = NULL;
    }

    if (fc->ky) {
        vfree(fc->ky);
        fc->ky = NULL;
    }
}

void akvcam_frame_convert_parameters_clear_dl_buffers(akvcam_frame_convert_parameters_t fc)
{
    if (fc->integral_image_data_x) {
        vfree(fc->integral_image_data_x);
        fc->integral_image_data_x = NULL;
    }

    if (fc->integral_image_data_y) {
        vfree(fc->integral_image_data_y);
        fc->integral_image_data_y = NULL;
    }

    if (fc->integral_image_data_z) {
        vfree(fc->integral_image_data_z);
        fc->integral_image_data_z = NULL;
    }

    if (fc->integral_image_data_a) {
        vfree(fc->integral_image_data_a);
        fc->integral_image_data_a = NULL;
    }

    if (fc->kdl) {
        vfree(fc->kdl);
        fc->kdl = NULL;
    }

    if (fc->src_height_dl_offset) {
        vfree(fc->src_height_dl_offset);
        fc->src_height_dl_offset = NULL;
    }

    if (fc->src_height_dl_offset_1) {
        vfree(fc->src_height_dl_offset_1);
        fc->src_height_dl_offset_1 = NULL;
    }

    if (fc->dl_src_width_offset_x) {
        vfree(fc->dl_src_width_offset_x);
        fc->dl_src_width_offset_x = NULL;
    }

    if (fc->dl_src_width_offset_y) {
        vfree(fc->dl_src_width_offset_y);
        fc->dl_src_width_offset_y = NULL;
    }

    if (fc->dl_src_width_offset_z) {
        vfree(fc->dl_src_width_offset_z);
        fc->dl_src_width_offset_z = NULL;
    }

    if (fc->dl_src_width_offset_a) {
        vfree(fc->dl_src_width_offset_a);
        fc->dl_src_width_offset_a = NULL;
    }
}
