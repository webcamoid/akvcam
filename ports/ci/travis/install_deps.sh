#!/bin/bash

# Set default Docker command
${EXEC} apt-get -y update
${EXEC} apt-get -y upgrade

# Install dev tools
${EXEC} apt-get -y install \
    g++ \
    make \
    libelf-dev \
    kmod \
    wget

if [ ! -z "${USE_QEMU}" ]; then
${EXEC} apt-get -y install \
    debootstrap \
    qemu-system-x86 \
    qemu-utils
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
