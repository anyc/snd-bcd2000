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

#include "bcd2000.h"
#include "midi.h"

/*
 * For details regarding the usable MIDI commands, please see the official
 * manual: http://www.behringer.com/EN/Products/BCD2000.aspx#softwareContent
 */

static unsigned char bcd2000_init_sequence[] = {
	0x07, 0x00, 0x00, 0x00, 0x78, 0x48, 0x1c, 0x81,
	0xc4, 0x00, 0x00, 0x00, 0x5e, 0x53, 0x4a, 0xf7,
	0x18, 0xfa, 0x11, 0xff, 0x6c, 0xf3, 0x90, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x18, 0xfa, 0x11, 0xff, 0x14, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xf2, 0x34, 0x4a, 0xf7,
	0x18, 0xfa, 0x11, 0xff
};

static unsigned char device_cmd_prefix[] = MIDI_CMD_PREFIX_INIT;

static int bcd2000_midi_input_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int bcd2000_midi_input_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

/* (de)register midi substream from client */
static void bcd2000_midi_input_trigger(struct snd_rawmidi_substream *substream,
						int up)
{
	struct bcd2000 *bcd2k = substream->rmidi->private_data;
	bcd2k->midi.receive_substream = up ? substream : NULL;
}

static void bcd2000_midi_handle_input(struct bcd2000 *bcd2k,
				const unsigned char *buf, unsigned int buf_len)
{
	unsigned int payload_length, tocopy;
	struct snd_rawmidi_substream *receive_substream;

	receive_substream = ACCESS_ONCE(bcd2k->midi.receive_substream);
	if (!receive_substream)
		return;

	bcd2000_dump_buffer(PREFIX "received from device: ", buf, buf_len);

	if (buf_len < 2)
		return;

	payload_length = buf[0];

	/* ignore packets without payload */
	if (payload_length == 0)
		return;

	tocopy = min(payload_length, buf_len-1);

	bcd2000_dump_buffer(PREFIX "sending to userspace: ",
					&buf[1], tocopy);

	snd_rawmidi_receive(receive_substream,
					&buf[1], tocopy);
}

static void bcd2000_midi_send(struct bcd2000 *bcd2k)
{
	int len, ret;
	struct snd_rawmidi_substream *send_substream;

	BUILD_BUG_ON(sizeof(device_cmd_prefix) >= MIDI_URB_BUFSIZE);

	send_substream = ACCESS_ONCE(bcd2k->midi.send_substream);
	if (!send_substream)
		return;

	/* copy command prefix bytes */
	memcpy(bcd2k->midi.out_buffer, device_cmd_prefix,
			sizeof(device_cmd_prefix));

	/*
	 * get MIDI packet and leave space for command prefix
	 * and payload length
	 */
	len = snd_rawmidi_transmit(send_substream,
							   bcd2k->midi.out_buffer + 3, MIDI_URB_BUFSIZE - 3);

	if (len < 0)
		dev_err(&bcd2k->dev->dev, "%s: snd_rawmidi_transmit error %d\n",
				__func__, len);

	if (len <= 0)
		return;

	/* set payload length */
	bcd2k->midi.out_buffer[2] = len;
	bcd2k->midi.out_urb->transfer_buffer_length = MIDI_URB_BUFSIZE;

	bcd2000_dump_buffer(PREFIX "sending to device: ",
						bcd2k->midi.out_buffer, len+3);

	/* send packet to the BCD2000 */
	ret = usb_submit_urb(bcd2k->midi.out_urb, GFP_ATOMIC);
	if (ret < 0)
		dev_err(&bcd2k->dev->dev, PREFIX
			"%s (%p): usb_submit_urb() failed, ret=%d, len=%d\n",
			__func__, send_substream, ret, len);
	else
		bcd2k->midi.out_active = 1;
}

