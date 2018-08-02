#!/bin/bash

EXEC="docker exec ${DOCKERSYS}"
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
echo
echo "Driver info:"
echo
modinfo akvcam.ko
echo

# Create the system image to boot with QEMU.
qemu-img create ${system_image} 1g
mkfs.ext4 ${system_image}

# Install bootstrap system
mkdir ${system_mount_point}
EOF
${EXEC} bash ${BUILDSCRIPT}
rm -vf ${BUILDSCRIPT}

mount -o loop ${system_image} ${system_mount_point}

cat << EOF >> ${BUILDSCRIPT}
fakechroot fakeroot debootstrap --variant=fakechroot --arch amd64 xenial ${system_mount_point}

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
