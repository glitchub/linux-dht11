obj-m += dht11.o
ccflags-y += -Wall
ccflags-y += -Werror

all:;make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:;make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
