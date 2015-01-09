obj-m := snd-bcd2000.o
snd-bcd2000-objs := audio.o bcd2000.o midi.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
