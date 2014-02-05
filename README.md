snd-usb-bcd2000
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
  and execute "make" in the snd-usb-bcd2000 folder, or
* Copy the files from dist/ into the main folder and use DKMS to build the
  kernel (see [Ubuntu DKMS](https://help.ubuntu.com/community/DKMS)).