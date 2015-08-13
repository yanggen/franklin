// vim: set foldmethod=marker :
#ifndef ADCBITS

// Includes. {{{
#include <stdint.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <sys/time.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>
#include <prussdrv.h>
#include <pruss_intc_mapping.h>
// }}}

// Defines. {{{
#define PRU 0
#define PRU_DATARAM PRUSS0_PRU0_DATARAM
#if !defined(PRU) || (PRU != 0 && PRU != 1)
#error PRU must be defined as 0 or 1.
#endif
#define NUM_ANALOG_INPUTS 8
#define NUM_GPIO_PINS (4 * 32)
#define NUM_DIGITAL_PINS (NUM_GPIO_PINS + 16)
#define ADCBITS 12
#define FRAGMENTS_PER_BUFFER 8
#define SAMPLES_PER_FRAGMENT 256
#define BBB_PRU_FRAGMENT_MASK (FRAGMENTS_PER_BUFFER - 1)

#define ARCH_MOTOR int bbb_id;
#define ARCH_SPACE int bbb_id, bbb_m0;

#define DATA_CLEAR(s, m) do {} while (0)
#define ARCH_NEW_MOTOR(s, m, base) do {} while (0)
#define DATA_DELETE(s, m) do {} while (0)
// }}}

#else

struct bbb_Gpio { // {{{
	uint32_t revision;		// 0
	uint32_t reserved0[3];
	uint32_t sysconfig;		// 10
	uint32_t reserved1[3];
	uint32_t eoi;			// 20
	uint32_t irqstatus_raw_0;	// 24
	uint32_t irqstatus_raw_1;	// 28
	uint32_t irqstatus_0;		// 2c
	uint32_t irqstatus_1;		// 30
	uint32_t irqstatus_set_0;	// 34
	uint32_t irqstatus_set_1;	// 38
	uint32_t irqstatus_clr_0;	// 3c
	uint32_t irqstatus_clr_1;	// 40
	uint32_t irqwaken_0;		// 44
	uint32_t irqwaken_1;		// 48
	uint32_t reserved2[50];
	uint32_t sysstatus;		// 114
	uint32_t reserved3[6];
	uint32_t ctrl;			// 130
	uint32_t oe;			// 134
	uint32_t datain;		// 138
	uint32_t dataout;		// 13c
	uint32_t leveldetect0;		// 140
	uint32_t leveldetect1;		// 144
	uint32_t risingdetect;		// 148
	uint32_t fallingdetect;		// 14c
	uint32_t debounceenable;	// 150
	uint32_t debouncingtime;	// 154
	uint32_t reserved4[14];
	uint32_t cleardataout;		// 190
	uint32_t setdataout;		// 194
};
// }}}

struct bbb_Temp { // {{{
	int id;
	int fd;	// For reading the ADC.
	bool active;
	int power_pin, fan_pin;
	bool power_inverted, fan_inverted;
	int power_target, fan_target;
}; // }}}

struct bbb_Pru { // {{{
	volatile uint16_t neg_base, pos_base;
	// These must be bytes, because read and write must be atomic.
	volatile uint8_t current_sample, current_fragment, next_fragment, state;
	volatile uint16_t buffer[FRAGMENTS_PER_BUFFER][SAMPLES_PER_FRAGMENT][2];
} __attribute__ ((packed)); // }}}

// Function declarations. {{{
void SET_OUTPUT(Pin_t _pin);
void SET_INPUT(Pin_t _pin);
void SET_INPUT_NOPULLUP(Pin_t _pin);
void SET(Pin_t _pin);
void RESET(Pin_t _pin);
void GET(Pin_t _pin, bool _default, void(*cb)(bool));
void arch_setup_start(char const *port);
void arch_setup_end(char const *run_id);
void arch_setup_temp(int which, int thermistor_pin, int active, int power_pin = -1, bool power_inverted = true, int power_target = 0, int fan_pin = -1, bool fan_inverted = false, int fan_target = 0);
void arch_motors_change();
void arch_addpos(int s, int m, int diff);
void arch_stop(bool fake);
void arch_home();
bool arch_running();
void arch_start_move(int extra);
bool arch_send_fragment();
int arch_fds();
int arch_tick();
void arch_set_duty(Pin_t pin, double duty);
double arch_get_duty(Pin_t pin);
void arch_discard();
off_t arch_send_audio(uint8_t *data, off_t sample, off_t num_records, int motor);
void DATA_SET(int s, int m, int value);
// }}}

