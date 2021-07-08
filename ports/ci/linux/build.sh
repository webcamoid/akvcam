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

DRIVER_FILE=akvcam.ko
DEFERRED_LOG=1
BUILDSCRIPT=dockerbuild.sh
system_image=system-image.img
system_mount_point=system-mount-point

cat << EOF >> ${BUILDSCRIPT}
echo "Available kernel headers:"
echo
ls /usr/src | grep linux-headers- | sort
echo

if [ ! -z "${USE_QEMU}" ]; then
    echo "Available kernel images:"
    echo
    ls /boot/vmlinuz-* | sort
    echo
    echo "Available RAM disk images:"
    echo
    ls /boot/initrd.img-* | sort
    echo
    echo "Available kernel modules:"
    echo
    ls /lib/modules | sort
    echo
    echo "Available QEMU images:"
    echo
    ls /usr/bin/qemu-system-* | sort
    echo
fi

# Build the driver and show it's info.
cd src
make KERNEL_DIR=/usr/src/linux-headers-${KERNEL_VERSION}-generic USE_SPARSE=1
cd ..
echo
echo "Driver info:"
echo
modinfo src/${DRIVER_FILE}
EOF
${EXEC} bash ${BUILDSCRIPT}
