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
echo

if [ ! -z "${USE_QEMU}" ]; then
    # Create the system image to boot with QEMU.
    qemu-img create -f raw ${system_image} 1g
    mkfs.ext4 -F ${system_image}

    # Install bootstrap system
    mkdir ${system_mount_point}
    mount -o loop ${system_image} ${system_mount_point}

    debootstrap \
        --components=main,universe,multiverse \
        --include=autofs,kmod,systemd,systemd-sysv,v4l-utils \
        --arch ${SYSTEM_ARCH} \
        --variant=minbase \
        ${SYSTEM_VERSION} ${system_mount_point}
    mkdir -p ${system_mount_point}/{dev,proc,sys}

    # Copy kernel modules
    mkdir -p ${system_mount_point}/lib/modules/${KERNEL_VERSION}-generic
    cp -rf /lib/modules/${KERNEL_VERSION}-generic/* ${system_mount_point}/lib/modules/${KERNEL_VERSION}-generic

    # Configure auto login with root user
    sed -i 's/#NAutoVTs=6/NAutoVTs=1/' ${system_mount_point}/etc/systemd/logind.conf
    sed -i 's/\/sbin\/agetty/\/sbin\/agetty --autologin root/' ${system_mount_point}/lib/systemd/system/*getty*.service
    sed -i 's/root:.:/root::/' ${system_mount_point}/etc/shadow

    # Prepare the system to test the driver
    cp -vf src/${DRIVER_FILE} ${system_mount_point}/root
    echo './driver_test.sh' >> ${system_mount_point}/root/.profile
    touch ${system_mount_point}/root/driver_test.sh
    chmod +x ${system_mount_point}/root/driver_test.sh

    echo '[ "\$(tty)" != /dev/tty1 ] && exit' >> ${system_mount_point}/root/driver_test.sh
    echo 'depmod -a >>driver_log.txt 2>&1' >> ${system_mount_point}/root/driver_test.sh
    echo 'modprobe v4l2-common' >> ${system_mount_point}/root/driver_test.sh
    echo 'modprobe videobuf2-core' >> ${system_mount_point}/root/driver_test.sh
    echo 'modprobe videobuf2-v4l2' >> ${system_mount_point}/root/driver_test.sh
    echo 'modprobe videobuf2-vmalloc' >> ${system_mount_point}/root/driver_test.sh
    echo 'modprobe videodev' >> ${system_mount_point}/root/driver_test.sh
    echo 'dmesg -C' >> ${system_mount_point}/root/driver_test.sh

    if [ -z "${DEFERRED_LOG}" ]; then
        echo 'insmod ${DRIVER_FILE} loglevel=7' >> ${system_mount_point}/root/driver_test.sh
        echo 'v4l2-ctl -d /dev/video0 --all -L' >> ${system_mount_point}/root/driver_test.sh
        echo 'v4l2-compliance -d /dev/video0 -f' >> ${system_mount_point}/root/driver_test.sh
    else
        echo 'insmod ${DRIVER_FILE} loglevel=7 >>driver_log.txt 2>&1' >> ${system_mount_point}/root/driver_test.sh
        echo 'v4l2-ctl -d /dev/video0 --all -L >>driver_log.txt 2>&1' >> ${system_mount_point}/root/driver_test.sh
        echo 'v4l2-compliance -d /dev/video0 -f >>driver_log.txt 2>&1' >> ${system_mount_point}/root/driver_test.sh
    fi

    echo 'rmmod ${DRIVER_FILE}' >> ${system_mount_point}/root/driver_test.sh

    if [ -z "${DEFERRED_LOG}" ]; then
        echo 'dmesg' >> ${system_mount_point}/root/driver_test.sh
    else
        echo 'dmesg >>driver_log.txt 2>&1' >> ${system_mount_point}/root/driver_test.sh
    fi

    echo 'shutdown -h now' >> ${system_mount_point}/root/driver_test.sh

    # Copy config.ini file.
    mkdir -p ${system_mount_point}/etc/akvcam
    cp -vf ports/ci/travis/config.ini ${system_mount_point}/etc/akvcam/config.ini

    # Choose a random wallpaper and use it as default frame.
    wallpaper=\$(ls /usr/share/backgrounds/*.{jpg,png} | shuf -n1)
    ffmpeg \
        -y \
        -i "\$wallpaper" \
        -s 640x480 \
        -pix_fmt bgr24 \
        ${system_mount_point}/etc/akvcam/default_frame.bmp

    umount ${system_mount_point}

    echo
    echo "Booting system with custom kernel:"
    echo
    qemu-system-x86_64 \\
        -kernel /boot/vmlinuz-${KERNEL_VERSION}-generic \\
        -initrd /boot/initrd.img-${KERNEL_VERSION}-generic \\
        -m 512M \\
        -append "root=/dev/sda console=ttyS0,9600 systemd.unit=multi-user.target rw" \\
        -drive file=${system_image},format=raw \\
        --nographic

    if [ ! -z "${DEFERRED_LOG}" ]; then
        mount -o loop ${system_image} ${system_mount_point}
        cat ${system_mount_point}/root/driver_log.txt
        umount ${system_mount_point}
    fi
fi
EOF
${EXEC} bash ${BUILDSCRIPT}
