#!/bin/sh

scriptPath=$(readlink -f "$_")
installDataFile=install.data
NAME=$1

if [ -z "$NAME" ]; then
    NAME=$(grep 'NAME=' "${installDataFile}" 2>/dev/null)
    NAME=${NAME#*=}
fi

VERSION=$2

if [ -z "$VERSION" ]; then
    VERSION=$(grep 'VERSION=' "${installDataFile}" 2>/dev/null)
    VERSION=${VERSION#*=}
fi

TARGET_DIR=$(dirname "${scriptPath}")

if [ ! -z "$3" ]; then
    TARGET_DIR=$3
fi

SUDO_CMD=

if [ "$EUID" != 0 ]; then
    SUDO_CMD=sudo
fi

if [ -z "${NAME}" ]; then
    echo "Installation started"
else
    echo "${NAME} installation started"
fi

echo
chmod 755 "${TARGET_DIR}"

function detect_missing_dependencies() {
    linuxSources=/lib/modules/$(uname -r)/build
    cmds="dkms gcc kmod make"
    missing_dependencies="";

    for cmd in $cmds; do
        which "${cmd}" &>/dev/null

        if [ "$?" != 0 ]; then
            if [ -z "${missing_dependencies}" ]; then
                missing_dependencies="${cmd}"
            else
                missing_dependencies="${missing_dependencies} ${cmd}"
            fi
        fi
    done

    if [ ! -e "${linuxSources}/include/generated/uapi/linux/version.h" ]; then
        if [ -z "${missing_dependencies}" ]; then
            missing_dependencies=linux-headers
        else
            missing_dependencies="${missing_dependencies} linux-headers"
        fi
    fi

    echo "${missing_dependencies}"
}

function distro() {
    distroId=$(grep -h ^ID_LIKE= /etc/*-release | tr -d '"')

    if [ -z "${distroId}" ]; then
        distroId=$(grep -h ^ID= /etc/*-release | tr -d '"')
    fi

    distroId=${distroId#*=}

    case "${distroId}" in
        *suse*)
            distroId=opensuse
            ;;
        *)
            ;;
    esac

    echo "${distroId}"
}

function distro_package() {
    distroId=$1
    package=$2
    depsMap=""

    case "${distro}Id" in
        arch)
            depsMap=";dkms:dkms"
            depsMap="${depsMap};gcc:gcc"
            depsMap="${depsMap};kmod:kmod"
            depsMap="${depsMap};make:make"
            depsMap="${depsMap};linux-headers:linux-headers"
            ;;
        debian)
            depsMap=";dkms:dkms"
            depsMap="${depsMap};gcc:gcc"
            depsMap="${depsMap};kmod:kmod"
            depsMap="${depsMap};make:make"
            depsMap="${depsMap};linux-headers:linux-headers-$(uname -r)"
            ;;
        fedora)
            depsMap=";dkms:dkms"
            depsMap="${depsMap};gcc:gcc"
            depsMap="${depsMap};kmod:kmod"
            depsMap="${depsMap};make:make"
            depsMap="${depsMap};linux-headers:kernel-devel kernel-headers"
            ;;
        mageia)
            depsMap=";dkms:dkms"
            depsMap="${depsMap};gcc:gcc"
            depsMap="${depsMap};kmod:kmod"
            depsMap="${depsMap};make:make"
            depsMap="${depsMap};linux-headers:kernel-linus-devel"
            ;;
        opensuse)
            depsMap=";dkms:dkms"
            depsMap="${depsMap};gcc:gcc"
            depsMap="${depsMap};kmod:kmod"
            depsMap="${depsMap};make:make"
            depsMap="${depsMap};linux-headers:kernel-devel"
            ;;
        *)
            depsMap=";dkms:dkms"
            depsMap="${depsMap};gcc:gcc"
            depsMap="${depsMap};kmod:kmod"
            depsMap="${depsMap};make:make"
            depsMap="${depsMap};linux-headers:linux-headers"
            ;;
    esac

    packages="${depsMap#*;${package}:}"
    packages="${packages%%;*}"

    if [ -z "${packages}" ]; then
        echo "${package}"
    else
        echo "${packages}"
    fi
}

function distro_packages() {
    distroId=$1
    packages=""

    for package in ${@:2}; do
        distroPackage=$(distro_package "${distroId}" "${package}")

        if [ -z "${packages}" ]; then
            packages="${distroPackage}"
        else
            packages="${packages} ${distroPackage}"
        fi
    done

    echo "${packages}"
}

function install_packages() {
    distroId=$1
    missing_dependencies=${@:2}
    SUDO_CMD=

    if [ "$EUID" != 0 ]; then
        SUDO_CMD=sudo
    fi

    case "${distroId}" in
        arch)
            ${SUDO_CMD} pacman -Syu --noconfirm --ignore linux,linux-api-headers,linux-docs,linux-firmware,linux-headers,pacman
            ${SUDO_CMD} pacman --noconfirm --needed -S ${missing_dependencies}
            ;;
        debian)
            ${SUDO_CMD} apt-get -y update
            ${SUDO_CMD} apt-get -y upgrade
            ${SUDO_CMD} apt-get -y install ${missing_dependencies}
            ;;
        fedora)
            ${SUDO_CMD} dnf -y upgrade-minimal --exclude=systemd,systemd-libs
            ${SUDO_CMD} dnf -y --skip-broken install ${missing_dependencies}
            ;;
        mageia)
            ${SUDO_CMD} dnf -y update
            ${SUDO_CMD} dnf -y install ${missing_dependencies}
            ;;
        opensuse)
            ${SUDO_CMD} zypper -n dup
            ${SUDO_CMD} zypper -n in ${missing_dependencies}
            ;;
        *)
            ;;
    esac
}

function is_distro_supported() {
    distroId=$1

    case "${distroId}" in
        arch)
            ;;
        debian)
            ;;
        fedora)
            ;;
        mageia)
            ;;
        opensuse)
            ;;
        *)
            echo false

            return 0
            ;;
    esac

    echo true

    return 1
}

distroId=$(distro)
missing_dependencies=$(detect_missing_dependencies)
missing_dependencies=$(distro_packages "${distroId}" ${missing_dependencies})

if [ ! -z "${missing_dependencies}" ]; then
    if [ "$(is_distro_supported "${distroId}")" = true ]; then
        echo "Installing missing dependencies"
        echo
        install_packages "${distroId}" ${missing_dependencies}

        if [ "$?" != 0 ]; then
            exit $?
        fi
    else
        echo "The following dependencies are missing: ${missing_dependencies// /, }." >&2
        echo >&2
        echo "Install them and try again" >&2

        exit -1
    fi
fi

echo "Creating a symlink to the sources"
${SUDO_CMD} ln -s "${TARGET_DIR}/src" "/usr/src/${NAME}-${VERSION}"
echo "Runnig DKMS"
${SUDO_CMD} dkms install "${NAME}/${VERSION}"

if [ -w "${TARGET_DIR}" ]; then
    SUDO_CMD=
fi

echo "Writting install data"
installData=${TARGET_DIR}/${installDataFile}

${SUDO_CMD} tee "${installData}" > /dev/null <<EOF
NAME=${NAME}
VERSION=${VERSION}
EOF

${SUDO_CMD} chmod 644 "${installData}"

echo "Writting undo script"
uninstallScript=${TARGET_DIR}/undo.sh

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

if [ -z "\${NAME}" ]; then
    echo "Uninstallation started"
else
    echo "\${NAME} uninstallation started"
fi

echo
echo "Runnig DKMS"
\${SUDO_CMD} dkms remove "\${NAME}/\${VERSION}" --all
echo "Removing the symlink to the sources"
\${SUDO_CMD} rm -f "/usr/src/\${NAME}-\${VERSION}"
echo

if [ -z "\${NAME}" ]; then
    echo "Uninstallation finished"
else
    echo "\${NAME} uninstallation finished"
fi

echo "You can now delete '\$(dirname \$(realpath \$0))' folder"
EOF

${SUDO_CMD} chmod 755 "${uninstallScript}"

echo

if [ -z "${NAME}" ]; then
    echo "Installation finished"
else
    echo "${NAME} installation finished"
fi
