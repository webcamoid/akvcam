KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
HAVE_SPARSE := $(shell which sparse 2>/dev/null)
SPARSE_MODE ?= 2

ifdef USE_SPARSE
ifdef HAVE_SPARSE
SPARSE_VAR = C=$(SPARSE_MODE)
endif
endif

obj-m += akvcam.o
akvcam-objs := \
	module.o \
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

install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
