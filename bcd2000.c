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
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifdef CONFIG_USB_DEBUG
#define DEBUG	1
#else
#define DEBUG	0
#endif
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/rawmidi.h>
#include "../usbaudio.h"
#include "../midi.h"

#define DEVICE_NAME "Behringer BCD2000"
#define DEVICE_SHORTNAME "bcd2000"

#define PREFIX DEVICE_SHORTNAME ": "
#define BUFSIZE 64

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x1397, 0x00bd) },
	{ },
};
MODULE_DEVICE_TABLE (usb, id_table);
MODULE_AUTHOR("Mario Kicherer, dev@kicherer.org");
MODULE_DESCRIPTION("Behringer BCD2000 Driver");
MODULE_LICENSE("GPL");

static int debug = 0;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "enable debug output (default: 0)");

static unsigned char set_led_prefix[] = {0x03, 0x00};

static unsigned char bcd2000_init_sequence[] = {
	0x07, 0x00, 0x00, 0x00, 0x78, 0x48, 0x1c, 0x81, 0xc4, 0x00, 0x00, 0x00, 0x5e, 0x53, 0x4a, 0xf7,
	0x18, 0xfa, 0x11, 0xff, 0x6c, 0xf3, 0x90, 0xff, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x18, 0xfa, 0x11, 0xff, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0x34, 0x4a, 0xf7,
	0x18, 0xfa, 0x11, 0xff};

struct bcd2000 {
	struct usb_device *dev;
	struct snd_card *card;
	struct usb_interface *intf;
	int card_index;
	struct snd_pcm *pcm;
	struct list_head midi_list;
	spinlock_t lock;
	struct mutex mutex;
	
	int midi_out_active;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *midi_receive_substream;
	struct snd_rawmidi_substream *midi_out_substream;
	
	unsigned char midi_in_buf[BUFSIZE];
	unsigned char midi_out_buf[BUFSIZE];
	
	unsigned char midi_cmd_buf[3];
	unsigned char midi_cmd_offset;
	
	struct urb * midi_out_urb;
	struct urb * midi_in_urb;
};

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

static DEFINE_MUTEX(devices_mutex);
static unsigned int devices_used;
static struct usb_driver bcd2000_driver;






static int bcd2000_midi_input_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int bcd2000_midi_input_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void bcd2000_midi_input_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct bcd2000 *bcd2k = substream->rmidi->private_data;
	
	if (!bcd2k)
		return;
	
	bcd2k->midi_receive_substream = up ? substream : NULL;
}