// Variables. {{{
EXTERN volatile bbb_Gpio *bbb_gpio[4];
EXTERN volatile uint32_t *bbb_padmap;
EXTERN volatile uint32_t *bbb_gpio_pad[32 * 4];
EXTERN int bbb_devmem;
EXTERN int bbb_active_temp;
EXTERN bbb_Temp bbb_temp[NUM_ANALOG_INPUTS];
EXTERN bbb_Pru *bbb_pru;
extern int bbb_pru_pad[16], bbb_pru_mode[16];
// }}}

#ifdef DEFINE_VARIABLES
#if PRU == 0
int bbb_pru_pad[16] = { 110, 111, 112, 113, 114, 115, 116, 117, 90, 91, 92, 93, 94, 95, 44, 45 };
int bbb_pru_mode[16] = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6 };
#else
int bbb_pru_pad[16] = { 70, 71, 72, 73, 74, 75, 76, 77, 86, 87, 88, 89, 62, 63, 42, 43 };
int bbb_pru_mode[16] = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5 };
#endif
// Pin setting. {{{
void SET_OUTPUT(Pin_t _pin) {
	if (_pin.valid()) {
		if (_pin.pin < NUM_GPIO_PINS) {
			bbb_gpio[_pin.pin >> 5]->oe &= ~(1 << (_pin.pin & 0x1f));
			if (bbb_gpio_pad[_pin.pin])
				*bbb_gpio_pad[_pin.pin] = 0x2f;	// Fast, with receiver, no pull, gpio.
		}
		else {
			int pin = _pin.pin - NUM_GPIO_PINS;
			debug("setting pru pin to output: pru pin %d gpio %d target %x", pin, bbb_pru_pad[pin], bbb_pru_mode[pin]);
			*bbb_gpio_pad[bbb_pru_pad[pin]] = bbb_pru_mode[pin];	// Fast, with receiver, no pull.
			debug("new state: %x", *bbb_gpio_pad[bbb_pru_pad[pin]]);
		}
	}
}

void SET_INPUT(Pin_t _pin) {
	if (_pin.valid()) {
		if (_pin.pin < NUM_GPIO_PINS) {
			bbb_gpio[_pin.pin >> 5]->oe |= 1 << (_pin.pin & 0x1f);
			if (bbb_gpio_pad[_pin.pin])
				*bbb_gpio_pad[_pin.pin] = 0x37;	// Fast, with receiver, pull, up, gpio.
		}
		else {
			debug("warning: trying to set pru pin to input");
		}
	}
}

void SET_INPUT_NOPULLUP(Pin_t _pin) {
	if (_pin.valid()) {
		if (_pin.pin < NUM_GPIO_PINS) {
			bbb_gpio[_pin.pin >> 5]->oe |= 1 << (_pin.pin & 0x1f);
			if (bbb_gpio_pad[_pin.pin])
				*bbb_gpio_pad[_pin.pin] = 0x2f;	// Fast, with receiver, no pull, gpio.
		}
		else {
			// Disable this pin.
			int pin = _pin.pin - NUM_GPIO_PINS;
			*bbb_gpio_pad[bbb_pru_pad[pin]] = 0x2f;	// Fast, with receiver, no pull, gpio.
		}
	}
}

#define RAWSET(_p) bbb_gpio[(_p) >> 5]->setdataout = 1 << ((_p) & 0x1f)
#define RAWRESET(_p) bbb_gpio[(_p) >> 5]->cleardataout = 1 << ((_p) & 0x1f)
void SET(Pin_t _pin) {
	SET_OUTPUT(_pin);
	if (_pin.valid() && _pin.pin < NUM_GPIO_PINS) {
		if (_pin.inverted())
			RAWRESET(_pin.pin);
		else
			RAWSET(_pin.pin);
	}
}

void RESET(Pin_t _pin) {
	SET_OUTPUT(_pin);
	if (_pin.valid() && _pin.pin < NUM_GPIO_PINS) {
		if (_pin.inverted())
			RAWSET(_pin.pin);
		else
			RAWRESET(_pin.pin);
	}
}

#define RAWGET(_p) (bool(bbb_gpio[(_p) >> 5]->datain & (1 << ((_p) & 0x1f))))
void GET(Pin_t _pin, bool _default, void(*cb)(bool)) {
	if (_pin.valid() && _pin.pin < NUM_GPIO_PINS) {
		if (RAWGET(_pin.pin))
			cb(!_pin.inverted());
		else
			cb(_pin.inverted());
	}
	else
		cb(_default);
}
// }}}

