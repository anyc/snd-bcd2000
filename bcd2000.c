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

#include <linux/version.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bitmap.h>

#include "bcd2000.h"
#include "midi.h"

static struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x1397, 0x00bd) },
	{ },
};

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;

static DEFINE_MUTEX(devices_mutex);
DECLARE_BITMAP(devices_used, SNDRV_CARDS);
static struct usb_driver bcd2000_driver;

#ifdef CONFIG_SND_DEBUG
void bcd2000_dump_buffer(const char *prefix, const char *buf, int len)
{
	print_hex_dump(KERN_DEBUG, prefix,
			DUMP_PREFIX_NONE, 16, 1,
			buf, len, false);
}
#else
void bcd2000_dump_buffer(const char *prefix, const char *buf, int len) {}
#endif

static void bcd2000_disconnect(struct usb_interface *interface)
{
	struct bcd2000 *bcd2k = usb_get_intfdata(interface);
	
	if (!bcd2k)
		return;
	
	mutex_lock(&devices_mutex);
	
	/* make sure that userspace cannot create new requests */
	snd_card_disconnect(bcd2k->card);
	
	bcd2000_free_midi(bcd2k);
	
	if (bcd2k->intf) {
		usb_set_intfdata(bcd2k->intf, NULL);
		bcd2k->intf = NULL;
	}
	
	clear_bit(bcd2k->card_index, devices_used);
	
	snd_card_free_when_closed(bcd2k->card);
	
	mutex_unlock(&devices_mutex);
}

static int bcd2000_probe(struct usb_interface *interface,
				const struct usb_device_id *usb_id)
{
	struct snd_card *card;
	struct bcd2000 *bcd2k;
	unsigned int card_index;
	char usb_path[32];
	int err;

	mutex_lock(&devices_mutex);

	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index)
		if (!test_bit(card_index, devices_used))
			break;

	if (card_index >= SNDRV_CARDS) {
		mutex_unlock(&devices_mutex);
		return -ENOENT;
	}

	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
	err = snd_card_create(index[card_index], id[card_index], THIS_MODULE,
			sizeof(*bcd2k), &card);
	#else
	err = snd_card_new(&interface->dev, index[card_index], id[card_index],
			THIS_MODULE, sizeof(*bcd2k), &card);
	#endif
	if (err < 0) {
		mutex_unlock(&devices_mutex);
		return err;
	}

	bcd2k = card->private_data;
	bcd2k->dev = interface_to_usbdev(interface);
	bcd2k->card = card;
	bcd2k->card_index = card_index;
	bcd2k->intf = interface;

	snd_card_set_dev(card, &interface->dev);

	strncpy(card->driver, "snd-bcd2000", sizeof(card->driver));
	strncpy(card->shortname, "BCD2000", sizeof(card->shortname));
	usb_make_path(bcd2k->dev, usb_path, sizeof(usb_path));
	snprintf(bcd2k->card->longname, sizeof(bcd2k->card->longname),
		    "Behringer BCD2000 at %s",
			usb_path);

	err = bcd2000_init_midi(bcd2k);
	if (err < 0)
		goto probe_error;

	err = snd_card_register(card);
	if (err < 0)
		goto probe_error;

	usb_set_intfdata(interface, bcd2k);
	set_bit(card_index, devices_used);

	mutex_unlock(&devices_mutex);
	return 0;

probe_error:
	dev_info(&bcd2k->dev->dev, PREFIX "error during probing");

	bcd2000_disconnect(interface);
	mutex_unlock(&devices_mutex);

	return err;
}

static struct usb_driver bcd2000_driver = {
	.name =		"snd-bcd2000",
	.probe =	bcd2000_probe,
	.disconnect =	bcd2000_disconnect,
	.id_table =	id_table,
};

module_usb_driver(bcd2000_driver);

MODULE_DEVICE_TABLE(usb, id_table);
MODULE_AUTHOR("Mario Kicherer, dev@kicherer.org");
MODULE_DESCRIPTION("Behringer BCD2000 driver");
MODULE_LICENSE("GPL");
