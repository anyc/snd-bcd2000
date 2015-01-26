#ifndef MIDI_H
#define MIDI_H

#include <sound/rawmidi.h>

#define MIDI_URB_BUFSIZE 64
#define MIDI_CMD_PREFIX_INIT {0x03, 0x00}

struct bcd2000;

#define MIDI_BUFSIZE 64

struct bcd2000_midi {
	struct bcd2000 *bcd2k;

	int out_active;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *receive_substream;
	struct snd_rawmidi_substream *send_substream;

	unsigned char in_buffer[MIDI_URB_BUFSIZE];
	unsigned char out_buffer[MIDI_URB_BUFSIZE];

	struct urb *out_urb;
	struct urb *in_urb;

	struct usb_anchor anchor;
};

int bcd2000_init_midi(struct bcd2000 *bcd2k);
void bcd2000_free_midi(struct bcd2000 *bcd2k);

#endif