// Setup helpers. {{{
void arch_setup_start(char const *port) {
	// TODO: find out if this thing varies, implement a search if it does.
	//FILE *f = fopen("/sys/devices/bone_capemgr.9/slots", "w");
	//if (!f) {
	//	fprintf(stderr, "unable to open slots file; are you root?");
	//	abort();
	//}
	//fprintf(f, "BB-ADC\n");
	//fclose(f);
	for (int i = 0; i < NUM_ANALOG_INPUTS; ++i) {
		// TODO: find out if this varies, implement a search if so.
		//char *filename;
		//asprintf(&filename, "/sys/devices/ocp.3/helper.15/AIN%d", i);
		//bbb_temp[i].fd = open(filename, O_RDONLY);
		bbb_temp[i].fd = -1;
		//free(filename);
		//if (bbb_temp[i].fd < 0) {
		//	fprintf(stderr, "unable to open analog input");
		//	abort();
		//}
		bbb_temp[i].active = false;
	}
	bbb_devmem = open("/dev/mem", O_RDWR);
	unsigned gpio_base[4] = { 0x44E07000, 0x4804C000, 0x481AC000, 0x481AE000 };
	unsigned pad_control_base = 0x44e10000;
	for (int i = 0; i < 4; ++i)
		bbb_gpio[i] = (volatile bbb_Gpio *)mmap(0, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, bbb_devmem, gpio_base[i]);
	int pad_offset[32 * 4] = {
		0x148, 0x14c, 0x150, 0x154, 0x158, 0x15c, 0x160, 0x164, 0xd0, 0xd4, 0xd8, 0xdc, 0x178, 0x17c, 0x180, 0x184,
		0x11c, 0x120, 0x21c, 0x1b0, 0x1b4, 0x124, 0x20, 0x24, -1, -1, 0x28, 0x2c, 0x128, 0x144, 0x70, 0x74,

		0x0, 0x4, 0x8, 0xc, 0x10, 0x14, 0x18, 0x1c, 0x168, 0x16c, 0x170, 0x174, 0x30, 0x34, 0x38, 0x3c,
		0x40, 0x44, 0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60, 0x64, 0x68, 0x6c, 0x78, 0x7c, 0x80, 0x84,

		0x88, 0x8c, 0x90, 0x94, 0x98, 0x9c, 0xa0, 0xa4, 0xa8, 0xac, 0xb0, 0xb4, 0xb8, 0xbc, 0xc0, 0xc4,
		0xc8, 0xcc, 0x134, 0x138, 0x13c, 0x140, 0xe0, 0xe4, 0xe8, 0xec, 0xf0, 0xf4, 0xf8, 0xfc, 0x100, 0x104,

		0x108, 0x10c, 0x110, 0x114, 0x118, 0x188, 0x18c, 0x1e4, 0x1e8, 0x12c, 0x130, -1, -1, 0x234, 0x190, 0x194,
		0x198, 0x19c, 0x1a0, 0x1a4, 0x1a8, 0x1ac, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
	};
	bbb_padmap = (volatile uint32_t *)mmap(0, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, bbb_devmem, pad_control_base);
	for (int i = 0; i < 32 * 4; ++i)
		bbb_gpio_pad[i] = pad_offset[i] >= 0 ? &bbb_padmap[(0x800 + pad_offset[i]) / 4] : NULL;
	bbb_active_temp = -1;
	tpruss_intc_initdata pruss_intc_initdata = PRUSS_INTC_INITDATA;
	prussdrv_init();
	int ret = prussdrv_open(PRU_EVTOUT_0);
	if (ret) {
		debug("unable to open pru system");
		abort();
	}
	prussdrv_pruintc_init(&pruss_intc_initdata);
	prussdrv_map_prumem(PRU_DATARAM, (void **)&bbb_pru);
	prussdrv_exec_program(PRU, "/usr/lib/franklin/bbb_pru.bin");
}

void arch_setup_end(char const *run_id) {
	// Override hwtime_step.
	hwtime_step = 40;
	setup_end();
}

static void bbb_next_adc() {
	for (int i = 0; i < NUM_ANALOG_INPUTS; ++i) {
		int n = (bbb_active_temp + 1 + i) % NUM_ANALOG_INPUTS;
		if (bbb_temp[n].active) {
			bbb_active_temp = n;
			return;
		}
	}
	bbb_active_temp = -1;
}

