/* Virtual camera output example.
 * Copyright (C) 2020  Gonzalo Exequiel Pedone
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
 *
 * Alternatively you can redistribute this file under the terms of the
 * BSD license as stated below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. The names of its contributors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

/* For the sake of simplicity, the program won't print anything to the terminal,
 * or do any validation check, you are adviced the check every single value
 * returned by ioctl() and other functions.
 * This program shows the very minimum code required to write frames to the
 * driver in every supported mode: rw, mmap and userptr.
 */

// We'll assume this is a valid akvcam output device.
#define VIDEO_OUTPUT "/dev/video7"

/* Choose the desired capture method, possible values:
 *
 * V4L2_CAP_READWRITE for rw
 * V4L2_CAP_STREAMING for mmap and userptr
 */
//#define CAPTURE_METHOD V4L2_CAP_READWRITE
#define CAPTURE_METHOD V4L2_CAP_STREAMING

/* Choose the desired memory mapping method, possible values:
 *
 * V4L2_MEMORY_MMAP
 * V4L2_MEMORY_USERPTR
 */
#define MEMORY_TYPE V4L2_MEMORY_MMAP
//#define MEMORY_TYPE V4L2_MEMORY_USERPTR

// Choose the number of buffers to use in mmap and userptr.
#define N_BUFFERS 4

// Send frames for about 30 seconds in a 30 FPS stream.
#define FPS 30
#define DURATION_SECONDS 30
#define N_FRAMES (FPS * DURATION_SECONDS)

// This structure will store the frames data.
struct DataBuffer
{
    char *start;
    size_t length;
};

int main()
{
    // Open the output device
    int fd = open(VIDEO_OUTPUT, O_RDWR | O_NONBLOCK, 0);

    /* Check that this is an actual output device and read the default frame
     * format.
     */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(fd, VIDIOC_G_FMT, &fmt);

    /* This step is not necessary, but you can also  set a different output
     * format from the supported ones. Supported pixel formats are:
     *
     * V4L2_PIX_FMT_RGB24;
     * V4L2_PIX_FMT_BGR24;
     */
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    ioctl(fd, VIDIOC_S_FMT, &fmt);

    // Query which methods are supported by the driver.
    struct v4l2_capability capabilities;
    memset(&capabilities, 0, sizeof(struct v4l2_capability));
    ioctl(fd, VIDIOC_QUERYCAP, &capabilities);

    struct DataBuffer *buffers = NULL;

    if (CAPTURE_METHOD == V4L2_CAP_READWRITE
        && capabilities.capabilities & V4L2_CAP_READWRITE) {
        // In 'rw' mode just reserve one single buffer.
        buffers = calloc(1, sizeof(struct DataBuffer));
        buffers->length = fmt.fmt.pix.sizeimage;
        buffers->start = calloc(1, fmt.fmt.pix.sizeimage);
    } else if (CAPTURE_METHOD == V4L2_CAP_STREAMING
               && capabilities.capabilities & V4L2_CAP_STREAMING) {
        // Request N_BUFFERS.
        struct v4l2_requestbuffers requestBuffers;
        memset(&requestBuffers, 0, sizeof(struct v4l2_requestbuffers));
        requestBuffers.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        requestBuffers.memory = MEMORY_TYPE;
        requestBuffers.count = N_BUFFERS;
        ioctl(fd, VIDIOC_REQBUFS, &requestBuffers);
        buffers = calloc(requestBuffers.count,
                         sizeof(struct DataBuffer));

        // Initialize the buffers.
        for (__u32 i = 0; i < requestBuffers.count; i++) {
            if (MEMORY_TYPE == V4L2_MEMORY_MMAP) {
                struct v4l2_buffer buffer;
                memset(&buffer, 0, sizeof(struct v4l2_buffer));
                buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
                buffer.memory = V4L2_MEMORY_MMAP;
                buffer.index = i;
                ioctl(fd, VIDIOC_QUERYBUF, &buffer);
                buffers[i].length = buffer.length;
                buffers[i].start = mmap(NULL,
                                        buffer.length,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED,
                                        fd,
                                        buffer.m.offset);
            } else {
                buffers[i].length = fmt.fmt.pix.sizeimage;
                buffers[i].start = calloc(1, fmt.fmt.pix.sizeimage);
            }
        }

        // Queue the buffers to the driver.
        for (__u32 i = 0; i < requestBuffers.count; i++) {
            struct v4l2_buffer buffer;
            memset(&buffer, 0, sizeof(struct v4l2_buffer));

            if (MEMORY_TYPE == V4L2_MEMORY_MMAP) {
                buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
                buffer.memory = V4L2_MEMORY_MMAP;
                buffer.index = i;
            } else {
                buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
                buffer.memory = V4L2_MEMORY_USERPTR;
                buffer.index = i;
                buffer.m.userptr = (unsigned long) buffers[i].start;
                buffer.length = buffers[i].length;
            }

            ioctl(fd, VIDIOC_QBUF, &buffer);
        }

        // Start the stream.
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(fd, VIDIOC_STREAMON, &type);
    }

    // Generate some random noise frames.
    srand(time(0));

    for (int i = 0; i < N_FRAMES; i++) {
        if (CAPTURE_METHOD == V4L2_CAP_READWRITE
            && capabilities.capabilities & V4L2_CAP_READWRITE) {
            // Write the frame data to the buffer.
            for (size_t byte = 0; byte < buffers->length; byte++)
                buffers->start[byte] = rand() & 0xff;

            write(fd, buffers->start, buffers->length);
        } else if (CAPTURE_METHOD == V4L2_CAP_STREAMING
                   && capabilities.capabilities & V4L2_CAP_STREAMING) {
            // Dequeue one buffer.
            struct v4l2_buffer buffer;
            memset(&buffer, 0, sizeof(struct v4l2_buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
            buffer.memory = MEMORY_TYPE;
            ioctl(fd, VIDIOC_DQBUF, &buffer);

            // Write the frame data to the buffer.
            for (size_t byte = 0; byte < buffer.bytesused; byte++)
                buffers[buffer.index].start[byte] = rand() & 0xff;

            // Queue the buffer with the frame data.
            ioctl(fd, VIDIOC_QBUF, &buffer);
        }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1e9 / FPS;
        nanosleep(&ts, &ts);
    }

    // Free the buffers.
    if (CAPTURE_METHOD == V4L2_CAP_READWRITE
        && capabilities.capabilities & V4L2_CAP_READWRITE) {
        free(buffers->start);
    } else if (CAPTURE_METHOD == V4L2_CAP_STREAMING
               && capabilities.capabilities & V4L2_CAP_STREAMING) {
        // Stop the stream.
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(fd, VIDIOC_STREAMOFF, &type);

        for (__u32 i = 0; i < N_BUFFERS; i++) {
            if (MEMORY_TYPE == V4L2_MEMORY_MMAP)
                munmap(buffers[i].start, buffers[i].length);
            else
                free(buffers[i].start);
        }
    }

    free(buffers);

    // Close the output device.
    close(fd);

    return 0;
}
