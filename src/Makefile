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
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install
