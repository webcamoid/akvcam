#!/bin/sh

scriptPath=$(readlink -f "$_")
NAME=$1
VERSION=$2
TARGET_DIR=$(dirname "${scriptPath}")

if [ ! -z "$3" ]; then
    TARGET_DIR=$3
fi

SUDO_CMD=

if [ "$EUID" != 0 ]; then
    SUDO_CMD=sudo
fi

echo "${NAME} installation started"
chmod 755 "${TARGET_DIR}"

kernelVersion=$(uname -r)
linuxSources=/lib/modules/${kernelVersion}/build
cmds="dkms gcc kmod make"
missing_dependencies="";

for cmd in $cmds; do
    which "${cmd}" &>/dev/null

    if [ "$?" != 0 ]; then
        if [ -z "${missing_dependencies}" ]; then
            missing_dependencies="${cmd}"
        else
            missing_dependencies="${missing_dependencies}, ${cmd}"
        fi
    fi
done

if [ ! -e "${linuxSources}/include/generated/uapi/linux/version.h" ]; then
    if [ -z "${missing_dependencies}" ]; then
        missing_dependencies=linux-headers
    else
        missing_dependencies="${missing_dependencies}, linux-headers"
    fi
fi

if [ ! -z "${missing_dependencies}" ]; then
    echo "The following dependencies are missing: ${missing_dependencies}."
    echo "Install them and try again"

    exit -1
fi

echo "Creating a symlink to the sources"
${SUDO_CMD} ln -s "${TARGET_DIR}/src" "/usr/src/${NAME}-${VERSION}"
echo "Runnig DKMS"
${SUDO_CMD} dkms install "${NAME}/${VERSION}"
echo "Writting uninstall script"
uninstallScript=${TARGET_DIR}/uninstall.sh

if [ -w "${uninstallScript}" ]; then
    SUDO_CMD=
fi

${SUDO_CMD} tee "${uninstallScript}" > /dev/null <<EOF
#!/bin/sh

scriptPath=\$(readlink -f "\$_")
NAME=${NAME}
VERSION=${VERSION}
TARGET_DIR=\$(dirname "\${scriptPath}")

SUDO_CMD=

if [ "\$EUID" != 0 ]; then
    SUDO_CMD=sudo
fi

echo "${NAME} uninstallation started"
echo "Runnig DKMS"
\${SUDO_CMD} dkms remove "\${NAME}/\${VERSION}" --all
echo "Removing a symlink to the sources"
\${SUDO_CMD} rm -f "/usr/src/\${NAME}-\${VERSION}"
echo "${NAME} uninstallation finished"
EOF

${SUDO_CMD} chmod +x "${uninstallScript}"
echo "${NAME} installation finished"
