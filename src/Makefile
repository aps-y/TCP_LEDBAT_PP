obj-m := tcp_ledbat_pp.o
IDIR= /lib/modules/$(shell uname -r)/kernel/net/ipv4/
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	install -v -m 644 tcp_ledbat_pp.ko $(IDIR)
	depmod
	modprobe tcp_ledbat_pp
	sysctl -w net.ipv4.tcp_allowed_congestion_control="cubic reno ledbat_pp"

clean:
	rm -rf Module.markers modules.order Module.symvers tcp_ledbat_pp.ko tcp_ledbat_pp.mod.c tcp_ledbat_pp.mod.o tcp_ledbat_pp.o
