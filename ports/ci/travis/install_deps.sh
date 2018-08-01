#!/bin/bash

KERNEL_VERSION_A=4.17.11
KERNEL_VERSION_B=041711
KERNEL_VERSION_C=201807280505

# Set default Docker command
EXEC="docker exec ${DOCKERSYS}"
${EXEC} apt-get -y update
${EXEC} apt-get -y upgrade

# Install dev tools
${EXEC} apt-get -y install \
    g++ \
    make \
    wget \
    linux-headers-generic

KERNEL_VERSION=${KERNEL_VERSION_A}-${KERNEL_VERSION_B}
url=http://kernel.ubuntu.com/~kernel-ppa/mainline/v${KERNEL_VERSION_A}
headers=linux-headers-${KERNEL_VERSION}_${KERNEL_VERSION}.${KERNEL_VERSION_C}_all.deb
headers_generic=linux-headers-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_amd64.deb
headers_modules=linux-modules-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_amd64.deb

for package in ${headers} ${headers_generic} ${headers_modules}; do
    ${EXEC} wget -c "${url}/${package}"
    ${EXEC} dpkg -i "${package}"
done
