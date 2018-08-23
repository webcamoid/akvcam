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

#include <linux/videodev2.h>

#include "utils.h"

static struct
{
    uint64_t id;
    int last_error;
} akvcam_utils_private;

typedef struct
{
    __u32 cmd;
    char  str[32];
} akvcam_utils_ioctl_strings, *akvcam_utils_ioctl_strings_t;

uint64_t akvcam_id()
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
        {VIDIOC_QUERYCAP           , "VIDIOC_QUERYCAP"           },
        {VIDIOC_RESERVED           , "VIDIOC_RESERVED"           },
        {VIDIOC_ENUM_FMT           , "VIDIOC_ENUM_FMT"           },
        {VIDIOC_G_FMT              , "VIDIOC_G_FMT"              },
        {VIDIOC_S_FMT              , "VIDIOC_S_FMT"              },
        {VIDIOC_REQBUFS            , "VIDIOC_REQBUFS"            },
        {VIDIOC_QUERYBUF           , "VIDIOC_QUERYBUF"           },
        {VIDIOC_G_FBUF             , "VIDIOC_G_FBUF"             },
        {VIDIOC_S_FBUF             , "VIDIOC_S_FBUF"             },
        {VIDIOC_OVERLAY            , "VIDIOC_OVERLAY"            },
        {VIDIOC_QBUF               , "VIDIOC_QBUF"               },
#ifdef VIDIOC_EXPBUF
        {VIDIOC_EXPBUF             , "VIDIOC_EXPBUF"             },
#endif
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
#ifdef VIDIOC_G_EDID
        {VIDIOC_G_EDID             , "VIDIOC_G_EDID"             },
#endif
#ifdef VIDIOC_S_EDID
        {VIDIOC_S_EDID             , "VIDIOC_S_EDID"             },
#endif
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
#ifdef VIDIOC_CREATE_BUFS
        {VIDIOC_CREATE_BUFS        , "VIDIOC_CREATE_BUFS"        },
#endif
#ifdef VIDIOC_PREPARE_BUF
        {VIDIOC_PREPARE_BUF        , "VIDIOC_PREPARE_BUF"        },
#endif
#ifdef VIDIOC_G_SELECTION
        {VIDIOC_G_SELECTION        , "VIDIOC_G_SELECTION"        },
#endif
#ifdef VIDIOC_S_SELECTION
        {VIDIOC_S_SELECTION        , "VIDIOC_S_SELECTION"        },
#endif
#ifdef VIDIOC_DECODER_CMD
        {VIDIOC_DECODER_CMD        , "VIDIOC_DECODER_CMD"        },
#endif
#ifdef VIDIOC_TRY_DECODER_CMD
        {VIDIOC_TRY_DECODER_CMD    , "VIDIOC_TRY_DECODER_CMD"    },
#endif
#ifdef VIDIOC_ENUM_DV_TIMINGS
        {VIDIOC_ENUM_DV_TIMINGS    , "VIDIOC_ENUM_DV_TIMINGS"    },
#endif
#ifdef VIDIOC_QUERY_DV_TIMINGS
        {VIDIOC_QUERY_DV_TIMINGS   , "VIDIOC_QUERY_DV_TIMINGS"   },
#endif
#ifdef VIDIOC_DV_TIMINGS_CAP
        {VIDIOC_DV_TIMINGS_CAP     , "VIDIOC_DV_TIMINGS_CAP"     },
#endif
#ifdef VIDIOC_ENUM_FREQ_BANDS
        {VIDIOC_ENUM_FREQ_BANDS    , "VIDIOC_ENUM_FREQ_BANDS"    },
#endif
#ifdef VIDIOC_DBG_G_CHIP_INFO
        {VIDIOC_DBG_G_CHIP_INFO    , "VIDIOC_DBG_G_CHIP_INFO"    },
#endif
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
