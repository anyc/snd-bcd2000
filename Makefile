obj-m := snd-usb-bcd2000.o
snd-usb-bcd2000-objs := bcd2000.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
