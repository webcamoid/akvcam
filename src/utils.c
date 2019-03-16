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

#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>

#include "utils.h"

static struct akvcam_utils
{
    uint64_t id;
    int last_error;
} akvcam_utils_private;

typedef struct
{
    __u32 cmd;
    char  str[32];
} akvcam_utils_ioctl_strings, *akvcam_utils_ioctl_strings_t;

uint64_t akvcam_id(void)
{
    return akvcam_utils_private.id++;
}

int akvcam_get_last_error(void)
{
    return akvcam_utils_private.last_error;
}

int akvcam_set_last_error(int error)
{
    akvcam_utils_private.last_error = error;

    return error;
}

const char *akvcam_string_from_ioctl(uint cmd)
{
    size_t i;
    static char unknown[1024];
    static akvcam_utils_ioctl_strings ioctl_strings[] = {
        {UVCIOC_CTRL_MAP           , "UVCIOC_CTRL_MAP"           },
        {UVCIOC_CTRL_QUERY         , "UVCIOC_CTRL_QUERY"         },
        {VIDIOC_QUERYCAP           , "VIDIOC_QUERYCAP"           },
#ifdef VIDIOC_RESERVED
        {VIDIOC_RESERVED           , "VIDIOC_RESERVED"           },
#endif
        {VIDIOC_ENUM_FMT           , "VIDIOC_ENUM_FMT"           },
        {VIDIOC_G_FMT              , "VIDIOC_G_FMT"              },
        {VIDIOC_S_FMT              , "VIDIOC_S_FMT"              },
        {VIDIOC_REQBUFS            , "VIDIOC_REQBUFS"            },
        {VIDIOC_QUERYBUF           , "VIDIOC_QUERYBUF"           },
        {VIDIOC_G_FBUF             , "VIDIOC_G_FBUF"             },
        {VIDIOC_S_FBUF             , "VIDIOC_S_FBUF"             },
        {VIDIOC_OVERLAY            , "VIDIOC_OVERLAY"            },
        {VIDIOC_QBUF               , "VIDIOC_QBUF"               },
        {VIDIOC_EXPBUF             , "VIDIOC_EXPBUF"             },
        {VIDIOC_DQBUF              , "VIDIOC_DQBUF"              },
        {VIDIOC_STREAMON           , "VIDIOC_STREAMON"           },
        {VIDIOC_STREAMOFF          , "VIDIOC_STREAMOFF"          },
        {VIDIOC_G_PARM             , "VIDIOC_G_PARM"             },
        {VIDIOC_S_PARM             , "VIDIOC_S_PARM"             },
        {VIDIOC_G_STD              , "VIDIOC_G_STD"              },
        {VIDIOC_S_STD              , "VIDIOC_S_STD"              },
        {VIDIOC_ENUMSTD            , "VIDIOC_ENUMSTD"            },
        {VIDIOC_ENUMINPUT          , "VIDIOC_ENUMINPUT"          },
        {VIDIOC_G_CTRL             , "VIDIOC_G_CTRL"             },
        {VIDIOC_S_CTRL             , "VIDIOC_S_CTRL"             },
        {VIDIOC_G_TUNER            , "VIDIOC_G_TUNER"            },
        {VIDIOC_S_TUNER            , "VIDIOC_S_TUNER"            },
        {VIDIOC_G_AUDIO            , "VIDIOC_G_AUDIO"            },
        {VIDIOC_S_AUDIO            , "VIDIOC_S_AUDIO"            },
        {VIDIOC_QUERYCTRL          , "VIDIOC_QUERYCTRL"          },
        {VIDIOC_QUERYMENU          , "VIDIOC_QUERYMENU"          },
        {VIDIOC_G_INPUT            , "VIDIOC_G_INPUT"            },
        {VIDIOC_S_INPUT            , "VIDIOC_S_INPUT"            },
        {VIDIOC_G_EDID             , "VIDIOC_G_EDID"             },
        {VIDIOC_S_EDID             , "VIDIOC_S_EDID"             },
        {VIDIOC_G_OUTPUT           , "VIDIOC_G_OUTPUT"           },
        {VIDIOC_S_OUTPUT           , "VIDIOC_S_OUTPUT"           },
        {VIDIOC_ENUMOUTPUT         , "VIDIOC_ENUMOUTPUT"         },
        {VIDIOC_G_AUDOUT           , "VIDIOC_G_AUDOUT"           },
        {VIDIOC_S_AUDOUT           , "VIDIOC_S_AUDOUT"           },
        {VIDIOC_G_MODULATOR        , "VIDIOC_G_MODULATOR"        },
        {VIDIOC_S_MODULATOR        , "VIDIOC_S_MODULATOR"        },
        {VIDIOC_G_FREQUENCY        , "VIDIOC_G_FREQUENCY"        },
        {VIDIOC_S_FREQUENCY        , "VIDIOC_S_FREQUENCY"        },
        {VIDIOC_CROPCAP            , "VIDIOC_CROPCAP"            },
        {VIDIOC_G_CROP             , "VIDIOC_G_CROP"             },
        {VIDIOC_S_CROP             , "VIDIOC_S_CROP"             },
        {VIDIOC_G_JPEGCOMP         , "VIDIOC_G_JPEGCOMP"         },
        {VIDIOC_S_JPEGCOMP         , "VIDIOC_S_JPEGCOMP"         },
        {VIDIOC_QUERYSTD           , "VIDIOC_QUERYSTD"           },
        {VIDIOC_TRY_FMT            , "VIDIOC_TRY_FMT"            },
        {VIDIOC_ENUMAUDIO          , "VIDIOC_ENUMAUDIO"          },
        {VIDIOC_ENUMAUDOUT         , "VIDIOC_ENUMAUDOUT"         },
        {VIDIOC_G_PRIORITY         , "VIDIOC_G_PRIORITY"         },
        {VIDIOC_S_PRIORITY         , "VIDIOC_S_PRIORITY"         },
        {VIDIOC_G_SLICED_VBI_CAP   , "VIDIOC_G_SLICED_VBI_CAP"   },
        {VIDIOC_LOG_STATUS         , "VIDIOC_LOG_STATUS"         },
        {VIDIOC_G_EXT_CTRLS        , "VIDIOC_G_EXT_CTRLS"        },
        {VIDIOC_S_EXT_CTRLS        , "VIDIOC_S_EXT_CTRLS"        },
        {VIDIOC_TRY_EXT_CTRLS      , "VIDIOC_TRY_EXT_CTRLS"      },
        {VIDIOC_ENUM_FRAMESIZES    , "VIDIOC_ENUM_FRAMESIZES"    },
        {VIDIOC_ENUM_FRAMEINTERVALS, "VIDIOC_ENUM_FRAMEINTERVALS"},
        {VIDIOC_G_ENC_INDEX        , "VIDIOC_G_ENC_INDEX"        },
        {VIDIOC_ENCODER_CMD        , "VIDIOC_ENCODER_CMD"        },
        {VIDIOC_TRY_ENCODER_CMD    , "VIDIOC_TRY_ENCODER_CMD"    },
        {VIDIOC_DBG_S_REGISTER     , "VIDIOC_DBG_S_REGISTER"     },
        {VIDIOC_DBG_G_REGISTER     , "VIDIOC_DBG_G_REGISTER"     },
        {VIDIOC_S_HW_FREQ_SEEK     , "VIDIOC_S_HW_FREQ_SEEK"     },
        {VIDIOC_S_DV_TIMINGS       , "VIDIOC_S_DV_TIMINGS"       },
        {VIDIOC_G_DV_TIMINGS       , "VIDIOC_G_DV_TIMINGS"       },
        {VIDIOC_DQEVENT            , "VIDIOC_DQEVENT"            },
        {VIDIOC_SUBSCRIBE_EVENT    , "VIDIOC_SUBSCRIBE_EVENT"    },
        {VIDIOC_UNSUBSCRIBE_EVENT  , "VIDIOC_UNSUBSCRIBE_EVENT"  },
        {VIDIOC_CREATE_BUFS        , "VIDIOC_CREATE_BUFS"        },
        {VIDIOC_PREPARE_BUF        , "VIDIOC_PREPARE_BUF"        },
        {VIDIOC_G_SELECTION        , "VIDIOC_G_SELECTION"        },
        {VIDIOC_S_SELECTION        , "VIDIOC_S_SELECTION"        },
        {VIDIOC_DECODER_CMD        , "VIDIOC_DECODER_CMD"        },
        {VIDIOC_TRY_DECODER_CMD    , "VIDIOC_TRY_DECODER_CMD"    },
        {VIDIOC_ENUM_DV_TIMINGS    , "VIDIOC_ENUM_DV_TIMINGS"    },
        {VIDIOC_QUERY_DV_TIMINGS   , "VIDIOC_QUERY_DV_TIMINGS"   },
        {VIDIOC_DV_TIMINGS_CAP     , "VIDIOC_DV_TIMINGS_CAP"     },
        {VIDIOC_ENUM_FREQ_BANDS    , "VIDIOC_ENUM_FREQ_BANDS"    },
        {VIDIOC_DBG_G_CHIP_INFO    , "VIDIOC_DBG_G_CHIP_INFO"    },
#ifdef VIDIOC_QUERY_EXT_CTRL
        {VIDIOC_QUERY_EXT_CTRL     , "VIDIOC_QUERY_EXT_CTRL"     },
#endif
        {0                         , ""                          },
    };

    for (i = 0; ioctl_strings[i].cmd; i++)
        if (ioctl_strings[i].cmd == cmd)
            return ioctl_strings[i].str;

    snprintf(unknown, 1024, "VIDIOC_UNKNOWN(%u)", cmd);

    return unknown;
}

size_t akvcam_line_size(const char *buffer, size_t size, bool *found)
{
    size_t i;
    *found = false;

    for (i = 0; i < size; i++)
        if (buffer[i] == '\n') {
            *found = true;

            break;
        }

    return i;
}

char *akvcam_strdup(const char *str, AKVCAM_MEMORY_TYPE type)
{
    char *str_dup;
    size_t len = str? strlen(str): 0;

    if (type == AKVCAM_MEMORY_TYPE_KMALLOC)
        str_dup = kmalloc(len + 1, GFP_KERNEL);
    else
        str_dup = vmalloc(len + 1);

    str_dup[len] = 0;

    if (str)
        memcpy(str_dup, str, len + 1);

    return str_dup;
}

char *akvcam_strip_str(const char *str, AKVCAM_MEMORY_TYPE type)
{
    return akvcam_strip_str_sub(str, 0, strlen(str), type);
}

char *akvcam_strip_str_sub(const char *str,
                           size_t from,
                           size_t size,
                           AKVCAM_MEMORY_TYPE type)
{
    char *stripped_str;
    ssize_t i;
    size_t len = akvcam_min(str? strlen(str): 0, from + size);
    size_t left;
    size_t right;
    size_t stripped_len = len;

    for (i = (ssize_t) from; i < (ssize_t) len; i++)
        if (!isspace(str[i]))
            break;

    left = (size_t) i;

    if (left == len) {
        stripped_len = 0;
    } else {
        for (i = (ssize_t) len - 1; i >= (ssize_t) from; i--)
            if (!isspace(str[i]))
                break;

        right = (size_t) i;
        stripped_len = right - left + 1;
    }

    if (type == AKVCAM_MEMORY_TYPE_KMALLOC)
        stripped_str = kmalloc(stripped_len + 1, GFP_KERNEL);
    else
        stripped_str = vmalloc(stripped_len + 1);

    stripped_str[stripped_len] = 0;

    if (stripped_len > 0)
        memcpy(stripped_str, str + left, stripped_len);

    return stripped_str;
}

char *akvcam_strip_move_str(char *str, AKVCAM_MEMORY_TYPE type)
{
    char *stripped_str = akvcam_strip_str(str, type);

    if (type == AKVCAM_MEMORY_TYPE_KMALLOC)
        kfree(str);
    else
        vfree(str);

    return stripped_str;
}

size_t akvcam_str_count(const char *str, char c)
{
    size_t count = 0;
    size_t i;

    for (i = 0; i < strlen(str); i++)
        if (str[i] == c)
            count++;

    return count;
}

void akvcam_replace(char *str, char from, char to)
{
    if (!str)
        return;

    for (; *str; str++)
        if (*str == from)
            *str = to;
}

void akvcam_get_timestamp(struct timeval *tv)
{
    struct timespec ts;
    ktime_get_ts(&ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}
