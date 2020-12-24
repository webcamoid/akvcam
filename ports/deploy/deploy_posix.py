#!/usr/bin/env python
# -*- coding: utf-8 -*-

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

import math
import os
import platform
import subprocess # nosec
import sys
import threading

from WebcamoidDeployTools import DTDeployBase
from WebcamoidDeployTools import DTQt5


class Deploy(DTDeployBase.DeployBase, DTQt5.Qt5Tools):
    def __init__(self):
        super().__init__()
        rootDir = os.path.normpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), '../..'))
        self.setRootDir(rootDir)
        self.installDir = os.path.join(self.buildDir, 'ports/deploy/temp_priv')
        self.pkgsDir = os.path.join(self.buildDir, 'ports/deploy/packages_auto', sys.platform)
        self.detectQtIFW()
        self.detectQtIFWVersion()
        self.programName = 'akvcam'
        self.rootInstallDir = os.path.join(self.installDir, self.programName + '_package')
        self.programVersion = self.detectVersion(os.path.join(self.rootDir, 'src/dkms.conf'))
        self.detectMake()
        self.packageConfig = os.path.join(self.rootDir, 'ports/deploy/package_info.conf')
        self.installerConfig = os.path.join(self.installDir, 'installer/config')
        self.installerPackages = os.path.join(self.installDir, 'installer/packages')
        self.licenseFile = os.path.join(self.rootDir, 'COPYING')
        self.installerTargetDir = '@ApplicationsDir@/' + self.programName
        self.installerScript = os.path.join(self.rootDir, 'ports/deploy/installscript.posix.qs')
        self.changeLog = os.path.join(self.rootDir, 'ChangeLog')
        self.outPackage = os.path.join(self.pkgsDir,
                                       'akvcam-installer-{}.run'.format(self.programVersion))

    @staticmethod
    def detectVersion(proFile):
        if 'DAILY_BUILD' in os.environ:
            return 'daily'

        version = '0.0.0'

        try:
            with open(proFile) as f:
                for line in f:
                    if line.startswith('PACKAGE_VERSION') and '=' in line:
                        version = line.split('=')[1].replace('"', '').strip()
        except:
            pass

        return version

    def prepare(self):
        print('Executing make install')
        params = {'INSTALLDIR': os.path.join(self.rootInstallDir, 'src')}
        self.makeInstall(os.path.join(self.rootDir, 'src'), params)
        print('\nWritting build system information\n')
        self.writeBuildInfo()

    def commitHash(self):
        try:
            process = subprocess.Popen(['git', 'rev-parse', 'HEAD'], # nosec
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE,
                                        cwd=self.rootDir)
            stdout, _ = process.communicate()

            if process.returncode != 0:
                return ''

            return stdout.decode(sys.getdefaultencoding()).strip()
        except:
            return ''

    @staticmethod
    def sysInfo():
        info = ''

        for f in os.listdir('/etc'):
            if f.endswith('-release'):
                with open(os.path.join('/etc' , f)) as releaseFile:
                    info += releaseFile.read()

        if len(info) < 1:
            info = ' '.join(platform.uname())

        return info

    def writeBuildInfo(self):
        shareDir = os.path.join(self.rootInstallDir, 'share')

        try:
            os.makedirs(self.pkgsDir)
        except:
            pass

        depsInfoFile = os.path.join(shareDir, 'build-info.txt')

        if not os.path.exists(shareDir):
            os.makedirs(shareDir)

        # Write repository info.

        with open(depsInfoFile, 'w') as f:
            commitHash = self.commitHash()

            if len(commitHash) < 1:
                commitHash = 'Unknown'

            print('    Commit hash: ' + commitHash)
            f.write('Commit hash: ' + commitHash + '\n')

            buildLogUrl = ''

            if 'TRAVIS_BUILD_WEB_URL' in os.environ:
                buildLogUrl = os.environ['TRAVIS_BUILD_WEB_URL']
            elif 'APPVEYOR_ACCOUNT_NAME' in os.environ and 'APPVEYOR_PROJECT_NAME' in os.environ and 'APPVEYOR_JOB_ID' in os.environ:
                buildLogUrl = 'https://ci.appveyor.com/project/{}/{}/build/job/{}'.format(os.environ['APPVEYOR_ACCOUNT_NAME'],
                                                                                          os.environ['APPVEYOR_PROJECT_SLUG'],
                                                                                          os.environ['APPVEYOR_JOB_ID'])

            if len(buildLogUrl) > 0:
                print('    Build log URL: ' + buildLogUrl)
                f.write('Build log URL: ' + buildLogUrl + '\n')

            print()
            f.write('\n')

        # Write host info.

        info = self.sysInfo()

        with open(depsInfoFile, 'a') as f:
            for line in info.split('\n'):
                if len(line) > 0:
                    print('    ' + line)
                    f.write(line + '\n')

            print()
            f.write('\n')

    @staticmethod
    def hrSize(size):
        i = int(math.log(size) // math.log(1024))

        if i < 1:
            return '{} B'.format(size)

        units = ['KiB', 'MiB', 'GiB', 'TiB']
        sizeKiB = size / (1024 ** i)

        return '{:.2f} {}'.format(sizeKiB, units[i - 1])

    def printPackageInfo(self, path):
        if os.path.exists(path):
            print('   ',
                  os.path.basename(path),
                  self.hrSize(os.path.getsize(path)))
            print('    sha256sum:', Deploy.sha256sum(path))
        else:
            print('   ',
                  os.path.basename(path),
                  'FAILED')

    def createAppInstaller(self, mutex):
        packagePath = self.createInstaller()

        if not packagePath:
            return

        mutex.acquire()
        print('Created installable package:')
        self.printPackageInfo(self.outPackage)
        mutex.release()

    def package(self):
        mutex = threading.Lock()

        threads = []
        packagingTools = []

        if self.qtIFW != '':
            threads.append(threading.Thread(target=self.createAppInstaller, args=(mutex,)))
            packagingTools += ['Qt Installer Framework']

        if len(packagingTools) > 0:
            print('Detected packaging tools: {}\n'.format(', '.join(packagingTools)))

        for thread in threads:
            thread.start()

        for thread in threads:
            thread.join()
