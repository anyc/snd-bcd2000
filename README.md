snd-bcd2000
===========

Linux driver for the Behringer BCD2000 DJ controller (1397:00bd) [Official Behringer Website](http://www.behringer.com/EN/Products/BCD2000.aspx)

Dependencies:

* snd_usbmidi_lib
* snd_rawmidi

Usage:
------

* Either assure that kernel sources are available under:
  <tt>/lib/modules/$(shell uname -r)/build</tt>
  and execute "make" in the snd-bcd2000 folder, _or_
  use DKMS to build the kernel (see [Ubuntu DKMS](https://help.ubuntu.com/community/DKMS)).
* Either directly load the module using ```insmod snd-bcd2000.ko``` or use ```modprobe snd-bcd2000```  
  if the module has been installed into the kernel's module tree.

If it does not work:

* Make sure you have all build tools, e.g., gcc and make, and the kernel headers installed.
  For Ubuntu: ```apt-get install build-essential linux-headers```
* If you can build and load the module, but it doesn't work, check the output of ```dmesg```.
  E.g., if there are errors like ```snd_usb_bcd2000: Unknown symbol snd_rawmidi_receive``` you
  have to load the dependencies of our module first. In the above case, execute ```modprobe snd_usbmidi-lib```.

Troubleshooting
---------------

If audio is enabled, device initialization sometimes fails with the following error in the kernel log:

```
snd-bcd2000: usb_submit_urb failed: -28
snd-bcd2000: could not start pcm stream
```

This is likely an issue with bandwidth reservation on the USB bus. As a workaroud, the driver now disables
the capturing from the device by default as most people just use it for playback. If you see this error or
you need to capture audio from the device, try the different USB ports of your PC - especially switch
between USB 2 and 3 ports.

There is also a repository:

([https://github.com/CodeKill3r/BCD2000HIDplus](https://github.com/CodeKill3r/BCD2000HIDplus))

with an alternative firmware for this device which claims to provide a standard USB audio interface so you
would not need a special driver. However, to use this firmware you need special hardware tools.
