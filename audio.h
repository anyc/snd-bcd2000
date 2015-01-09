#ifndef AUDIO_H
#define AUDIO_H

#include <sound/pcm.h>

#define USB_N_URBS 4
#define USB_N_PACKETS_PER_URB 16
#define USB_PACKET_SIZE 360
#define USB_BUFFER_SIZE (USB_PACKET_SIZE * USB_N_PACKETS_PER_URB)

#define BYTES_PER_PERIOD 3528
#define PERIODS_MAX 128
#define ALSA_BUFFER_SIZE (BYTES_PER_PERIOD * PERIODS_MAX)

struct bcd2000;

struct bcd2000_urb {
	struct bcd2000 *bcd2k;
	struct bcd2000_substream *stream;

	/* BEGIN DO NOT SEPARATE */
	struct urb instance;
	struct usb_iso_packet_descriptor packets[USB_N_PACKETS_PER_URB];
	/* END DO NOT SEPARATE */
	u8 *buffer;
};

struct bcd2000_substream {
	spinlock_t lock;
	struct snd_pcm_substream *instance;

	bool active;
	snd_pcm_uframes_t dma_off; /* current position in alsa dma_area */
	snd_pcm_uframes_t period_off; /* current position in current period */

	struct bcd2000_urb urbs[USB_N_URBS];

	struct mutex mutex;
	u8 state;
	wait_queue_head_t wait_queue;
	bool wait_cond;
};

struct bcd2000_pcm {
	struct bcd2000 *bcd2k;

	struct snd_pcm *instance;
	struct snd_pcm_hardware pcm_info;

	struct bcd2000_substream playback;
	struct bcd2000_substream capture;
	bool panic; /* if set driver won't do anymore pcm on device */
};

int bcd2000_init_audio(struct bcd2000 *bcd2k);
void bcd2000_free_audio(struct bcd2000 *bcd2k);

#endif