static void bcd2000_midi_handle_input(struct bcd2000 *bcd2k,
							const unsigned char *buf, int len)
{
	unsigned char length, cmd, buf_offset, tocopy;
	int ret;
	
	if (!bcd2k->midi_receive_substream)
		return;
	
	if (DEBUG || debug) {
		printk(KERN_DEBUG PREFIX "received (%d bytes): ", len);
		for (ret=0; ret < len; ret++) {
			printk("%x ", buf[ret]);
		}
		printk("\n");
	}
	
	if (len < 2)
		return;
	
	/*
	 * The BCD2000 sends MIDI commands with 2 or 3 bytes length. In case of
	 * 2 bytes, the command is the same as before.
	 *
	 * Packet structure: mm nn oo (pp)
	 *	mm: payload length
	 *	nn: MIDI command or note
	 *	oo: note or velocity
	 *	pp: velocity
	 */
	
	length = buf[0];
	
	/* ignore packets without payload, should not happen */
	if (length == 0)
		return;
	
	/*
	 * The MIDI packets the BCD2000 sends can be arbitrarily truncated or
	 * concatenated. Therefore, this loop accumulates the bytes from the
	 * input buffer until a full valid MIDI packet is in the MIDI command
	 * buffer "midi_cmd_buf".
	 */
	
	buf_offset = 1;
	while (buf_offset <= length) {
		cmd = buf[buf_offset];
		
		if (bcd2k->midi_cmd_offset == 0 && cmd != 0x90 && cmd != 0xb0) {
			/*
			 * this is a 2-byte midi packet -> reuse command byte from
			 * last midi packet
			 */
			bcd2k->midi_cmd_offset = 1;
		}
		
		/* determine the number of bytes we want to copy this time */
		tocopy = min( 3 - bcd2k->midi_cmd_offset, length - (buf_offset - 1));
		
		if (tocopy > 0) {
			memcpy(&bcd2k->midi_cmd_buf[bcd2k->midi_cmd_offset], &buf[buf_offset], tocopy);
		} else
			printk(KERN_ERR PREFIX "error parsing\n");
		
		bcd2k->midi_cmd_offset += tocopy;
		buf_offset += tocopy;
		
		/* is our MIDI packet complete? */
		if (bcd2k->midi_cmd_offset == 3) {
			if (DEBUG || debug) {
				printk(KERN_DEBUG PREFIX "send midi packet (%d bytes): ",
						bcd2k->midi_cmd_offset);
				for (ret=0;ret< bcd2k->midi_cmd_offset;ret++) {
					printk("%x ", bcd2k->midi_cmd_buf[ret]);
				}
				printk("\n");
			}
			
			/* send MIDI packet */
			snd_rawmidi_receive(bcd2k->midi_receive_substream, bcd2k->midi_cmd_buf, 3);
			
			bcd2k->midi_cmd_offset = 0;
		}
	}
}

static int bcd2000_midi_output_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int bcd2000_midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct bcd2000 * bcd2k = substream->rmidi->private_data;
	if (bcd2k->midi_out_active) {
		usb_free_urb(bcd2k->midi_out_urb);
		bcd2k->midi_out_active = 0;
	}
	return 0;
}

static void bcd2000_midi_send(struct bcd2000 *bcd2k,
							 struct snd_rawmidi_substream *substream)
{
	int len, ret;
	
	/* copy the "set LED" command bytes */
	memcpy(bcd2k->midi_out_buf, set_led_prefix, sizeof(set_led_prefix));
	
	/* get MIDI packet */
	len = snd_rawmidi_transmit(substream, bcd2k->midi_out_buf + 3, BUFSIZE - 3);
	
	if (len <= 0)
		return;
	
	/* set payload length */
	bcd2k->midi_out_buf[2] = len;
	bcd2k->midi_out_urb->transfer_buffer_length = BUFSIZE;
	
	if (DEBUG || debug) {
		printk(KERN_DEBUG PREFIX "sending: ");
		for (ret=0;ret< 3 + len;ret++) {
			printk("%x ", bcd2k->midi_out_buf[ret]);
		}
		printk("\n");
	}
	
	/* send packet to the BCD2000 */
	ret = usb_submit_urb(bcd2k->midi_out_urb, GFP_ATOMIC);
	if (ret < 0) {
		printk(KERN_ERR PREFIX
			   "bcd2000_midi_send(%p): usb_submit_urb() failed,"
			   "ret=%d, len=%d: ", substream, ret, len);
		switch (ret) {
			case -ENOMEM: printk("ENOMEM\n"); break;
			case -ENODEV: printk("ENODEV\n"); break;
			case -ENXIO: printk("ENXIO\n"); break;
			case -EINVAL: printk("EINVAL\n"); break;
			case -EAGAIN: printk("EAGAIN\n"); break;
			case -EFBIG: printk("EFBIG\n"); break;
			case -EPIPE: printk("EPIPE\n"); break;
			case -EMSGSIZE: printk("EMSGSIZE\n"); break;
			default: printk("\n");
		}
	} else {
		bcd2k->midi_out_active = 1;
	}
}

static void bcd2000_midi_output_done(struct urb* urb)
{
	struct bcd2000 * bcd2k = urb->context;
	
	bcd2k->midi_out_active = 0;
	if (urb->status != 0) {
		printk(KERN_ERR PREFIX "urb status: %d\n", urb->status);
		return;
	}
	
	if (!bcd2k->midi_out_substream)
		return;
	
	bcd2000_midi_send(bcd2k, bcd2k->midi_out_substream);
}

