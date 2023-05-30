MODULE_NAME = akvcam
MODULE_VERSION = 1.2.4
KERNEL_ROOT ?= $(shell realpath /lib/modules/$(shell uname -r))
KERNEL_DIR ?= $(KERNEL_ROOT)/build
COPY = cp -f
MKDIR = mkdir -p
RMDIR = rm -rvf
PWD := $(shell pwd)
DKMS := dkms
DESTDIR :=
PREFIX := $(DESTDIR)/usr/src
INSTALLDIR := $(PREFIX)/$(MODULE_NAME)-$(MODULE_VERSION)

HAVE_SPARSE := $(shell which sparse 2>/dev/null)
SPARSE_MODE ?= 2

ifdef USE_SPARSE
ifdef HAVE_SPARSE
SPARSE_VAR = C=$(SPARSE_MODE)
endif
endif

obj-m += $(MODULE_NAME).o
akvcam-objs := \
	module.o \
	attributes.o \
	buffers.o \
	controls.o \
	device.o \
	driver.o \
	file_read.o \
        format.o \
	frame.o \
        frame_filter.o \
        ioctl.o \
	list.o \
	log.o \
	map.o \
	rbuffer.o \
	settings.o \
	utils.o

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) $(SPARSE_VAR) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

install: uninstall
	$(MKDIR) $(INSTALLDIR)
	$(COPY) Makefile $(INSTALLDIR)
	$(COPY) dkms.conf $(INSTALLDIR)
	$(COPY) *.h $(INSTALLDIR)
	$(COPY) *.c $(INSTALLDIR)

dkms_install: | dkms_uninstall install
	$(DKMS) install $(MODULE_NAME)/$(MODULE_VERSION)

uninstall:
	$(RMDIR) $(INSTALLDIR)

dkms_uninstall:
	- $(DKMS) remove $(MODULE_NAME)/$(MODULE_VERSION) --all
	$(RMDIR) $(INSTALLDIR)
