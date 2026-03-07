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

#include <linux/videodev2.h>

#include "format_specs.h"
#include "utils.h"

#define VFT_UNKNOWN AKVCAM_VIDEO_FORMAT_TYPE_UNKNOWN
#define VFT_RGB     AKVCAM_VIDEO_FORMAT_TYPE_RGB
#define VFT_YUV     AKVCAM_VIDEO_FORMAT_TYPE_YUV
#define VFT_GRAY    AKVCAM_VIDEO_FORMAT_TYPE_GRAY

#define CT_END AKVCAM_COMPONENT_TYPE_UNKNOWN
#define CT_R   AKVCAM_COMPONENT_TYPE_R
#define CT_G   AKVCAM_COMPONENT_TYPE_G
#define CT_B   AKVCAM_COMPONENT_TYPE_B
#define CT_Y   AKVCAM_COMPONENT_TYPE_Y
#define CT_U   AKVCAM_COMPONENT_TYPE_U
#define CT_V   AKVCAM_COMPONENT_TYPE_V
#define CT_A   AKVCAM_COMPONENT_TYPE_A

static const akvcam_format_specs akvcam_format_specs_table[] = {
    // RGB formats (1 or 2 bytes per pixel)

    {V4L2_PIX_FMT_RGB555,
     "RGB15",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{3, {{CT_R, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_ARGB555,
     "ARGB555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{4, {{CT_A, 2, 0, 15, 2, 1, 0, 0},
           {CT_R, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_XRGB555,
     "XRGB555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{3, {{CT_R, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_RGBA555,
     "RGBA555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{4, {{CT_R, 2, 0, 11, 2, 5, 0, 0},
           {CT_G, 2, 0,  6, 2, 5, 0, 0},
           {CT_B, 2, 0,  1, 2, 5, 0, 0},
           {CT_A, 2, 0,  0, 2, 1, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_RGBX555,
     "RGBX555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{3, {{CT_R, 2, 0, 11, 2, 5, 0, 0},
           {CT_G, 2, 0,  6, 2, 5, 0, 0},
           {CT_B, 2, 0,  1, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_ABGR555,
     "ABGR555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{4, {{CT_A, 2, 0, 15, 2, 1, 0, 0},
           {CT_B, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_R, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_XBGR555,
     "XBGR555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{3, {{CT_B, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_R, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_BGRA555,
     "BGRA555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{4, {{CT_B, 2, 0, 11, 2, 5, 0, 0},
           {CT_G, 2, 0,  6, 2, 5, 0, 0},
           {CT_R, 2, 0,  1, 2, 5, 0, 0},
           {CT_A, 2, 0,  0, 2, 1, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_BGRX555,
     "BGRX555",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{3, {{CT_B, 2, 0, 11, 2, 4, 0, 0},
           {CT_G, 2, 0,  6, 2, 4, 0, 0},
           {CT_R, 2, 0,  1, 2, 4, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_RGB565,
     "RGB16",
     VFT_RGB,
     __ORDER_LITTLE_ENDIAN__,
     1,
     {{3, {{CT_R, 2, 0, 11, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 6, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_RGB555X,
     "RGB555X",
     VFT_RGB,
     __ORDER_BIG_ENDIAN__,
     1,
     {{3, {{CT_R, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_ARGB555X,
     "ARGB555X",
     VFT_RGB,
     __ORDER_BIG_ENDIAN__,
     1,
     {{4, {{CT_A, 2, 0, 15, 2, 1, 0, 0},
           {CT_R, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_XRGB555X,
     "XRGB555X",
     VFT_RGB,
     __ORDER_BIG_ENDIAN__,
     1,
     {{3, {{CT_R, 2, 0, 10, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 5, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},
    {V4L2_PIX_FMT_RGB565X,
     "RGB565X",
     VFT_RGB,
     __ORDER_BIG_ENDIAN__,
     1,
     {{3, {{CT_R, 2, 0, 11, 2, 5, 0, 0},
           {CT_G, 2, 0,  5, 2, 6, 0, 0},
           {CT_B, 2, 0,  0, 2, 5, 0, 0}}, 16}
     }},

    // RGB formats (3 or 4 bytes per pixel)

    {V4L2_PIX_FMT_BGR24,
     "BGR24",
     VFT_RGB,
     __BYTE_ORDER__,
     1,
     {{3, {{CT_B, 3, 0, 0, 1, 8, 0, 0},
           {CT_G, 3, 1, 0, 1, 8, 0, 0},
           {CT_R, 3, 2, 0, 1, 8, 0, 0}}, 24}
     }},
    {V4L2_PIX_FMT_RGB24,
     "RGB24",
     VFT_RGB,
     __BYTE_ORDER__,
     1,
     {{3, {{CT_R, 3, 0, 0, 1, 8, 0, 0},
           {CT_G, 3, 1, 0, 1, 8, 0, 0},
           {CT_B, 3, 2, 0, 1, 8, 0, 0}}, 24}
     }},
     {V4L2_PIX_FMT_BGR32,
      "BGR32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_B, 4, 1, 0, 1, 8, 0, 0},
            {CT_G, 4, 2, 0, 1, 8, 0, 0},
            {CT_R, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_ABGR32,
      "ABGR32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{4, {{CT_A, 4, 0, 0, 1, 8, 0, 0},
            {CT_B, 4, 1, 0, 1, 8, 0, 0},
            {CT_G, 4, 2, 0, 1, 8, 0, 0},
            {CT_R, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_XBGR32,
      "XBGR32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_B, 4, 1, 0, 1, 8, 0, 0},
            {CT_G, 4, 2, 0, 1, 8, 0, 0},
            {CT_R, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_BGRA32,
      "BGRA32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{4, {{CT_B, 4, 0, 0, 1, 8, 0, 0},
            {CT_G, 4, 1, 0, 1, 8, 0, 0},
            {CT_R, 4, 2, 0, 1, 8, 0, 0},
            {CT_A, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_BGRX32,
      "BGRX32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_B, 4, 0, 0, 1, 8, 0, 0},
            {CT_G, 4, 1, 0, 1, 8, 0, 0},
            {CT_R, 4, 2, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_RGB32,
      "RGB32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_R, 4, 1, 0, 1, 8, 0, 0},
            {CT_G, 4, 2, 0, 1, 8, 0, 0},
            {CT_B, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_RGBA32,
      "RGBA32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{4, {{CT_R, 4, 0, 0, 1, 8, 0, 0},
            {CT_G, 4, 1, 0, 1, 8, 0, 0},
            {CT_B, 4, 2, 0, 1, 8, 0, 0},
            {CT_A, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_RGBX32,
      "RGBX32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_R, 4, 0, 0, 1, 8, 0, 0},
            {CT_G, 4, 1, 0, 1, 8, 0, 0},
            {CT_B, 4, 2, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_ARGB32,
      "ARGB32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{4, {{CT_A, 4, 0, 0, 1, 8, 0, 0},
            {CT_R, 4, 1, 0, 1, 8, 0, 0},
            {CT_G, 4, 2, 0, 1, 8, 0, 0},
            {CT_B, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},
     {V4L2_PIX_FMT_XRGB32,
      "XRGB32",
      VFT_RGB,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_R, 4, 1, 0, 1, 8, 0, 0},
            {CT_G, 4, 2, 0, 1, 8, 0, 0},
            {CT_B, 4, 3, 0, 1, 8, 0, 0}}, 32}
      }},

    // Luminance+Chrominance formats

     {V4L2_PIX_FMT_YUYV,
      "YUY2",
      VFT_YUV,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_Y, 2, 0, 0, 1, 8, 0, 0},
            {CT_U, 4, 1, 0, 1, 8, 1, 0},
            {CT_V, 4, 3, 0, 1, 8, 1, 0}}, 16}
      }},
     {V4L2_PIX_FMT_YVYU,
      "YVYU",
      VFT_YUV,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_Y, 2, 0, 0, 1, 8, 0, 0},
            {CT_V, 4, 1, 0, 1, 8, 1, 0},
            {CT_U, 4, 3, 0, 1, 8, 1, 0}}, 16}
      }},
     {V4L2_PIX_FMT_UYVY,
      "UYVY",
      VFT_YUV,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_U, 4, 0, 0, 1, 8, 1, 0},
            {CT_Y, 2, 1, 0, 1, 8, 0, 0},
            {CT_V, 4, 2, 0, 1, 8, 1, 0}}, 16}
      }},
     {V4L2_PIX_FMT_VYUY,
      "VYUY",
      VFT_YUV,
      __BYTE_ORDER__,
      1,
      {{3, {{CT_V, 4, 0, 0, 1, 8, 1, 0},
            {CT_Y, 2, 1, 0, 1, 8, 0, 0},
            {CT_U, 4, 2, 0, 1, 8, 1, 0}}, 16}
      }},

    // two planes -- one Y, one Cr + Cb interleaved

     {V4L2_PIX_FMT_NV12,
      "NV12",
      VFT_YUV,
      __BYTE_ORDER__,
      2,
      {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
       {2, {{CT_U, 2, 0, 0, 1, 8, 1, 1},
            {CT_V, 2, 1, 0, 1, 8, 1, 1}}, 8}
      }},
     {V4L2_PIX_FMT_NV21,
      "NV21",
      VFT_YUV,
      __BYTE_ORDER__,
      2,
      {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
       {2, {{CT_V, 2, 0, 0, 1, 8, 1, 1},
            {CT_U, 2, 1, 0, 1, 8, 1, 1}}, 8}
      }},
     {V4L2_PIX_FMT_NV16,
      "NV16",
      VFT_YUV,
      __BYTE_ORDER__,
      2,
      {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
       {2, {{CT_U, 2, 0, 0, 1, 8, 1, 0},
            {CT_V, 2, 1, 0, 1, 8, 1, 0}}, 8}
      }},
     {V4L2_PIX_FMT_NV61,
      "NV61",
      VFT_YUV,
      __BYTE_ORDER__,
      2,
      {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
       {2, {{CT_V, 2, 0, 0, 1, 8, 1, 0},
            {CT_U, 2, 1, 0, 1, 8, 1, 0}}, 8}
      }},

    // three planes - Y Cb, Cr

     {V4L2_PIX_FMT_YUV410,
      "YUV410",
      VFT_YUV,
      __BYTE_ORDER__,
      3,
      {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
       {1, {{CT_U, 1, 0, 0, 1, 8, 2, 2}}, 2},
       {1, {{CT_V, 1, 0, 0, 1, 8, 2, 2}}, 2}
      }},
      {V4L2_PIX_FMT_YVU410,
       "YVU410",
       VFT_YUV,
       __BYTE_ORDER__,
       3,
       {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
        {1, {{CT_V, 1, 0, 0, 1, 8, 2, 2}}, 2},
        {1, {{CT_U, 1, 0, 0, 1, 8, 2, 2}}, 2}
       }},
      {V4L2_PIX_FMT_YUV411P,
       "YUV411P",
       VFT_YUV,
       __BYTE_ORDER__,
       3,
       {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
        {1, {{CT_U, 1, 0, 0, 1, 8, 2, 0}}, 2},
        {1, {{CT_V, 1, 0, 0, 1, 8, 2, 0}}, 2}
       }},
      {V4L2_PIX_FMT_YUV420,
       "YUV420",
       VFT_YUV,
       __BYTE_ORDER__,
       3,
       {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
        {1, {{CT_U, 1, 0, 0, 1, 8, 1, 1}}, 4},
        {1, {{CT_V, 1, 0, 0, 1, 8, 1, 1}}, 4}
       }},
      {V4L2_PIX_FMT_YVU420,
       "YVU420",
       VFT_YUV,
       __BYTE_ORDER__,
       3,
       {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
        {1, {{CT_V, 1, 0, 0, 1, 8, 1, 1}}, 4},
        {1, {{CT_U, 1, 0, 0, 1, 8, 1, 1}}, 4}
       }},
      {V4L2_PIX_FMT_YUV422P,
       "YUV422P",
       VFT_YUV,
       __BYTE_ORDER__,
       3,
       {{1, {{CT_Y, 1, 0, 0, 1, 8, 0, 0}}, 8},
        {1, {{CT_U, 1, 0, 0, 1, 8, 1, 0}}, 4},
        {1, {{CT_V, 1, 0, 0, 1, 8, 1, 0}}, 4}
       }},

     // End

      {0,
       "",
       VFT_UNKNOWN,
       __BYTE_ORDER__,
       0,
       {}},
};

akvcam_format_specs_ct akvcam_format_specs_from_fixel_format(__u32 pixel_format)
{
    akvcam_format_specs_ct fmt = akvcam_format_specs_table;

    for (; fmt->fourcc != 0; fmt++)
        if (fmt->fourcc == pixel_format)
            return fmt;

    return NULL;
}

akvcam_color_component_ct akvcam_format_specs_component(akvcam_format_specs_ct self,
                                                        AKVCAM_COMPONENT_TYPE component_type)
{
    for (size_t j = 0; j < self->nplanes; ++j)
        for (size_t i = 0; i < self->planes[j].ncomponents; ++i)
            if (self->planes[j].components[i].type == component_type)
                return self->planes[j].components + i;

    return NULL;
}

int akvcam_format_specs_component_plane(akvcam_format_specs_ct self,
                                        AKVCAM_COMPONENT_TYPE component_type)
{
    for (size_t j = 0; j < self->nplanes; ++j)
        for (size_t i = 0; i < self->planes[j].ncomponents; ++i)
            if (self->planes[j].components[i].type == component_type)
                return j;

    return -1;
}

bool akvcam_format_specs_contains(akvcam_format_specs_ct self,
                                  AKVCAM_COMPONENT_TYPE component_type)
{
    for (size_t j = 0; j < self->nplanes; ++j)
        for (size_t i = 0; i < self->planes[j].ncomponents; ++i)
            if (self->planes[j].components[i].type == component_type)
                return true;

    return false;
}

size_t akvcam_format_specs_byte_depth(akvcam_format_specs_ct self)
{
    if (self->type == AKVCAM_VIDEO_FORMAT_TYPE_UNKNOWN)
        return 0;
    else if (self->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB)
        return akvcam_format_specs_component(self, AKVCAM_COMPONENT_TYPE_R)->byte_depth;

    return akvcam_format_specs_component(self, AKVCAM_COMPONENT_TYPE_Y)->byte_depth;
}

size_t akvcam_format_specs_depth(akvcam_format_specs_ct self)
{
     if (self->type == AKVCAM_VIDEO_FORMAT_TYPE_UNKNOWN)
         return 0;
     else if (self->type == AKVCAM_VIDEO_FORMAT_TYPE_RGB)
        return akvcam_format_specs_component(self, AKVCAM_COMPONENT_TYPE_R)->depth;

    return akvcam_format_specs_component(self, AKVCAM_COMPONENT_TYPE_Y)->depth;
}

size_t akvcam_format_specs_number_of_components(akvcam_format_specs_ct self)
{
    size_t n = akvcam_format_specs_main_components(self);

    if (akvcam_format_specs_contains(self, AKVCAM_COMPONENT_TYPE_A))
        n++;

    return n;
}

size_t akvcam_format_specs_main_components(akvcam_format_specs_ct self)
{
    size_t n = 0;

    switch (self->type) {
    case AKVCAM_VIDEO_FORMAT_TYPE_RGB:
    case AKVCAM_VIDEO_FORMAT_TYPE_YUV:
        n = 3;

        break;

    case AKVCAM_VIDEO_FORMAT_TYPE_GRAY:
        n = 1;

        break;

    default:
        break;
    }

    return n;
}

bool akvcam_format_specs_is_fast(akvcam_format_specs_ct self)
{
    if (self->endianness != __BYTE_ORDER__)
        return false;

    size_t cur_depth = 0;

    for (size_t j = 0; j < self->nplanes; ++j)
        for (size_t i = 0; i < self->planes[j].ncomponents; ++i) {
            akvcam_color_component_ct component = self->planes[j].components + i;

            if (component->shift > 0)
                return false;

            if (component->depth < 1 || (component->depth & (component->depth - 1)))
                return false;

            if (cur_depth < 1)
                cur_depth = component->depth;
            else if (component->depth != cur_depth)
                return false;
        }

    return true;
}

size_t akvcam_plane_pixel_size(akvcam_plane_ct plane)
{
    size_t pixel_size = 0;

    for (size_t i = 0; i < plane->ncomponents; ++i) {
        akvcam_color_component_ct component = plane->components + i;
        pixel_size = akvcam_max(pixel_size, component->step);
    }

    return pixel_size;
}

size_t akvcam_plane_width_div(akvcam_plane_ct plane)
{
    size_t width_div = 0;

    for (size_t i = 0; i < plane->ncomponents; ++i) {
        akvcam_color_component_ct component = plane->components + i;

        if (i == 0)
            width_div = component->width_div;
        else
            width_div = akvcam_min(width_div, component->width_div);
    }

    return width_div;
}

size_t akvcam_plane_height_div(akvcam_plane_ct plane)
{
    size_t height_div = 0;

    for (size_t i = 0; i < plane->ncomponents; ++i) {
        akvcam_color_component_ct component = plane->components + i;
        height_div = akvcam_max(height_div, component->height_div);
    }

    return height_div;
}

uint64_t akvcam_color_component_max(akvcam_color_component_ct color_component)
{
    if (!color_component)
        return 0;

    return (1ULL << color_component->depth) - 1ULL;
}

__u32 akvcam_fourcc_from_string(const char *fourcc_str)
{
    akvcam_format_specs_ct fmt = akvcam_format_specs_table;

    for (; fmt->fourcc != 0; fmt++)
        if (strncmp(fmt->name, fourcc_str, AKVCAM_MAX_STRING_SIZE) == 0)
            return fmt->fourcc;

    return 0;
}

const char *akvcam_string_from_fourcc(__u32 fourcc)
{
    akvcam_format_specs_ct specs =
            akvcam_format_specs_from_fixel_format(fourcc);

    return specs? specs->name: "";
}

__u32 akvcam_default_input_pixel_format(void)
{
    return V4L2_PIX_FMT_RGB24;
}

__u32 akvcam_default_output_pixel_format(void)
{
    return V4L2_PIX_FMT_YUYV;
}

size_t akvcam_supported_pixel_formats(void)
{
    return ARRAY_SIZE(akvcam_format_specs_table) - 1;
}

__u32 akvcam_pixel_format_by_index(size_t index)
{
    if (index >= akvcam_supported_pixel_formats())
        return 0;

    return akvcam_format_specs_table[index].fourcc;
}
