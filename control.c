/*
 * Behringer BCD2000 driver
 *
 *   Copyright (C) 2014 Mario Kicherer (dev@kicherer.org)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/interrupt.h>
#include <sound/control.h>
#include <sound/tlv.h>

#include "bcd2000.h"
#include "control.h"
#include "midi.h"

static const char * const phono_mic_sw_texts[2] = { "Phono A", "Mic" };

/* 
 * switch between Phono A and Mic input using a MIDI program change command
 *
 * The manual specifies "c0 [00|01]" but the windows driver sends
 * "09 01 [00|01]", we follow the manual here.
 */
static void bcd2000_control_phono_mic_sw_update(struct bcd2000_control *ctrl)
{
	int actual_length, ret;
	char buffer[MIDI_URB_BUFSIZE] = MIDI_CMD_PREFIX_INIT;

	buffer[2] = 2;
	buffer[3] = 0xC0;
	buffer[4] = ctrl->phono_mic_switch;

	ret = usb_interrupt_msg(ctrl->bcd2k->dev, usb_sndintpipe(ctrl->bcd2k->dev, 0x1),
							buffer, MIDI_URB_BUFSIZE, &actual_length, 100);
	if (ret)
		dev_err(&ctrl->bcd2k->dev->dev, PREFIX "usb_interrupt_msg failed\n");
}

static int bcd2000_control_phono_mic_sw_info(struct snd_kcontrol *kcontrol,
										  struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(uinfo, 1, 2, phono_mic_sw_texts);
}

static int bcd2000_control_phono_mic_sw_put(struct snd_kcontrol *kcontrol,
										 struct snd_ctl_elem_value *ucontrol)
{
	struct bcd2000_control *ctrl = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (ctrl->phono_mic_switch != ucontrol->value.enumerated.item[0]) {
		ctrl->phono_mic_switch = ucontrol->value.enumerated.item[0];

		bcd2000_control_phono_mic_sw_update(ctrl);

		changed = 1;
	}

	return changed;
}

static int bcd2000_control_phono_mic_sw_get(struct snd_kcontrol *kcontrol,
										 struct snd_ctl_elem_value *ucontrol)
{
	struct bcd2000_control *ctrl = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = ctrl->phono_mic_switch;

	return 0;
}

static struct snd_kcontrol_new elements[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Phono A / Mic Capture Switch",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = bcd2000_control_phono_mic_sw_info,
		.get = bcd2000_control_phono_mic_sw_get,
		.put = bcd2000_control_phono_mic_sw_put
	},
	{}
};

int bcd2000_init_control(struct bcd2000 *bcd2k)
{
	int i, ret;

	bcd2k->control.bcd2k = bcd2k;

	i = 0;
	while (elements[i].name) {
		ret = snd_ctl_add(bcd2k->card, snd_ctl_new1(&elements[i],
													&bcd2k->control));
		if (ret < 0) {
			dev_err(&bcd2k->dev->dev, "cannot add control\n");
			return ret;
		}
		i++;
	}

	return 0;
}

void bcd2000_free_control(struct bcd2000 *bcd2k)
{
}