void arch_setup_temp(int which, int thermistor_pin, int active, int power_pin, bool power_inverted, int power_target, int fan_pin, bool fan_inverted, int fan_target) {
	bbb_temp[thermistor_pin].active = active;
	bbb_temp[thermistor_pin].id = which;
	bbb_temp[thermistor_pin].power_pin = power_pin;
	bbb_temp[thermistor_pin].power_inverted = power_inverted;
	bbb_temp[thermistor_pin].power_target = power_target;
	bbb_temp[thermistor_pin].fan_pin = fan_pin;
	bbb_temp[thermistor_pin].fan_inverted = fan_inverted;
	bbb_temp[thermistor_pin].fan_target = fan_target;
	if (bbb_active_temp < 0)
		bbb_next_adc();
}
// }}}

// Runtime helpers. {{{

// PRU has per motor:
// buffer with steps: writable by cpu until active, then readable by pru until discarded.
// current fragment in buffer: writable by pru, readable by cpu.
// current sample in buffer: writable by pru, readable by cpu.
// current moving direction (-1/0/1): writable by pru, readable by cpu.
//
// PRU globals:
// state: 0: waiting for limit check; cpu can set to 1, 2, or 3.
// state: 1: not running; cpu can set to 0.
// state: 2: Doing single step; pru can set to 0; cpu can set to 4 (and expect pru to set it to 0 or 1).
// state: 3: Free running; cpu can set to 4.
// state: 4: cpu requested stop; pru must set to 1.
int arch_tick() {
	// Handle temps and check limit switches.
	if (bbb_active_temp >= 0) {
		int a = bbb_active_temp;
		// New temperature ready to read.
		char data[6];	// 12 bit adc: maximum 4 digits, plus newline and NUL.
		/*int num = read(bbb_temp[a].fd, data, sizeof(data));
		bbb_next_adc();
		if (num <= 0)
			debug("Error reading from adc.");
		else */if (bbb_temp[a].active) {
			int t = 0; //atoi(data);
			if (bbb_temp[a].power_pin >= 0) {
				if ((bbb_temp[a].power_target < t) ^ bbb_temp[a].power_inverted)
					RAWSET(bbb_temp[a].power_pin);
				else
					RAWRESET(bbb_temp[a].power_pin);
			}
			if (bbb_temp[a].fan_pin >= 0) {
				if ((bbb_temp[a].fan_target < t) ^ bbb_temp[a].fan_inverted)
					RAWSET(bbb_temp[a].fan_pin);
				else
					RAWRESET(bbb_temp[a].fan_pin);
			}
			handle_temp(bbb_temp[a].id, t);
		}
	}
	else
		bbb_next_adc();
	// TODO: Pwm.
	int state = bbb_pru->state;
	if (state != 2 && state != 1) {
		// Check probe.
		if (settings.probing && probe_pin.valid()) {
			if (RAWGET(probe_pin.pin) ^ probe_pin.inverted()) {
				// Probe hit.
				sending_fragment = 0;
				stopping = 2;
				send_host(CMD_LIMIT, -1, -1, NAN);
				cbs_after_current_move = 0;
			}
		}
		int m0 = 0;
		// Avoid race condition by reading cf twice.
		int cf = bbb_pru->current_fragment;
		int cs;
		while (true) {
			cs = bbb_pru->current_sample;
			int cf2 = bbb_pru->current_fragment;
			if (cf == cf2)
				break;
			cf = cf2;
		}
		for (int s = 0; s < num_spaces; ++s) {
			for (int m = 0; m < spaces[s].num_motors; ++m) {
				if (!spaces[s].motor[m]->active || !spaces[s].motor[m]->step_pin.valid() || spaces[s].motor[m]->step_pin.pin < NUM_GPIO_PINS)
					continue;
				int pin = spaces[s].motor[m]->step_pin.pin - NUM_GPIO_PINS;
				bool negative = bool((bbb_pru->buffer[cf][cs][0] ^ bbb_pru->neg_base) & (1 << pin)) ^ spaces[s].motor[m]->dir_pin.inverted();
				Pin_t *p = negative ? &spaces[s].motor[m]->limit_max_pin : &spaces[s].motor[m]->limit_min_pin;
				if (!p->valid())
					continue;
				if (RAWGET(p->pin) ^ p->inverted()) {
					// Limit hit.
					sending_fragment = 0;
					stopping = 2;
					send_host(CMD_LIMIT, s, m, spaces[s].motor[m]->settings.current_pos / spaces[s].motor[m]->steps_per_unit);
					cbs_after_current_move = 0;
				}
			}
			m0 += spaces[s].num_motors;
		}
		// TODO: homing.
		if (state == 0) {
			state = settings.probing ? 2 : 3; // TODO: homing.
			bbb_pru->state = state;
		}
		// TODO: LED.
		// TODO: Timeout.
	}
	return state == 3 ? 100 : state == 2 ? 10 : 200;
}

