#ifndef CONTROL_H
#define CONTROL_H

struct bcd2000;

struct bcd2000_control {
	struct bcd2000 *bcd2k;

	bool phono_mic_switch;
};

int bcd2000_init_control(struct bcd2000 *bcd2k);
void bcd2000_free_control(struct bcd2000 *bcd2k);

#endif