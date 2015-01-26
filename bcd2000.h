#ifndef BCD2000_H
#define BCD2000_H

#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <sound/core.h>
#include <sound/initval.h>

#define DEVICENAME "BCD2000"
#define PREFIX "snd-bcd2000: "

#include "audio.h"
#include "control.h"
#include "midi.h"

struct bcd2000 {
	struct usb_device *dev;
	struct snd_card *card;
	struct usb_interface *intf;
	int card_index;

	struct bcd2000_midi midi;
	struct bcd2000_pcm pcm;
	struct bcd2000_control control;
};

void bcd2000_dump_buffer(const char *prefix, const char *buf, int len);

#endif