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

if [ ! -z "${USE_WGET}" ]; then
    export DOWNLOAD_CMD="wget -nv -c"
else
    export DOWNLOAD_CMD="curl --retry 10 -sS -kLOC -"
fi

# Fix keyboard layout bug when running apt

cat << EOF > keyboard_config
XKBMODEL="pc105"
XKBLAYOUT="us"
XKBVARIANT=""
XKBOPTIONS=""
BACKSPACE="guess"
EOF

export LC_ALL=C
export DEBIAN_FRONTEND=noninteractive

apt-get -qq -y update
apt-get install -qq -y keyboard-configuration
cp -vf keyboard_config /etc/default/keyboard
dpkg-reconfigure --frontend noninteractive keyboard-configuration

# Install missing dependenies

apt-get -qq -y upgrade
apt-get -qq -y install \
    curl \
    libdbus-1-3 \
    libfontconfig1 \
    libgl1 \
    libx11-xcb1 \
    libxcb-glx0 \
    libxext6 \
    libxkbcommon-x11-0 \
    libxrender1 \
    wget

mkdir -p .local/bin

# Install Qt Installer Framework

qtIFW=QtInstallerFramework-linux-x64-${QTIFWVER}.run
${DOWNLOAD_CMD} "http://download.qt.io/official_releases/qt-installer-framework/${QTIFWVER}/${qtIFW}" || true

if [ -e "${qtIFW}" ]; then
    chmod +x "${qtIFW}"
    QT_QPA_PLATFORM=minimal \
    ./"${qtIFW}" \
        --verbose \
        --root ~/QtIFW \
        --accept-licenses \
        --accept-messages \
        --confirm-command \
        install
    cd .local
    cp -rvf ~/QtIFW/* .
    cd ..
fi

# Install dev tools

apt-get -qq -y install \
    g++ \
    git \
    kmod \
    libelf-dev \
    make \
    python3 \
    sparse \
    wget \
    xvfb

if [ ! -z "${USE_QEMU}" ]; then
    apt-get -qq -y install \
        debootstrap \
        ffmpeg \
        initramfs-tools \
        qemu-system-x86 \
        qemu-utils \
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

for package in ${modules} ${image} ${headers} ${headers_generic}; do
    ${DOWNLOAD_CMD} "${url}/${SYSTEM_ARCH}/${package}"
    dpkg -i "${package}"
done

if [ ! -z "${USE_QEMU}" ]; then
    update-initramfs -c -k "${KERNEL_VERSION}-generic"
fi
