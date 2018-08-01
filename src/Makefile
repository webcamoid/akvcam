KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

obj-m += akvcam.o
akvcam-objs := \
	controls.o \
	device.o \
	driver.o \
	fops.o \
	format.o \
	ioctl_ops.o \
	list.o \
	module.o \
	mutex.o \
	object.o \
	queue.o \
	utils.o

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
