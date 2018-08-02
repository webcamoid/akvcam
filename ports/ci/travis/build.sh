#!/bin/bash

EXEC="docker exec ${DOCKERSYS}"
DRIVER_FILE=akvcam.ko
BUILDSCRIPT=dockerbuild.sh
system_image=system-image.img
system_mount_point=system-mount-point

cat << EOF >> ${BUILDSCRIPT}
echo "Available kernel headers:"
echo
ls /usr/src | grep linux-headers- | sort
echo
echo "Available kernel images:"
echo
ls /boot/vmlinuz-* | sort
echo
echo "Available QEMU images:"
echo
ls /usr/bin/qemu-system-* | sort
echo

# Build the driver and show it's info.
cd src
make KERNEL_DIR=/usr/src/linux-headers-${KERNEL_VERSION}-generic
cd ..
echo
echo "Driver info:"
echo
modinfo src/${DRIVER_FILE}
echo

# Create the system image to boot with QEMU.
qemu-img create ${system_image} 1g
mkfs.ext4 ${system_image}

# Install bootstrap system
mkdir ${system_mount_point}
mount -o loop ${system_image} ${system_mount_point}
debootstrap --arch amd64 xenial ${system_mount_point}

# Configure auto login with root user
sed -i 's/#NAutoVTs=6/NAutoVTs=1/' ${system_mount_point}/etc/systemd/logind.conf
sed -i 's/\/sbin\/agetty/\/sbin\/agetty --autologin root/' ${system_mount_point}/lib/systemd/system/*getty*
sed -i 's/root:.:/root::/' ${system_mount_point}/etc/shadow
mkdir -p ${system_mount_point}/etc/systemd/system/getty@tty1.service.d
echo '[Service]' >> ${system_mount_point}/etc/systemd/system/getty@tty1.service.d/override.conf
echo 'ExecStart=/sbin/agetty --noissue --autologin root %I \$TERM' >> ${system_mount_point}/etc/systemd/system/getty@tty1.service.d/override.conf
echo 'Type=idle' >> ${system_mount_point}/etc/systemd/system/getty@tty1.service.d/override.conf

# Prepare the system to test the driver
cp -vf src/${DRIVER_FILE} ${system_mount_point}/root
echo './driver_test.sh' >> ${system_mount_point}/root/.profile
touch ${system_mount_point}/root/driver_test.sh
chmod +x ${system_mount_point}/root/driver_test.sh
echo 'dmesg -C' >> ${system_mount_point}/root/driver_test.sh
echo 'insmod ${DRIVER_FILE}' >> ${system_mount_point}/root/driver_test.sh
echo 'dmesg' >> ${system_mount_point}/root/driver_test.sh
echo 'shutdown -h now' >> ${system_mount_point}/root/driver_test.sh

echo
echo "Booting system with custom kernel:"
echo
qemu-system-x86_64 \\
    -kernel /boot/vmlinuz-${KERNEL_VERSION}-generic \\
    -append "root=/dev/sda console=ttyS0" \\
    -hda ${system_image} \\
    --nographic
EOF
${EXEC} bash ${BUILDSCRIPT}
