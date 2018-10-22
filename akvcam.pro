# akvcam, virtual camera for Linux.
# Copyright (C) 2018  Gonzalo Exequiel Pedone
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

CONFIG += console
CONFIG -= app_bundle qt

TARGET = akvcam
TEMPLATE = lib

lupdate_only {
    HEADERS += \
        src/buffer.h \
        src/buffers.h \
        src/buffers_types.h \
        src/controls.h \
        src/controls_types.h \
        src/device.h \
        src/device_types.h \
        src/driver.h \
        src/events.h \
        src/events_types.h \
        src/file_read.h \
        src/format.h \
        src/format_types.h \
        src/frame.h \
        src/frame_types.h \
        src/ioctl.h \
        src/list.h \
        src/list_types.h \
        src/map.h \
        src/mmap.h \
        src/node.h \
        src/node_types.h \
        src/object.h \
        src/rbuffer.h \
        src/settings.h \
        src/utils.h

    SOURCES += \
        src/module.c \
        src/buffer.c \
        src/buffers.c \
        src/controls.c \
        src/device.c \
        src/driver.c \
        src/events.c \
        src/file_read.c \
        src/format.c \
        src/frame.c \
        src/ioctl.c \
        src/list.c \
        src/map.c \
        src/mmap.c \
        src/node.c \
        src/object.c \
        src/rbuffer.c \
        src/settings.c \
        src/utils.c
}

KERNEL_RELEASE = $$system(uname -r)
isEmpty(KERNEL_DIR): KERNEL_DIR = /lib/modules/$${KERNEL_RELEASE}/build
!isEmpty(USE_SPARSE): USE_SPARSE_VAR = USE_SPARSE=1
isEmpty(SPARSE_MODE): SPARSE_MODE=2

INCLUDEPATH += \
    $${KERNEL_DIR}/include \
    $${KERNEL_DIR}/include/linux \
    $${KERNEL_DIR}/arch/x86/include

DEFINES += \
    __KERNEL__ \
    CONFIG_COMPAT \
    CONFIG_HZ=0 \
    CONFIG_PAGE_OFFSET=0 \
    CONFIG_PCI \
    KBUILD_MODNAME=\"\\\"\\\"\"

OTHER_FILES += \
    src/Makefile \
    share/config_example.ini

DUMMY_FILES = .
makedriver.input = DUMMY_FILES
makedriver.output = $${PWD}/src/akvcam.ko
makedriver.commands = \
    cd $${PWD}/src; \
    make \
        KERNEL_DIR=$${KERNEL_DIR} \
        $${USE_SPARSE_VAR} \
        SPARSE_MODE=$${SPARSE_MODE}; \
    cd ..
makedriver.clean = \
    $${PWD}/src/*.ko \
    $${PWD}/src/*.o \
    $${PWD}/src/*.mod.c \
    $${PWD}/src/modules.order \
    $${PWD}/src/Module.symvers
makedriver.CONFIG += no_link
QMAKE_EXTRA_COMPILERS += makedriver
PRE_TARGETDEPS += compiler_makedriver_make_all
