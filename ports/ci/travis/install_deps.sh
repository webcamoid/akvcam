#!/bin/bash

# Set default Docker command
EXEC="docker exec ${DOCKERSYS}"
${EXEC} apt-get -y update
${EXEC} apt-get -y upgrade

# Install dev tools
${EXEC} apt-get -y install \
    g++ \
    make \
    wget

url=http://kernel.ubuntu.com/~kernel-ppa/mainline/v${KERNEL_VERSION_A}
headers=linux-headers-${KERNEL_VERSION}_${KERNEL_VERSION}.${KERNEL_VERSION_C}_all.deb
headers_generic=linux-headers-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_amd64.deb
#headers_modules=linux-modules-${KERNEL_VERSION}-generic_${KERNEL_VERSION}.${KERNEL_VERSION_C}_amd64.deb

for package in ${headers} ${headers_generic} ${headers_modules}; do
    ${EXEC} wget -c "${url}/${package}"
    ${EXEC} dpkg -i "${package}"
done
