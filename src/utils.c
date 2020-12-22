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

typedef struct
{
    int error;
    char  str[32];
    char  description[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_error_strings, *akvcam_utils_error_strings_t;

typedef struct
{
    enum v4l2_buf_type type;
    char  str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_buf_type_strings, *akvcam_utils_buf_type_strings_t;

typedef struct
{
    enum v4l2_field field;
    char  str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_field_strings, *akvcam_utils_field_strings_t;

typedef struct
{
    enum v4l2_memory memory;
    char  str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_v4l2_memory_strings, *akvcam_utils_v4l2_memory_strings_t;

typedef struct
{
    AKVCAM_RW_MODE rw_mode;
    char  str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_rw_mode_strings, *akvcam_utils_rw_mode_strings_t;

typedef struct
{
    __u32 flag;
    char  str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_buffer_flags_strings, *akvcam_utils_buffer_flags_strings_t;

typedef struct
{
    __u32 flag;
    char  str[AKVCAM_MAX_STRING_SIZE];
} akvcam_utils_buffer_capabilities_strings, *akvcam_utils_buffer_capabilities_strings_t;

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
    static char ioctlstr[AKVCAM_MAX_STRING_SIZE];
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

    snprintf(ioctlstr, AKVCAM_MAX_STRING_SIZE, "VIDIOC_UNKNOWN(%u)", cmd);

    return ioctlstr;
}

const char *akvcam_string_from_error(int error)
{
    size_t i;
    static char errorstr[AKVCAM_MAX_STRING_SIZE];
    static akvcam_utils_error_strings error_strings[] = {
        {EPERM	, "EPERM"  , "Operation not permitted"            },
        {ENOENT	, "ENOENT" , "No such file or directory"          },
        {ESRCH	, "ESRCH"  , "No such process"                    },
        {EINTR	, "EINTR"  , "Interrupted system call"            },
        {EIO	, "EIO"    , "I/O error"                          },
        {ENXIO	, "ENXIO"  , "No such device or address"          },
        {E2BIG	, "E2BIG"  , "Argument list too long"             },
        {ENOEXEC, "ENOEXEC", "Exec format error"                  },
        {EBADF	, "EBADF"  , "Bad file number"                    },
        {ECHILD	, "ECHILD" , "No child processes"                 },
        {EAGAIN	, "EAGAIN" , "Try again"                          },
        {ENOMEM	, "ENOMEM" , "Out of memory"                      },
        {EACCES	, "EACCES" , "Permission denied"                  },
        {EFAULT	, "EFAULT" , "Bad address"                        },
        {ENOTBLK, "ENOTBLK", "Block device required"              },
        {EBUSY	, "EBUSY"  , "Device or resource busy"            },
        {EEXIST	, "EEXIST" , "File exists"                        },
        {EXDEV	, "EXDEV"  , "Cross-device link"                  },
        {ENODEV	, "ENODEV" , "No such device"                     },
        {ENOTDIR, "ENOTDIR", "Not a directory"                    },
        {EISDIR	, "EISDIR" , "Is a directory"                     },
        {EINVAL	, "EINVAL" , "Invalid argument"                   },
        {ENFILE	, "ENFILE" , "File table overflow"                },
        {EMFILE	, "EMFILE" , "Too many open files"                },
        {ENOTTY	, "ENOTTY" , "Not a typewriter"                   },
        {ETXTBSY, "ETXTBSY", "Text file busy"                     },
        {EFBIG	, "EFBIG"  , "File too large"                     },
        {ENOSPC	, "ENOSPC" , "No space left on device"            },
        {ESPIPE	, "ESPIPE" , "Illegal seek"                       },
        {EROFS	, "EROFS"  , "Read-only file system "             },
        {EMLINK	, "EMLINK" , "Too many links"                     },
        {EPIPE	, "EPIPE"  , "Broken pipe"                        },
        {EDOM	, "EDOM"   , "Math argument out of domain of func"},
        {ERANGE	, "ERANGE" , "Math result not representable"      },
        {0      , ""       , ""                                   },
    };

    memset(errorstr, 0, AKVCAM_MAX_STRING_SIZE);

    if (error >= 0)
        return errorstr;

    for (i = 0; error_strings[i].error; i++)
        if (error_strings[i].error == -error) {
            snprintf(errorstr,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s (%s)",
                     error_strings[i].description,
                     error_strings[i].str);

            return errorstr;
        }

    snprintf(errorstr, AKVCAM_MAX_STRING_SIZE, "Unknown error (%d)", error);

    return errorstr;
}

const char *akvcam_string_from_ioctl_error(uint cmd, int error)
{
    const char *cmdstr = akvcam_string_from_ioctl(cmd);
    const char *errorstr = akvcam_string_from_error(error);
    static char ioctl_error_str[AKVCAM_MAX_STRING_SIZE];

    snprintf(ioctl_error_str,
             AKVCAM_MAX_STRING_SIZE,
             "%s: %s",
             cmdstr,
             errorstr);

    return ioctl_error_str;
}

const char *akvcam_string_from_v4l2_buf_type(enum v4l2_buf_type type)
{
    size_t i;
    static char buf_type_str[AKVCAM_MAX_STRING_SIZE];
    static akvcam_utils_buf_type_strings buf_type_strings[] = {
        {V4L2_BUF_TYPE_VIDEO_CAPTURE       , "V4L2_BUF_TYPE_VIDEO_CAPTURE"       },
        {V4L2_BUF_TYPE_VIDEO_OUTPUT        , "V4L2_BUF_TYPE_VIDEO_OUTPUT"        },
        {V4L2_BUF_TYPE_VIDEO_OVERLAY       , "V4L2_BUF_TYPE_VIDEO_OVERLAY"       },
        {V4L2_BUF_TYPE_VBI_CAPTURE         , "V4L2_BUF_TYPE_VBI_CAPTURE"         },
        {V4L2_BUF_TYPE_VBI_OUTPUT          , "V4L2_BUF_TYPE_VBI_OUTPUT"          },
        {V4L2_BUF_TYPE_SLICED_VBI_CAPTURE  , "V4L2_BUF_TYPE_SLICED_VBI_CAPTURE"  },
        {V4L2_BUF_TYPE_SLICED_VBI_OUTPUT   , "V4L2_BUF_TYPE_SLICED_VBI_OUTPUT"   },
        {V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY, "V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY"},
        {V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, "V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE"},
        {V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE , "V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE" },
        {V4L2_BUF_TYPE_SDR_CAPTURE         , "V4L2_BUF_TYPE_SDR_CAPTURE "        },
        {V4L2_BUF_TYPE_SDR_OUTPUT          , "V4L2_BUF_TYPE_SDR_OUTPUT  "        },
        {V4L2_BUF_TYPE_META_CAPTURE        , "V4L2_BUF_TYPE_META_CAPTURE"        },
        {V4L2_BUF_TYPE_META_OUTPUT         , "V4L2_BUF_TYPE_META_OUTPUT "        },
        {0                                 , ""                                  },
    };

    memset(buf_type_str, 0, AKVCAM_MAX_STRING_SIZE);

    for (i = 0; buf_type_strings[i].type; i++)
        if (buf_type_strings[i].type == type) {
            snprintf(buf_type_str,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s",
                     buf_type_strings[i].str);

            return buf_type_str;
        }

    snprintf(buf_type_str, AKVCAM_MAX_STRING_SIZE, "v4l2_buf_type(%d)", type);

    return buf_type_str;
}

const char *akvcam_string_from_rw_mode(AKVCAM_RW_MODE rw_mode)
{
    size_t i = 0;
    size_t j = 0;
    size_t n;
    static char rw_mode_str[AKVCAM_MAX_STRING_SIZE];
    static akvcam_utils_rw_mode_strings rw_mode_strings[] = {
        {AKVCAM_RW_MODE_READWRITE, "rw"     },
        {AKVCAM_RW_MODE_MMAP     , "mmap"   },
        {AKVCAM_RW_MODE_USERPTR  , "userptr"},
        {0                       , ""       },
    };

    n = snprintf(rw_mode_str, AKVCAM_MAX_STRING_SIZE, "AKVCAM_RW_MODE(");

    for (i = 0; rw_mode_strings[i].rw_mode; i++)
        if (rw_mode_strings[i].rw_mode & rw_mode) {
            if (j > 0)
                n += snprintf(rw_mode_str + n, AKVCAM_MAX_STRING_SIZE - n, ", ");

            n += snprintf(rw_mode_str + n, AKVCAM_MAX_STRING_SIZE - n, "%s", rw_mode_strings[i].str);
            j++;
        }

    snprintf(rw_mode_str + n, AKVCAM_MAX_STRING_SIZE - n, ")");

    return rw_mode_str;
}

const char *akvcam_string_from_v4l2_memory(enum v4l2_memory memory)
{
    size_t i;
    static char memory_str[AKVCAM_MAX_STRING_SIZE];
    static akvcam_utils_v4l2_memory_strings memory_strings[] = {
        {V4L2_MEMORY_MMAP   , "V4L2_MEMORY_MMAP"   },
        {V4L2_MEMORY_USERPTR, "V4L2_MEMORY_USERPT" },
        {V4L2_MEMORY_OVERLAY, "V4L2_MEMORY_OVERLAY"},
        {V4L2_MEMORY_DMABUF , "V4L2_MEMORY_DMABUF" },
        {0                  , ""                   },
    };

    memset(memory_str, 0, AKVCAM_MAX_STRING_SIZE);

    for (i = 0; memory_strings[i].memory; i++)
        if (memory_strings[i].memory == memory) {
            snprintf(memory_str,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s",
                     memory_strings[i].str);

            return memory_str;
        }

    snprintf(memory_str, AKVCAM_MAX_STRING_SIZE, "v4l2_memory(%d)", memory);

    return memory_str;
}

const char *akvcam_string_from_v4l2_format(const struct v4l2_format *format)
{
    static char format_str[AKVCAM_MAX_STRING_SIZE];
    size_t n;
    const char *type;

    memset(format_str, 0, AKVCAM_MAX_STRING_SIZE);
    n = snprintf(format_str, AKVCAM_MAX_STRING_SIZE, "struct v4l2_format {\n");
    type = akvcam_string_from_v4l2_buf_type(format->type);
    n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\ttype: %s\n", type);

    if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE
        || format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\twidth: %u\n", format->fmt.pix.width);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\theight: %u\n", format->fmt.pix.height);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tpixelformat: %u\n", format->fmt.pix.pixelformat);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tfield: %u\n", format->fmt.pix.field);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tbytesperline: %u\n", format->fmt.pix.bytesperline);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tsizeimage: %u\n", format->fmt.pix.sizeimage);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tcolorspace: %u\n", format->fmt.pix.colorspace);
    } else if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
               || format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\twidth: %u\n", format->fmt.pix_mp.width);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\theight: %u\n", format->fmt.pix_mp.height);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tpixelformat: %u\n", format->fmt.pix_mp.pixelformat);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tfield: %u\n", format->fmt.pix_mp.field);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tcolorspace: %u\n", format->fmt.pix_mp.colorspace);
        n += snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tnum_planes: %u\n", format->fmt.pix_mp.num_planes);
    }

    snprintf(format_str + n, AKVCAM_MAX_STRING_SIZE - n, "}");

    return format_str;
}

const char *akvcam_string_from_v4l2_buffer(const struct v4l2_buffer *buffer)
{
    static char buffer_str[AKVCAM_MAX_STRING_SIZE];
    size_t n;
    const char *type;
    const char *memory;
    const char *flags;
    const char *field;

    n = snprintf(buffer_str, AKVCAM_MAX_STRING_SIZE, "struct v4l2_buffer {\n");
    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tindex: %u\n", buffer->index);
    type = akvcam_string_from_v4l2_buf_type(buffer->type);
    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\ttype: %s\n", type);
    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tbytesused: %u\n", buffer->bytesused);
    flags = akvcam_string_from_v4l2_buffer_flags(buffer->flags);
    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tflags: %s\n", flags);
    field = akvcam_string_from_v4l2_field(buffer->field);
    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tfield: %s\n", field);
    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tsequence: %u\n", buffer->sequence);
    memory = akvcam_string_from_v4l2_memory(buffer->memory);
    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tmemory: %s\n", memory);

    switch (buffer->memory) {
    case V4L2_MEMORY_MMAP:
        n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tm.offset: %u\n", buffer->m.offset);
        break;

    case V4L2_MEMORY_USERPTR:
        n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tm.userptr: %p\n", (void *) buffer->m.userptr);
        //n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tm.planes: %p\n", buffer->m.planes);
        break;

    case V4L2_MEMORY_OVERLAY:
        break;

    case V4L2_MEMORY_DMABUF:
        n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tm.fd: %d\n", buffer->m.fd);
        break;

    default:
        break;
    }

    n += snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tlength: %u\n", buffer->length);
    snprintf(buffer_str + n, AKVCAM_MAX_STRING_SIZE - n, "}");

    return buffer_str;
}

