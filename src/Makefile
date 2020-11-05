MODULE_NAME = akvcam
MODULE_VERSION = 1.1.0
KERNEL_ROOT ?= $(shell realpath /lib/modules/$(shell uname -r))
KERNEL_DIR ?= $(KERNEL_ROOT)/build
COPY = cp -f
MKDIR = mkdir -p
RMDIR = rm -rvf
PWD := $(shell pwd)
DKMS := dkms
DESTDIR :=
PREFIX := $(DESTDIR)/usr/src
INSTALLDIR = $(PREFIX)/$(MODULE_NAME)-$(MODULE_VERSION)

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
	buffer.o \
	buffers.o \
	controls.o \
	device.o \
	driver.o \
	events.o \
	file_read.o \
	format.o \
	frame.o \
	global_deleter.o \
	ioctl.o \
	list.o \
	log.o \
	map.o \
	mmap.o \
	node.o \
	object.o \
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
ifdef USE_DKMS
	$(DKMS) install $(MODULE_NAME)/$(MODULE_VERSION)
endif

uninstall:
ifdef USE_DKMS
	$(DKMS) remove $(MODULE_NAME)/$(MODULE_VERSION) --all
endif
	$(RMDIR) $(INSTALLDIR)