static void bcd2000_command_reply_dispatch(struct urb* urb)
{
	int ret;
	struct device *dev = &urb->dev->dev;
	struct bcd2000 *bcd2k = urb->context;
	
	if (urb->status || !bcd2k) {
		dev_warn(dev, "received urb->status = %i\n", urb->status);
		return;
	}
	
	if (urb->actual_length > 0)
		bcd2000_midi_handle_input(bcd2k, urb->transfer_buffer, urb->actual_length);
	
	bcd2k->midi_in_urb->actual_length = 0;
	ret = usb_submit_urb(bcd2k->midi_in_urb, GFP_ATOMIC);
	if (ret < 0)
		dev_err(dev, "unable to submit urb. OOM!?\n");
}

static void bcd2000_midi_output_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct bcd2000 *bcd2k = substream->rmidi->private_data;
	
	if (up) {
		bcd2k->midi_out_substream = substream;
		if (!bcd2k->midi_out_active)
			bcd2000_midi_send(bcd2k, substream);
	} else {
		bcd2k->midi_out_substream = NULL;
	}
}

static struct snd_rawmidi_ops bcd2000_midi_output =
{
	.open =    bcd2000_midi_output_open,
	.close =   bcd2000_midi_output_close,
	.trigger = bcd2000_midi_output_trigger,
};

static struct snd_rawmidi_ops bcd2000_midi_input =
{
	.open =    bcd2000_midi_input_open,
	.close =   bcd2000_midi_input_close,
	.trigger = bcd2000_midi_input_trigger,
};

static void bcd2000_init_device(struct bcd2000 * bcd2k) {
	int ret;
	
	/* copy init sequence into buffer */
	memcpy(bcd2k->midi_out_buf, bcd2000_init_sequence, 52);
	bcd2k->midi_out_urb->transfer_buffer_length = 52;
	
	/* submit sequence */
	ret = usb_submit_urb(bcd2k->midi_out_urb, GFP_ATOMIC);
	if (ret < 0) {
		printk(KERN_ERR PREFIX
				"bcd2000_init_device: usb_submit_urb() failed,"
				"ret=%d: ", ret);
		switch (ret) {
			case -ENOMEM: printk("ENOMEM\n"); break;
			case -ENODEV: printk("ENODEV\n"); break;
			case -ENXIO: printk("ENXIO\n"); break;
			case -EINVAL: printk("EINVAL\n"); break;
			case -EAGAIN: printk("EAGAIN\n"); break;
			case -EFBIG: printk("EFBIG\n"); break;
			case -EPIPE: printk("EPIPE\n"); break;
			case -EMSGSIZE: printk("EMSGSIZE\n"); break;
			default: printk("\n");
		}
	} else {
		bcd2k->midi_out_active = 1;
	}
	
	/* send empty packet to enable button and controller events */
	bcd2k->midi_in_urb->actual_length = 0;
	ret = usb_submit_urb(bcd2k->midi_in_urb, GFP_ATOMIC);
}

static void bcd2000_init_midi(struct bcd2000 * bcd2k) {
	int ret;
	struct snd_rawmidi *rmidi;
	
	ret = snd_rawmidi_new(bcd2k->card, bcd2k->card->shortname, 0,
					1, /* output */
					1, /* input */
					&rmidi);

	if (ret < 0)
		return;

	strlcpy(rmidi->name, bcd2k->card->shortname, sizeof(rmidi->name));

	rmidi->info_flags = SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = bcd2k;

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					&bcd2000_midi_output);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
					&bcd2000_midi_input);

	bcd2k->rmidi = rmidi;
	
	bcd2k->midi_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	bcd2k->midi_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	
	usb_fill_int_urb(bcd2k->midi_in_urb, bcd2k->dev,
				usb_rcvbulkpipe(bcd2k->dev, 0x81),
				bcd2k->midi_in_buf, BUFSIZE,
				bcd2000_command_reply_dispatch, bcd2k, 1);
	
	usb_fill_int_urb(bcd2k->midi_out_urb, bcd2k->dev,
				usb_sndbulkpipe(bcd2k->dev, 0x1),
				bcd2k->midi_out_buf, BUFSIZE,
				bcd2000_midi_output_done, bcd2k, 1);
	
	bcd2000_init_device(bcd2k);
}

