#!/bin/bash

EXEC="docker exec ${DOCKERSYS}"
BUILDSCRIPT=dockerbuild.sh

cat << EOF >> ${BUILDSCRIPT}
echo "Available headers:"
ls /usr/src | sort
echo

pushd /usr/src/linux-headers-4.4.0-131
make oldconfig && make prepare
popd

cd src
make KERNEL_DIR=/usr/src/linux-headers-4.4.0-131
EOF
${EXEC} bash ${BUILDSCRIPT}
