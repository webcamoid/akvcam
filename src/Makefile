KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

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
	mutex.o \
	node.o \
	object.o \
	rbuffer.o \
	utils.o

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