static int bcd2000_midi_output_open(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static int bcd2000_midi_output_close(struct snd_rawmidi_substream *substream)
{
	struct bcd2000 *bcd2k = substream->rmidi->private_data;

	if (bcd2k->midi.out_active) {
		usb_kill_urb(bcd2k->midi.out_urb);
		bcd2k->midi.out_active = 0;
	}

	return 0;
}

/* (de)register midi substream from client */
static void bcd2000_midi_output_trigger(struct snd_rawmidi_substream *substream,
						int up)
{
	struct bcd2000 *bcd2k = substream->rmidi->private_data;

	if (up) {
		bcd2k->midi.send_substream = substream;
		/* check if there is data userspace wants to send */
		if (!bcd2k->midi.out_active)
			bcd2000_midi_send(bcd2k);
	} else {
		bcd2k->midi.send_substream = NULL;
	}
}

static void bcd2000_output_complete(struct urb *urb)
{
	struct bcd2000 *bcd2k = urb->context;

	bcd2k->midi.out_active = 0;

	if (urb->status)
		dev_warn(&urb->dev->dev,
			PREFIX "output urb->status: %d\n", urb->status);

	if (urb->status == -ESHUTDOWN)
		return;

	/* check if there is more data userspace wants to send */
	bcd2000_midi_send(bcd2k);
}

static void bcd2000_input_complete(struct urb *urb)
{
	int ret;
	struct bcd2000 *bcd2k = urb->context;

	if (urb->status)
		dev_warn(&urb->dev->dev,
			PREFIX "input urb->status: %i\n", urb->status);

	if (!bcd2k || urb->status == -ESHUTDOWN)
		return;

	if (urb->actual_length > 0)
		bcd2000_midi_handle_input(bcd2k, urb->transfer_buffer,
					urb->actual_length);

	/* return URB to device */
	ret = usb_submit_urb(bcd2k->midi.in_urb, GFP_ATOMIC);
	if (ret < 0)
		dev_err(&bcd2k->dev->dev, PREFIX
			"%s: usb_submit_urb() failed, ret=%d\n",
			__func__, ret);
}

static struct snd_rawmidi_ops bcd2000_midi_output = {
	.open =    bcd2000_midi_output_open,
	.close =   bcd2000_midi_output_close,
	.trigger = bcd2000_midi_output_trigger,
};

static struct snd_rawmidi_ops bcd2000_midi_input = {
	.open =    bcd2000_midi_input_open,
	.close =   bcd2000_midi_input_close,
	.trigger = bcd2000_midi_input_trigger,
};

int bcd2000_init_midi(struct bcd2000 *bcd2k)
{
	int ret;
	struct snd_rawmidi *rmidi;
	struct bcd2000_midi *midi;

	ret = snd_rawmidi_new(bcd2k->card, bcd2k->card->shortname, 0,
					1, /* output */
					1, /* input */
					&rmidi);

	if (ret < 0)
		return ret;

	strlcpy(rmidi->name, bcd2k->card->shortname, sizeof(rmidi->name));

	rmidi->info_flags = SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = bcd2k;

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
					&bcd2000_midi_output);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT,
					&bcd2000_midi_input);

	midi = &bcd2k->midi;
	midi->rmidi = rmidi;

	midi->in_urb = usb_alloc_urb(0, GFP_KERNEL);
	midi->out_urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!midi->in_urb || !midi->out_urb) {
		dev_err(&bcd2k->dev->dev, PREFIX "usb_alloc_urb failed\n");
		return -ENOMEM;
	}

	usb_fill_int_urb(midi->in_urb, bcd2k->dev,
				usb_rcvintpipe(bcd2k->dev, 0x81),
				midi->in_buffer, MIDI_URB_BUFSIZE,
				bcd2000_input_complete, bcd2k, 1);

	usb_fill_int_urb(midi->out_urb, bcd2k->dev,
				usb_sndintpipe(bcd2k->dev, 0x1),
				midi->out_buffer, MIDI_URB_BUFSIZE,
				bcd2000_output_complete, bcd2k, 1);

	init_usb_anchor(&midi->anchor);
	usb_anchor_urb(midi->out_urb, &midi->anchor);
	usb_anchor_urb(midi->in_urb, &midi->anchor);

	/* copy init sequence into buffer */
	memcpy(midi->out_buffer, bcd2000_init_sequence, 52);
	midi->out_urb->transfer_buffer_length = 52;

	/* submit sequence */
	ret = usb_submit_urb(midi->out_urb, GFP_KERNEL);
	if (ret < 0)
		dev_err(&bcd2k->dev->dev, PREFIX
			"%s: usb_submit_urb() out failed, ret=%d: ",
			__func__, ret);
	else
		midi->out_active = 1;

	/* pass URB to device to enable button and controller events */
	ret = usb_submit_urb(midi->in_urb, GFP_KERNEL);
	if (ret < 0)
		dev_err(&bcd2k->dev->dev, PREFIX
			"%s: usb_submit_urb() in failed, ret=%d: ",
			__func__, ret);

	/* ensure initialization is finished */
	usb_wait_anchor_empty_timeout(&midi->anchor, 1000);

	return 0;
}

void bcd2000_free_midi(struct bcd2000 *bcd2k)
{
	/* usb_kill_urb not necessary, urb is aborted automatically */
	usb_free_urb(bcd2k->midi.out_urb);
	usb_free_urb(bcd2k->midi.in_urb);
}
