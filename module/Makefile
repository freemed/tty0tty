# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSCULL_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS) -I..

ifneq ($(KERNELRELEASE),)
# call from kernel build system

#obj-m	:= tiny_tty.o tiny_serial.o tty0tty.o 
obj-m	:= tty0tty.o 

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)
KBASE     := /lib/modules/$(KVER)
KSRC      ?= $(PWD)
KBUILD    := $(KSRC)
MOD_DIR   := $(KBASE)/kernel

modules: default

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions modules.order Module.symvers

depend .depend dep:
	$(CC) $(CFLAGS) -M *.c > .depend

install: modules
	@( [ `id -u` -ne 1 ] || { echo "Must be root to install modules"; exit 1; } )
	rm -f $(MOD_DIR)/extra/tty0tty.ko
	$(MAKE) -C $(KERNELDIR) M=$(PWD) O=$(KBASE) modules_install
	depmod -a

ifeq (.depend,$(wildcard .depend))
include .depend
endif

