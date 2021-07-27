#!/bin/bash
#
# akvcam, virtual camera for Linux.
# Copyright (C) 2020  Gonzalo Exequiel Pedone
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

git clone https://github.com/webcamoid/DeployTools.git

export PATH="${PWD}/.local/bin:${PATH}"
export INSTALL_PREFIX="${PWD}/package-data"
export PACKAGES_DIR="${PWD}/packages"
export BUILD_PATH=${PWD}/src
export PYTHONPATH="${PWD}/DeployTools"

xvfb-run --auto-servernum python3 \
        ./DeployTools/deploy.py \
        -d "${INSTALL_PREFIX}" \
        -c ./package_info.conf \
        -o "${PACKAGES_DIR}"