const char *akvcam_string_from_v4l2_buffer_flags(__u32 flags)
{
    size_t i = 0;
    size_t j = 0;
    size_t n;
    static char flags_str[AKVCAM_MAX_STRING_SIZE];
    static akvcam_utils_buffer_flags_strings flags_strings[] = {
        {V4L2_BUF_FLAG_MAPPED              , "mapped"              },
        {V4L2_BUF_FLAG_QUEUED              , "queued"              },
        {V4L2_BUF_FLAG_DONE                , "done"                },
        {V4L2_BUF_FLAG_KEYFRAME            , "keyframe"            },
        {V4L2_BUF_FLAG_PFRAME              , "pframe"              },
        {V4L2_BUF_FLAG_BFRAME              , "bframe"              },
        {V4L2_BUF_FLAG_ERROR               , "error"               },
        {V4L2_BUF_FLAG_IN_REQUEST          , "in_request"          },
        {V4L2_BUF_FLAG_TIMECODE            , "timecode"            },
        {V4L2_BUF_FLAG_M2M_HOLD_CAPTURE_BUF, "m2m_hold_capture_buf"},
        {V4L2_BUF_FLAG_PREPARED            , "prepared"            },
        {V4L2_BUF_FLAG_NO_CACHE_INVALIDATE , "no_cache_invalidate" },
        {V4L2_BUF_FLAG_NO_CACHE_CLEAN      , "no_cache_clean"      },
        {V4L2_BUF_FLAG_TIMESTAMP_MASK      , "timestamp_mask"      },
        {V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN   , "timestamp_unknown"   },
        {V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC , "timestamp_monotonic" },
        {V4L2_BUF_FLAG_TIMESTAMP_COPY      , "timestamp_copy"      },
        {V4L2_BUF_FLAG_TSTAMP_SRC_MASK     , "tstamp_src_mask"     },
        {V4L2_BUF_FLAG_TSTAMP_SRC_EOF      , "tstamp_src_eof"      },
        {V4L2_BUF_FLAG_TSTAMP_SRC_SOE      , "tstamp_src_soe"      },
        {V4L2_BUF_FLAG_LAST                , "last"                },
        {V4L2_BUF_FLAG_REQUEST_FD          , "request_fd"          },
        {0                                 , ""                    },
    };

    n = snprintf(flags_str, AKVCAM_MAX_STRING_SIZE, "V4L2_BUF_FLAG(");

    for (i = 0; flags_strings[i].flag; i++)
        if (flags_strings[i].flag & flags) {
            if (j > 0)
                n += snprintf(flags_str + n, AKVCAM_MAX_STRING_SIZE - n, ", ");

            n += snprintf(flags_str + n, AKVCAM_MAX_STRING_SIZE - n, "%s", flags_strings[i].str);
            j++;
        }

    snprintf(flags_str + n, AKVCAM_MAX_STRING_SIZE - n, ")");

    return flags_str;
}

