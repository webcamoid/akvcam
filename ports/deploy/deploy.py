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

from WebcamoidDeployTools import DTUtils


if __name__ =='__main__':
    system = DTUtils.Utils().system

    while True:
        deploy = __import__('deploy_' + system).Deploy()

        try:
            deploy = __import__('deploy_' + system).Deploy()
        except:
            print('No valid deploy script found.')

            exit()

        if system == deploy.targetSystem:
            deploy.run()

            exit()

        system = deploy.targetSystem
