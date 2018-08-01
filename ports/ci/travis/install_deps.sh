#!/bin/bash

# Set default Docker command
EXEC="docker exec ${DOCKERSYS}"
${EXEC} apt-get -y update
${EXEC} apt-get -y upgrade

# Install dev tools
${EXEC} apt-get -y install \
    g++ \
    make \
    linux-headers