const char *akvcam_string_from_v4l2_field(enum v4l2_field field)
{
    size_t i;
    static char field_str[AKVCAM_MAX_STRING_SIZE];
    static akvcam_utils_field_strings field_strings[] = {
        {V4L2_FIELD_ANY          , "V4L2_FIELD_ANY"          },
        {V4L2_FIELD_NONE         , "V4L2_FIELD_NONE"         },
        {V4L2_FIELD_TOP          , "V4L2_FIELD_TOP"          },
        {V4L2_FIELD_BOTTOM       , "V4L2_FIELD_BOTTOM"       },
        {V4L2_FIELD_INTERLACED   , "V4L2_FIELD_INTERLACED"   },
        {V4L2_FIELD_SEQ_TB       , "V4L2_FIELD_SEQ_TB"       },
        {V4L2_FIELD_SEQ_BT       , "V4L2_FIELD_SEQ_BT"       },
        {V4L2_FIELD_ALTERNATE    , "V4L2_FIELD_ALTERNATE"    },
        {V4L2_FIELD_INTERLACED_TB, "V4L2_FIELD_INTERLACED_TB"},
        {V4L2_FIELD_INTERLACED_BT, "V4L2_FIELD_INTERLACED_BT"},
        {-1                      , ""                        },
    };

    memset(field_str, 0, AKVCAM_MAX_STRING_SIZE);

    for (i = 0; field_strings[i].field >= 0; i++)
        if (field_strings[i].field == field) {
            snprintf(field_str,
                     AKVCAM_MAX_STRING_SIZE,
                     "%s",
                     field_strings[i].str);

            return field_str;
        }

    snprintf(field_str, AKVCAM_MAX_STRING_SIZE, "v4l2_field(%d)", field);

    return field_str;
}

