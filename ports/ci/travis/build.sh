#!/bin/bash

EXEC="docker exec ${DOCKERSYS}"
BUILDSCRIPT=dockerbuild.sh

cat << EOF >> ${BUILDSCRIPT}
echo "Available headers:"
echo
ls /usr/src | sort
echo

cd src
make KERNEL_DIR=/usr/src/linux-headers-${KERNEL_VERSION}-generic
modinfo akvcam.ko
EOF
${EXEC} bash ${BUILDSCRIPT}
