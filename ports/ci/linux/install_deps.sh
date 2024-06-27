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
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-xfixes0 \
    libxcb-xinerama0 \
    libxext6 \
    libxkbcommon-x11-0 \
    libxrender1 \
    wget

mkdir -p .local/bin

if [ -z "${ARCHITECTURE}" ]; then
    architecture="${DOCKERIMG%%/*}"
else
    case "${ARCHITECTURE}" in
        aarch64)
            architecture=arm64v8
            ;;
        armv7)
            architecture=arm32v7
            ;;
        *)
            architecture=${ARCHITECTURE}
            ;;
    esac
fi

if [[ ( "${architecture}" = amd64 || "${architecture}" = arm64v8 ) && ! -z "${QTIFWVER}" ]]; then
    # Install Qt Installer Framework

    case "${architecture}" in
        arm64v8)
            qtArch=arm64
            ;;
        *)
            qtArch=x64
            ;;
    esac

    qtIFW=QtInstallerFramework-linux-${qtArch}-${QTIFWVER}.run
    ${DOWNLOAD_CMD} "http://download.qt.io/official_releases/qt-installer-framework/${QTIFWVER}/${qtIFW}" || true

    if [ -e "${qtIFW}" ]; then
        if [ "${architecture}" = arm64v8 ]; then
            ln -svf libtiff.so.6 /usr/lib/aarch64-linux-gnu/libtiff.so.5
            ln -svf libwebp.so.7 /usr/lib/aarch64-linux-gnu/libwebp.so.6
        fi

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
fi

# Install dev tools

apt-get -qq -y install \
    g++ \
    git \
    kmod \
    libelf-dev \
    make \
    makeself \
    python3 \
    sparse \
    wget \
    xvfb \
    xz-utils

case "$architecture" in
    arm64v8)
        systemArch=arm64
        ;;
    arm32v7)
        systemArch=armhf
        ;;
    *)
        systemArch=amd64
        ;;
esac

url=https://kernel.ubuntu.com/mainline/${REPOSITORY}
headers=${systemArch}/linux-headers-${KERNEL_VERSION}_${KERNEL_VERSION}.${KERNEL_VERSION_C}_all.deb
headers_generic=${systemArch}/linux-headers-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_${systemArch}.deb

for package in ${headers} ${headers_generic}; do
    ${DOWNLOAD_CMD} "${url}/${package}"
    dpkg -i "${package#*/}"
done
