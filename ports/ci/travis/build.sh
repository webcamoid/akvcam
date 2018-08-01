#!/bin/bash

EXEC="docker exec ${DOCKERSYS}"
BUILDSCRIPT=dockerbuild.sh

cat << EOF >> ${BUILDSCRIPT}
ls /usr/src | sort
cd src
make KERNEL_DIR=/usr/src/linux-headers-4.4.0-130
EOF
${EXEC} bash ${BUILDSCRIPT}
