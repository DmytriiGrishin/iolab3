# If called directly from the command line, invoke the kernel build system.
ifeq ($(KERNELRELEASE),)
	KERNEL_SOURCE := /usr/src/linux-headers-4.15.0-34-generic
	PWD := $(shell pwd)
default: all

MODULE  = virt1.ko
VIRTDEV = virt0
DEVICE  = wlp2s0

all:
	make -C /lib/modules/$(shell uname -r)/build M="$(PWD)" modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M="$(PWD)" clean
do:
	sudo insmod $(MODULE) link=$(DEVICE)
od:
	sudo rmmod $(MODULE)
if:
	ip -s a show dev $(VIRTDEV)
	cat /proc/net/dev | grep -e $(VIRTDEV) -e $(DEVICE)

redo:
	make od && make all && make do
	
else
	obj-m := virt1.o
endif
