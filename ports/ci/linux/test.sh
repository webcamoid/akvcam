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

export BUILD_PATH=${PWD}/src-${REPOSITORY%.*}
DRIVER_FILE=akvcam.ko
DEFERRED_LOG=1
system_image=system-image.img
system_mount_point=system-mount-point

if [ "${USE_QEMU}" = 1 ]; then
    # Create the system image to boot with QEMU.
    qemu-img create -f raw ${system_image} 1g
    mkfs.ext4 -F ${system_image}

    # Install bootstrap system
    mkdir ${system_mount_point}
    mount -v -o loop,rw,sync ${system_image} ${system_mount_point}

    mkdir -p ${system_mount_point}/{dev,proc,sys}
    debootstrap \
        --components=main,universe,multiverse \
        --include=autofs,kmod,systemd,systemd-sysv,util-linux,v4l-utils \
        --arch "${SYSTEM_ARCH}" \
        --variant=minbase \
        "${SYSTEM_VERSION}" \
        "${system_mount_point}"

    # Copy kernel modules
    mkdir -p "${system_mount_point}/lib/modules/${KERNEL_VERSION}-generic"
    cp -rf "/lib/modules/${KERNEL_VERSION}-generic"/* "${system_mount_point}/lib/modules/${KERNEL_VERSION}-generic"

    # Configure auto login with root user
    sed -i 's/#NAutoVTs=6/NAutoVTs=1/' ${system_mount_point}/etc/systemd/logind.conf
    sed -i 's|/sbin/agetty|/sbin/agetty --autologin root|' ${system_mount_point}/lib/systemd/system/*getty*.service
    sed -i 's/root:.:/root::/' ${system_mount_point}/etc/shadow

    # Prepare the system to test the driver
    cp -vf "${BUILD_PATH}/${DRIVER_FILE}" ${system_mount_point}/root
    echo './driver_test.sh' >> ${system_mount_point}/root/.profile
    touch ${system_mount_point}/root/driver_test.sh
    chmod +x ${system_mount_point}/root/driver_test.sh

    cat << EOF > ${system_mount_point}/root/driver_test.sh
[ "\$(tty)" != /dev/tty1 ] && exit
depmod -a >>driver_log.txt 2>&1
modprobe v4l2-common
modprobe videobuf2-core
modprobe videobuf2-v4l2
modprobe videobuf2-vmalloc
modprobe videodev
dmesg -C

if [ "${DEFERRED_LOG}" = 1 ]; then
    insmod ${DRIVER_FILE} loglevel=7 >>driver_log.txt 2>&1
    v4l2-ctl -d /dev/video0 --all -L >>driver_log.txt 2>&1
    v4l2-compliance -d /dev/video0 -s -f >>driver_log.txt 2>&1
    rmmod ${DRIVER_FILE} >>driver_log.txt 2>&1
    dmesg >>driver_log.txt 2>&1
else
    insmod ${DRIVER_FILE} loglevel=7
    v4l2-ctl -d /dev/video0 --all -L
    v4l2-compliance -d /dev/video0 -s -f
    rmmod ${DRIVER_FILE}
    dmesg
fi

shutdown -h now
EOF

    # Copy config.ini file.
    mkdir -p ${system_mount_point}/etc/akvcam
    cp -vf ports/ci/linux/config.ini ${system_mount_point}/etc/akvcam/config.ini

    # Choose a random wallpaper and use it as default frame.
    wallpaper=$(ls /usr/share/backgrounds/*.{jpg,png} | shuf -n1)
    ffmpeg \
        -y \
        -i "${wallpaper}" \
        -s 640x480 \
        -pix_fmt bgr24 \
        ${system_mount_point}/etc/akvcam/default_frame.bmp

    # FIXME: For Ubuntu > Groovy Gorilla the folder 'system-mount-point'
    # magically disappears and umount returns the following message:
    #
    # umount: system-mount-point: umount failed: No such file or directory.
    #
    # Patches welcome.
    umount -vf ${system_mount_point}

    echo
    echo "Booting system with custom kernel:"
    echo
    qemu-system-x86_64 \
        -kernel "/boot/vmlinuz-${KERNEL_VERSION}-generic" \
        -initrd "/boot/initrd.img-${KERNEL_VERSION}-generic" \
        -m 512M \
        -append "root=/dev/sda console=ttyS0,9600 systemd.unit=multi-user.target rw" \
        -drive file=${system_image},format=raw \
        --nographic

    if [ ! -z "${DEFERRED_LOG}" ]; then
        mount -o loop,rw,sync ${system_image} ${system_mount_point}
        cat ${system_mount_point}/root/driver_log.txt
        umount -vf ${system_mount_point}/
    fi
fi