static void bcd2000_close_midi(struct bcd2000 * bcd2k) {
	usb_free_urb(bcd2k->midi_out_urb);
	usb_free_urb(bcd2k->midi_in_urb);
}





/*
 * general part
 */

static void bcd2000_free_usb_related_resources(struct bcd2000 *bcd2k,
							    struct usb_interface *interface)
{
	unsigned int i;
	struct usb_interface *intf;
	
// 	mutex_lock(&ua->mutex);
// 	free_stream_urbs(&ua->capture);
// 	free_stream_urbs(&ua->playback);
// 	mutex_unlock(&ua->mutex);
// 	free_stream_buffers(ua, &ua->capture);
// 	free_stream_buffers(ua, &ua->playback);
	
	for (i = 0; i < 1; ++i) {
		mutex_lock(&bcd2k->mutex);
		intf = bcd2k->intf;
		bcd2k->intf = NULL;
		mutex_unlock(&bcd2k->mutex);
		if (intf) {
			usb_set_intfdata(intf, NULL);
			if (intf != interface)
				usb_driver_release_interface(&bcd2000_driver,
									    intf);
		}
	}
}

static void bcd2000_card_free(struct snd_card *card)
{
// 	struct bcd2000 *bcd2k = card->private_data;
// 	mutex_destroy(&ua->mutex);
}

static int bcd2000_probe(struct usb_interface *interface, const struct usb_device_id *usb_id)
{
	struct snd_card *card;
	struct bcd2000 * bcd2k;
	unsigned int card_index;
	char usb_path[32];
	int err;
	
	
	mutex_lock(&devices_mutex);
	
	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index)
		if (enable[card_index] && !(devices_used & (1 << card_index)))
			break;
	if (card_index >= SNDRV_CARDS) {
		mutex_unlock(&devices_mutex);
		return -ENOENT;
	}
	err = snd_card_create(index[card_index], id[card_index], THIS_MODULE,
					sizeof(*bcd2k), &card);
	if (err < 0) {
		mutex_unlock(&devices_mutex);
		return err;
	}
	card->private_free = bcd2000_card_free;
	bcd2k = card->private_data;
	bcd2k->dev = interface_to_usbdev(interface);
	bcd2k->card = card;
	bcd2k->card_index = card_index;
	INIT_LIST_HEAD(&bcd2k->midi_list);
	spin_lock_init(&bcd2k->lock);
	mutex_init(&bcd2k->mutex);
	
// 	INIT_LIST_HEAD(&bcd2k->ready_playback_urbs);
// 	tasklet_init(&bcd2k->playback_tasklet,
// 				playback_tasklet, (unsigned long)bcd2k);
// 	init_waitqueue_head(&bcd2k->alsa_capture_wait);
// 	init_waitqueue_head(&bcd2k->rate_feedback_wait);
// 	init_waitqueue_head(&bcd2k->alsa_playback_wait);
	
	bcd2k->intf = interface;
	
	snd_card_set_dev(card, &interface->dev);
	
	strcpy(card->driver, DEVICE_NAME);
	strcpy(card->shortname, DEVICE_SHORTNAME);
	usb_make_path(bcd2k->dev, usb_path, sizeof(usb_path));
	snprintf(bcd2k->card->longname, sizeof(bcd2k->card->longname),
		    DEVICE_NAME ", at %s, %s speed",
				usb_path,
				bcd2k->dev->speed == USB_SPEED_HIGH ? "high" : "full");
	
	printk(KERN_INFO PREFIX "%s", bcd2k->card->longname);
	
