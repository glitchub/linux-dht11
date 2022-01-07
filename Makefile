obj-m += dht11.o
ccflags-y += -Wall
ccflags-y += -Werror

.PHONY: modules clean

modules clean:; make -C /lib/modules/$(shell uname -r)/build M=$(PWD) $@
