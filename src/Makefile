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
	buffers.o \
	controls.o \
	device.o \
	driver.o \
	events.o \
	format.o \
	frame.o \
	ioctl.o \
	list.o \
	mmap.o \
	node.o \
	object.o \
	rbuffer.o \
	utils.o

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) $(SPARSE_VAR) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
