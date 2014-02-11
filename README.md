snd-bcd2000
===============

Linux driver for the Behringer BCD2000 DJ controller (1397:00bd) [Official Behringer Website](http://www.behringer.com/EN/Products/BCD2000.aspx)

*Please note*: currently, only the MIDI part of the controller is working.
Audio support is work-in-progress.

Dependencies:

* snd_usbmidi_lib
* snd_rawmidi

Usage:

* Either assure that kernel sources are available under:
  <tt>/lib/modules/$(shell uname -r)/build</tt>
  and execute "make" in the snd-bcd2000 folder, _or_
  use DKMS to build the kernel (see [Ubuntu DKMS](https://help.ubuntu.com/community/DKMS)).
* Either directly load the module using ```insmod snd-bcd2000.ko``` or use ```modprobe snd-bcd2000```  
  if the module has been installed into the kernel's module tree.

If it doesn't work:

* Make sure you have all build tools, e.g., gcc and make, and the kernel headers installed.
  For Ubuntu: ```apt-get install build-essential linux-headers```
* If you can build and load the module, but it doesn't work, check the output of ```dmesg```.
  E.g., if there are errors like ```snd_usb_bcd2000: Unknown symbol snd_rawmidi_receive``` you
  have to load the dependencies of our module first. In the above case, execute ```modprobe snd_usbmidi-lib```.
