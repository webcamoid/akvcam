#!/bin/bash

# Set default Docker command
EXEC="docker exec ${DOCKERSYS}"
${EXEC} apt-get -y update
${EXEC} apt-get -y upgrade

# Install dev tools
${EXEC} apt-get -y install \
    g++ \
    make \
    libelf1 \
    kmod \
    wget

if [ ! -z "${USE_QEMU}" ]; then
${EXEC} apt-get -y install \
    debootstrap \
    qemu
fi

url=http://kernel.ubuntu.com/~kernel-ppa/mainline/v${KERNEL_VERSION_A}
headers=linux-headers-${KERNEL_VERSION}_${KERNEL_VERSION}.${KERNEL_VERSION_C}_all.deb
headers_generic=linux-headers-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_amd64.deb

if [ ! -z "${USE_QEMU}" ]; then
    if [ -z "${UNSIGNED_IMG}" ]; then
        image=linux-image-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_amd64.deb
    else
        image=linux-image-unsigned-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_amd64.deb
    fi
fi

for package in ${image} ${headers} ${headers_generic}; do
    ${EXEC} wget -c "${url}/${package}"
    ${EXEC} dpkg -i "${package}"
done