void arch_motors_change() {
	for (int s = 0; s < num_spaces; ++s) {
		for (int m = 0; m < spaces[s].num_motors; ++m) {
			Pin_t *p = &spaces[s].motor[m]->dir_pin;
			if (p->valid() && p->pin >= NUM_GPIO_PINS) {
				int pin = p->pin - NUM_GPIO_PINS;
				if (p->inverted())
					bbb_pru->pos_base |= 1 << pin;
				else
					bbb_pru->neg_base |= 1 << pin;
			}
			p = &spaces[s].motor[m]->step_pin;
			if (p->valid() && p->pin >= NUM_GPIO_PINS && p->inverted()) {
				int pin = p->pin - NUM_GPIO_PINS;
				bbb_pru->pos_base |= 1 << pin;
				bbb_pru->neg_base |= 1 << pin;
			}
		}
	}
	// Configure hardware for updated settings.
	// number of active motors.
	// hwtime_step
	// led pin
	// probe pin
	// timeout
	// motor pins
}

void arch_addpos(int s, int m, int diff) {
	// Nothing to do.
}

void arch_stop(bool fake) {
	// Stop moving, update current_pos.
	int state = bbb_pru->state;
	switch (state) {
	case 1:
		return;
	case 0:
		break;
	case 2:
	case 3:
		bbb_pru->state = 4;
		// Wait for pru to ack.
		while (bbb_pru->state == 4) {}
		break;
	}
	bbb_pru->state = 1;
	// Update current_pos.
	abort_move(bbb_pru->current_sample);
	current_fragment_pos = 0;
}

void arch_home() { // TODO
	// Start homing.
}

bool arch_running() {
	// True if an underrun will follow.
	return bbb_pru->state != 1;
}

void arch_start_move(int extra) {
	// Start moving with sent buffers.
	int state = bbb_pru->state;
	if (state != 1)
		debug("arch_start_move called with non-1 state %d", state);
	bbb_pru->state = 0;
}

bool arch_send_fragment() {
	bbb_pru->next_fragment = (bbb_pru->next_fragment + 1) & BBB_PRU_FRAGMENT_MASK;
}

int arch_fds() {
	return 0;
}

double arch_get_duty(Pin_t _pin) { // TODO
	if (_pin.pin < 0 || _pin.pin >= NUM_GPIO_PINS) {
		debug("invalid pin for arch_get_duty: %d(max %d)", _pin.pin, NUM_GPIO_PINS);
		return 1;
	}
	return 1;
}

void arch_set_duty(Pin_t _pin, double duty) { // TODO
	if (_pin.pin < 0 || _pin.pin >= NUM_GPIO_PINS) {
		debug("invalid pin for arch_set_duty: %d(max %d)", _pin.pin, NUM_GPIO_PINS);
		return;
	}
	// TODO.
}

void arch_discard() {
	int fragments = (current_fragment - bbb_pru->current_fragment) & BBB_PRU_FRAGMENT_MASK;
	if (fragments <= 2)
		return;
	current_fragment = (current_fragment - (fragments - 2)) & BBB_PRU_FRAGMENT_MASK;
	bbb_pru->next_fragment = current_fragment;
	restore_settings();
}

off_t arch_send_audio(uint8_t *data, off_t sample, off_t num_records, int motor) {
	// TODO.
}

static void bbb_set_pru(int which, int s, int m) {
	int pin = spaces[s].motor[m]->step_pin.pin - NUM_GPIO_PINS;
	if (spaces[s].motor[m]->step_pin.valid() && pin >= 0) {
		if (spaces[s].motor[m]->step_pin.inverted())
			bbb_pru->buffer[current_fragment][current_fragment_pos][which] &= ~(1 << pin);
		else
			bbb_pru->buffer[current_fragment][current_fragment_pos][which] |= 1 << pin;
	}
}

void DATA_SET(int s, int m, int value) {
	int id = spaces[s].motor[m]->bbb_id;
	if (value) {
		if (value < -1 || value > 1) {
			debug("invalid sample %d for %d %d", value, s, m);
			abort();
		}
		// The invert flag of the dir pin is used even if the pin is invalid.
		if ((value < 0) ^ spaces[s].motor[m]->dir_pin.inverted())
			bbb_set_pru(0, s, m);
		else
			bbb_set_pru(1, s, m);
	}
}
// }}}
#endif

#endif