const char *akvcam_string_from_v4l2_requestbuffers(const struct v4l2_requestbuffers *reqbuffs)
{
    static char reqbuffs_str[AKVCAM_MAX_STRING_SIZE];
    size_t n;
    const char *type;
    const char *memory;
    const char *capabilities;

    n = snprintf(reqbuffs_str, AKVCAM_MAX_STRING_SIZE, "struct v4l2_requestbuffers {\n");
    n += snprintf(reqbuffs_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tcount: %u\n", reqbuffs->count);
    type = akvcam_string_from_v4l2_buf_type(reqbuffs->type);
    n += snprintf(reqbuffs_str + n, AKVCAM_MAX_STRING_SIZE - n, "\ttype: %s\n", type);
    memory = akvcam_string_from_v4l2_memory(reqbuffs->memory);
    n += snprintf(reqbuffs_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tmemory: %s\n", memory);
    capabilities = akvcam_string_from_v4l2_buffer_capabilities(reqbuffs->capabilities);
    n += snprintf(reqbuffs_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tcapabilities: %s\n", capabilities);
    snprintf(reqbuffs_str + n, AKVCAM_MAX_STRING_SIZE - n, "}");

    return reqbuffs_str;
}

const char *akvcam_string_from_v4l2_buffer_capabilities(__u32 flags)
{
    size_t i = 0;
    size_t j = 0;
    size_t n;
    static char capabilities_str[AKVCAM_MAX_STRING_SIZE];
    static akvcam_utils_buffer_capabilities_strings capabilities_strings[] = {
        {V4L2_BUF_CAP_SUPPORTS_MMAP		           , "mmap"                },
        {V4L2_BUF_CAP_SUPPORTS_USERPTR		       , "userptr"             },
        {V4L2_BUF_CAP_SUPPORTS_DMABUF		       , "dmabuf"              },
        {V4L2_BUF_CAP_SUPPORTS_REQUESTS		       , "requests"            },
        {V4L2_BUF_CAP_SUPPORTS_ORPHANED_BUFS	   , "orphaned_bufs"       },
        {V4L2_BUF_CAP_SUPPORTS_M2M_HOLD_CAPTURE_BUF, "m2m_hold_capture_buf"},
        {0                                         , ""                    },
    };

    n = snprintf(capabilities_str, AKVCAM_MAX_STRING_SIZE, "V4L2_BUF_FLAG(");

    for (i = 0; capabilities_strings[i].flag; i++)
        if (capabilities_strings[i].flag & flags) {
            if (j > 0)
                n += snprintf(capabilities_str + n, AKVCAM_MAX_STRING_SIZE - n, ", ");

            n += snprintf(capabilities_str + n, AKVCAM_MAX_STRING_SIZE - n, "%s", capabilities_strings[i].str);
            j++;
        }

    snprintf(capabilities_str + n, AKVCAM_MAX_STRING_SIZE - n, ")");

    return capabilities_str;
}

const char *akvcam_string_from_v4l2_create_buffers(const struct v4l2_create_buffers *buffers)
{
    static char buffers_str[AKVCAM_MAX_STRING_SIZE];
    size_t n;
    const char *memory;
    const char *format;
    const char *capabilities;

    n = snprintf(buffers_str, AKVCAM_MAX_STRING_SIZE, "struct v4l2_create_buffers {\n");
    n += snprintf(buffers_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tindex: %u\n", buffers->index);
    n += snprintf(buffers_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tcount: %u\n", buffers->count);
    memory = akvcam_string_from_v4l2_memory(buffers->memory);
    n += snprintf(buffers_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tmemory: %s\n", memory);
    format = akvcam_string_from_v4l2_format(&buffers->format);
    n += snprintf(buffers_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tformat: %s\n", format);
    capabilities = akvcam_string_from_v4l2_buffer_capabilities(buffers->capabilities);
    n += snprintf(buffers_str + n, AKVCAM_MAX_STRING_SIZE - n, "\tcapabilities: %s\n", capabilities);
    snprintf(buffers_str + n, AKVCAM_MAX_STRING_SIZE - n, "}");

    return buffers_str;
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
void akvcam_get_timespec(struct timespec *tv)
{
    ktime_get_ts(tv);
}
#else
void akvcam_get_timespec(struct __kernel_timespec *tv)
{
    struct timespec64 ts;
    ktime_get_ts64(&ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_nsec = ts.tv_nsec;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
void akvcam_get_timestamp(struct timeval *tv)
{
    struct timespec ts;
    ktime_get_ts(&ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}
#else
void akvcam_get_timestamp(struct __kernel_v4l2_timeval *tv)
{
    struct timespec64 ts;
    ktime_get_ts64(&ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}
#endif
