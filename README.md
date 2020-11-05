# akvcam, Virtual camera driver for Linux  #

akvcam is a fully compliant V4L2 virtual camera driver for Linux.

## Features ##

* Flexible configuration with a simple INI file like.
* Support for map, user pointer, and read/write modes.
* Can cat and echo to the device.
* Supports emulated camera controls in capture devices (brightness, contrast, saturation, etc.).
* Configurable default picture in case no input signal available.
* The devices can't be rejected by programs that rejects M2M devices.
* Fully compliant with V4L2 standard.
* Support for LTS kernels.

## Build and Install ##

Visit the [wiki](https://github.com/webcamoid/akvcam/wiki) for a comprehensive compile and install instructions.

## Status ##

[![Build Status](https://travis-ci.org/webcamoid/akvcam.svg?branch=master)](https://travis-ci.org/webcamoid/akvcam)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/eaeeaacb491c498bbffbe2087bc2d4dd)](https://www.codacy.com/gh/webcamoid/akvcam/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=webcamoid/akvcam&amp;utm_campaign=Badge_Grade)

## Reporting Bugs ##

Report all issues in the [issues tracker](http://github.com/webcamoid/akvcam/issues).
