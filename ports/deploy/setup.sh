#!/bin/sh

scriptPath=$(readlink -f "$0")
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

SCRIPT_BASENAME=$(basename "$0")
SUDO_CMD=

if [ ! -w "${TARGET_DIR}" ]; then
    SUDO_CMD=sudo
fi

if [ -z "${NAME}" ]; then
    echo "Installation started"
else
    echo "${NAME} installation started"
fi

echo
${SUDO_CMD} chmod 755 "${TARGET_DIR}"

echo "Writting install data"
installData=${TARGET_DIR}/${installDataFile}

${SUDO_CMD} tee "${installData}" > /dev/null <<EOF
NAME=${NAME}
VERSION=${VERSION}
EOF

${SUDO_CMD} chmod 644 "${installData}"

echo "Writting undo script"
undoScript=${TARGET_DIR}/undo.sh

${SUDO_CMD} tee "${undoScript}" > /dev/null <<EOF
#!/bin/sh

scriptPath=\$(readlink -f "\$0")
NAME=${NAME}
VERSION=${VERSION}

SUDO_CMD=

if [ "\$EUID" != 0 ]; then
    SUDO_CMD=sudo
fi

if [ -z "\${NAME}" ]; then
    echo "Reverting setup"
else
    echo "Reverting \${NAME} setup"
fi

echo
echo "Runnig DKMS"
\${SUDO_CMD} dkms remove "\${NAME}/\${VERSION}" --all
echo "Removing the symlink to the sources"
\${SUDO_CMD} rm -f "/usr/src/\${NAME}-\${VERSION}"
echo

if [ -z "\${NAME}" ]; then
    echo "Reverted setup"
else
    echo "Reverted \${NAME} setup"
fi
EOF

${SUDO_CMD} chmod 755 "${undoScript}"

echo "Writting uninstall script"
uninstallScript=${TARGET_DIR}/uninstall.sh

${SUDO_CMD} tee "${uninstallScript}" > /dev/null <<EOF
#!/bin/sh

scriptPath=\$(readlink -f "\$0")
NAME=${NAME}
VERSION=${VERSION}
TARGET_DIR=\$(dirname "\${scriptPath}")

SUDO_CMD=

if [ ! -w "\${TARGET_DIR}" ]; then
    SUDO_CMD=sudo
fi

if [ -z "\${NAME}" ]; then
    echo "Uninstallation started"
else
    echo "\${NAME} uninstallation started"
fi

echo
"\${TARGET_DIR}/undo.sh"

is_sensible_directory() {
    directory=\$1

    # https://wiki.archlinux.org/title/XDG_user_directories
    . ~/.config/user-dirs.dirs 2>/dev/null

    # https://refspecs.linuxfoundation.org/FHS_3.0/fhs-3.0.html
    sensible_directories="
        /
        /bin
        /boot
        /dev
        /etc
        /home
        \${HOME}
        /lib
        /media
        /mnt
        /opt
        /proc
        /root
        /run
        /sbin
        /svr
        /sys
        /tmp
        /usr
        /usr/bin
        /usr/include
        /usr/lib
        /usr/libexec
        /usr/local
        /usr/local/share
        /usr/sbin
        /usr/share
        /usr/share/color
        /usr/share/dict
        /usr/share/man
        /usr/share/misc
        /usr/share/ppd
        /usr/share/sgml
        /usr/share/xml
        /usr/src
        /var
        /var/account
        /var/cache
        /var/cache/fonts
        /var/cache/man
        /var/crash
        /var/games
        /var/lib
        /var/lib/color
        /var/lib/hwclock
        /var/lib/misc
        /var/lock
        /var/log
        /var/mail
        /var/opt
        /var/run
        /var/spool
        /var/spool/cron
        /var/spool/lpd
        /var/spool/rwho
        /var/tmp
        /var/yp
        \${XDG_DESKTOP_DIR}
        \${XDG_DOCUMENTS_DIR}
        \${XDG_DOWNLOAD_DIR}
        \${XDG_MUSIC_DIR}
        \${XDG_PICTURES_DIR}
        \${XDG_PUBLICSHARE_DIR}
        \${XDG_TEMPLATES_DIR}
        \${XDG_VIDEOS_DIR}"

    for sdir in \$sensible_directories; do
        if [ "\$sdir" = "\$directory" ]; then
            echo true

            return 1
        fi
    done

    echo false

    return 0
}

sensibleDir=\$(is_sensible_directory "\${TARGET_DIR}")

if [ "\${sensibleDir}" = true ]; then
    echo "'\${TARGET_DIR}' can't be deleted"
else
    echo "Deleting '\${TARGET_DIR}'"
    \${SUDO_CMD} rm -rf "\${TARGET_DIR}" 2>/dev/null
fi

echo

if [ -z "\${NAME}" ]; then
    echo "Uninstallation finished"
else
    echo "\${NAME} uninstallation finished"
fi
EOF

${SUDO_CMD} chmod 755 "${uninstallScript}"

detect_missing_dependencies() {
    linuxSources=/lib/modules/$(uname -r)/build
    cmds="dkms gcc kmod make"
    missing_dependencies="";

    for cmd in $cmds; do
        cmdPath=$(which "${cmd}" 2>/dev/null)

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

distro() {
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

distro_package() {
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

distro_packages() {
    distroId=$1
    shift
    packages=""

    for package in $@; do
        distroPackage=$(distro_package "${distroId}" "${package}")

        if [ -z "${packages}" ]; then
            packages="${distroPackage}"
        else
            packages="${packages} ${distroPackage}"
        fi
    done

    echo "${packages}"
}

install_packages() {
    distroId=$1
    shift
    missing_dependencies=$@
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

is_distro_supported() {
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
        echo "Installing missing dependencies: ${missing_dependencies// /, }"
        echo
        install_packages "${distroId}" ${missing_dependencies}

        if [ "$?" != 0 ]; then
            exit $?
        fi
    else
        echo "The following dependencies are missing: ${missing_dependencies// /, }." >&2
        echo >&2
        echo "Install them and then run '${TARGET_DIR}/${SCRIPT_BASENAME}' script." >&2

        exit -1
    fi
fi

echo "Creating a symlink to the sources"

write_link() {
    SUDO_CMD=

    if [ "$EUID" != 0 ]; then
        SUDO_CMD=sudo
    fi

    ${SUDO_CMD} ln -sf "${TARGET_DIR}/src" "/usr/src/${NAME}-${VERSION}"

    if [ "$?" != 0 ]; then
        exit $?
    fi
}

write_link

echo "Runnig DKMS"

run_dkms() {
    SUDO_CMD=

    if [ "$EUID" != 0 ]; then
        SUDO_CMD=sudo
    fi

    ${SUDO_CMD} dkms install "${NAME}/${VERSION}"

    if [ "$?" != 0 ]; then
        exit $?
    fi
}

run_dkms
echo

if [ -z "${NAME}" ]; then
    echo "Installation finished"
else
    echo "${NAME} installation finished"
fi
