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

DEPLOYSCRIPT=deployscript.sh

cat << EOF > ${DEPLOYSCRIPT}
#!/bin/sh

export PATH="\${PWD}/.local/bin:\${PATH}"
export INSTALL_PREFIX="\${PWD}/package-data"
export PACKAGES_DIR="\${PWD}/packages-${REPOSITORY}"
export PYTHONPATH="\${PWD}/DeployTools"
export GITHUB_REF=$GITHUB_REF
export GITHUB_SERVER_URL=$GITHUB_SERVER_URL
export GITHUB_REPOSITORY=$GITHUB_REPOSITORY
export GITHUB_RUN_ID=$GITHUB_RUN_ID
EOF

if [ ! -z "${DAILY_BUILD}" ]; then
    cat << EOF >> ${DEPLOYSCRIPT}
export DAILY_BUILD=1
EOF
fi

cat << EOF >> ${DEPLOYSCRIPT}
cd src
make install INSTALLDIR=\${INSTALL_PREFIX}/src
cd ..
cp -vf package_info.conf.in package_info.conf
version=\$(grep '^MODULE_VERSION' src/Makefile | awk -F= '{print \$2}' | tr -d ' ')
sed -i "s|@VERSION@|\${version}|g" package_info.conf
sed -i "s|@CMAKE_SOURCE_DIR@|\${PWD}|g" package_info.conf
sed -i "s|@QTIFW_TARGET_DIR@|@ApplicationsDir@/akvcam|g" package_info.conf
xvfb-run --auto-servernum python3 \
        ./DeployTools/deploy.py \
        -d "\${INSTALL_PREFIX}" \
        -c ./package_info.conf \
        -o "\${PACKAGES_DIR}"
EOF

chmod +x ${DEPLOYSCRIPT}
${EXEC} bash ${DEPLOYSCRIPT}
