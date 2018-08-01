#!/bin/bash

EXEC="docker exec ${DOCKERSYS}"
BUILDSCRIPT=dockerbuild.sh

cat << EOF >> ${BUILDSCRIPT}
cd src
make
EOF
${EXEC} bash ${BUILDSCRIPT}
