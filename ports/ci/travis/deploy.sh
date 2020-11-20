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

cd ports/deploy
git clone https://github.com/webcamoid/DeployTools.git
cd ../..

DEPLOYSCRIPT=deployscript.sh

cat << EOF > ${DEPLOYSCRIPT}
#!/bin/sh

export PATH="\$PWD/.local/bin:\$PATH"
export PYTHONPATH="\$PWD/ports/deploy/DeployTools"
EOF

if [ ! -z "${DAILY_BUILD}" ]; then
    cat << EOF >> ${DEPLOYSCRIPT}
export DAILY_BUILD=1
EOF
fi

cat << EOF >> ${DEPLOYSCRIPT}
xvfb-run --auto-servernum python3 ports/deploy/deploy.py
EOF

chmod +x ${DEPLOYSCRIPT}
${EXEC} bash ${DEPLOYSCRIPT}
