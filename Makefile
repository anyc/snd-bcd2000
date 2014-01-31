obj-m := snd-usb-bcd2000.o
snd-usb-bcd2000-objs := bcd2000.o
EXTRA_CFLAGS=-I/lib/modules/$(shell uname -r)/build/sound/usb/misc/

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean