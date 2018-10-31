#!/bin/bash
#
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

# Set default Docker command
${EXEC} apt-get -y update
${EXEC} apt-get -y upgrade

# Install dev tools
${EXEC} apt-get -y install \
    g++ \
    make \
    libelf-dev \
    kmod \
    sparse \
    wget

if [ ! -z "${USE_QEMU}" ]; then
${EXEC} apt-get -y install \
    debootstrap \
    initramfs-tools \
    qemu-system-x86 \
    qemu-utils \
    ffmpeg \
    ubuntu-wallpapers
fi

url=http://kernel.ubuntu.com/~kernel-ppa/mainline/${REPOSITORY}
headers=linux-headers-${KERNEL_VERSION}_${KERNEL_VERSION}.${KERNEL_VERSION_C}_all.deb
headers_generic=linux-headers-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_${SYSTEM_ARCH}.deb

if [ ! -z "${USE_QEMU}" ]; then
    if [ -z "${UNSIGNED_IMG}" ]; then
        image=linux-image-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_${SYSTEM_ARCH}.deb
    else
        image=linux-image-unsigned-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_${SYSTEM_ARCH}.deb
    fi

    if [ ! -z "${NEED_MODULES}" ]; then
        modules=linux-modules-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_${SYSTEM_ARCH}.deb
    fi
fi


for package in ${image} ${headers} ${headers_generic} ${modules}; do
    ${EXEC} wget -c "${url}/${package}"
    ${EXEC} dpkg -i "${package}"
done

if [ ! -z "${USE_QEMU}" ]; then
    ${EXEC} update-initramfs -c -k ${KERNEL_VERSION}-generic
fi
