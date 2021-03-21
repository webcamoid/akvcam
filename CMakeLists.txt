# akvcam, virtual camera for Linux.
# Copyright (C) 2021  Gonzalo Exequiel Pedone
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

cmake_minimum_required(VERSION 3.14)

project(akvcam LANGUAGES C)

execute_process(COMMAND uname -r
                OUTPUT_VARIABLE KERNEL_RELEASE
                OUTPUT_STRIP_TRAILING_WHITESPACE)
set(KERNEL_DIR /lib/modules/${KERNEL_RELEASE}/build CACHE PATH "Kernel sources")
set(USE_SPARSE OFF CACHE BOOL "Use sparse for bug catching")
set(SPARSE_MODE 2 CACHE STRING "sparse mode")

if (USE_SPARSE)
    set(MAKE_CMD make KERNEL_DIR=${KERNEL_DIR} USE_SPARSE=1 SPARSE_MODE=${SPARSE_MODE})
else ()
    set(MAKE_CMD make KERNEL_DIR=${KERNEL_DIR})
endif ()

file(GLOB_RECURSE MODULE_SOURCES
     RELATIVE ${CMAKE_SOURCE_DIR}
     CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/*.h
     ${CMAKE_SOURCE_DIR}/src/*.c)

foreach(SOURCE_FILE ${MODULE_SOURCES})
    configure_file(${SOURCE_FILE} ${SOURCE_FILE} COPYONLY)
endforeach()

configure_file(src/dkms.conf src/dkms.conf COPYONLY)
configure_file(src/Makefile src/Makefile COPYONLY)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/akvcam.ko
                   COMMAND ${MAKE_CMD}
                   WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/src
                   VERBATIM)
add_custom_target(akvcam ALL
                  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/src/akvcam.ko
                  VERBATIM
                  SOURCES src/dkms.conf
                          src/Makefile
                          share/config_example.ini)

# This is a hack for making possible for the IDE to detect the kernel include
# directories. Don't try to build it, because it won't.
add_library(akvcamlib STATIC EXCLUDE_FROM_ALL ${MODULE_SOURCES})
include_directories(src
                    ${KERNEL_DIR}/include
                    ${KERNEL_DIR}/include/linux
                    ${KERNEL_DIR}/include/uapi
                    ${KERNEL_DIR}/arch/x86/include
                    ${KERNEL_DIR}/arch/x86/include/generated)
add_definitions(-D__KERNEL__
                -DCONFIG_COMPAT
                -DCONFIG_HZ=0
                -DCONFIG_PAGE_OFFSET=0
                -DCONFIG_PCI
                -DKBUILD_MODNAME="")