// 	err = alloc_stream_buffers(bcd2k, &bcd2k->capture);
// 	if (err < 0)
// 		goto probe_error;
// 	err = alloc_stream_buffers(bcd2k, &bcd2k->playback);
// 	if (err < 0)
// 		goto probe_error;
// 	
// 	err = alloc_stream_urbs(bcd2k, &bcd2k->capture, capture_urb_complete);
// 	if (err < 0)
// 		goto probe_error;
// 	err = alloc_stream_urbs(bcd2k, &bcd2k->playback, playback_urb_complete);
// 	if (err < 0)
// 		goto probe_error;
// 	
// 	err = snd_pcm_new(card, "bcd2000", 0, 1, 1, &bcd2k->pcm);
// 	if (err < 0)
// 		goto probe_error;
// 	bcd2k->pcm->private_data = bcd2k;
// 	strcpy(bcd2k->pcm->name, "bcd2000");
// 	snd_pcm_set_ops(bcd2k->pcm, SNDRV_PCM_STREAM_PLAYBACK, &playback_pcm_ops);
// 	snd_pcm_set_ops(bcd2k->pcm, SNDRV_PCM_STREAM_CAPTURE, &capture_pcm_ops);
	
// 	err = snd_usbmidi_create(card, bcd2k->intf,
// 						&bcd2k->midi_list, &midi_quirk);
// 	if (err < 0)
// 		goto probe_error;
	
	
	
	bcd2000_init_midi(bcd2k);
	
	
	err = snd_card_register(card);
	if (err < 0)
		goto probe_error;
	
	usb_set_intfdata(interface, bcd2k);
	devices_used |= 1 << card_index;
	
	mutex_unlock(&devices_mutex);
	return 0;
	
probe_error:
	bcd2000_free_usb_related_resources(bcd2k, interface);
	snd_card_free(card);
	mutex_unlock(&devices_mutex);
	return err;
}

static void bcd2000_disconnect(struct usb_interface *interface)
{
	struct bcd2000 *bcd2k = usb_get_intfdata(interface);
	struct list_head *midi;
	
	if (!bcd2k)
		return;
	
	bcd2000_close_midi(bcd2k);
	
	mutex_lock(&devices_mutex);
	
// 	set_bit(DISCONNECTED, &bcd2k->states);
// 	wake_up(&bcd2k->rate_feedback_wait);
	
	/* make sure that userspace cannot create new requests */
	snd_card_disconnect(bcd2k->card);
	
	/* make sure that there are no pending USB requests */
	list_for_each(midi, &bcd2k->midi_list)
		snd_usbmidi_disconnect(midi);
	
// 	abort_alsa_playback(bcd2k);
// 	abort_alsa_capture(bcd2k);
// 	mutex_lock(&bcd2k->mutex);
// 	stop_usb_playback(bcd2k);
// 	stop_usb_capture(bcd2k);
// 	mutex_unlock(&bcd2k->mutex);
	
	bcd2000_free_usb_related_resources(bcd2k, interface);
	
	devices_used &= ~(1 << bcd2k->card_index);
	
	snd_card_free_when_closed(bcd2k->card);
	
	mutex_unlock(&devices_mutex);
}

static struct usb_driver bcd2000_driver = {
	.name =	DEVICE_SHORTNAME,
	.probe =	bcd2000_probe,
	.disconnect =	bcd2000_disconnect,
	.id_table =	id_table,
};

static int __init bcd2000_init(void)
{
	int retval = 0;
	
	retval = usb_register(&bcd2000_driver);
	if (retval)
		printk(KERN_INFO "usb_register failed. Error number %d", retval);
	return retval;
}

static void __exit bcd2000_exit(void)
{
	usb_deregister(&bcd2000_driver);
}

module_init (bcd2000_init);
module_exit (bcd2000_exit);
