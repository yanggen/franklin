#include "firmware.h"

void handle_motors() {
	if (step_state == 1) {
		set_speed(0);
		return;
	}
	last_active = seconds();
	cli();
	uint8_t state = step_state;
	uint8_t cf = current_fragment;
	uint8_t cs = current_sample;
	sei();
	// Check probe.
	bool probed;
	if (settings[cf].probing && probe_pin < NUM_DIGITAL_PINS) {
		if (state == 0) {
			//debug("checking probe %d %d", cf, cs);
			if (GET(probe_pin) ^ bool(pin_flags & 2))
				stopping = active_motors;
			probed = true;
		}
		else
			probed = false;
	}
	else
		probed = true;	// If we didn't need to probe; don't block later on.
	if (stopping < 0) {
		// Check sensors.
		for (uint8_t m = 0; m < active_motors; ++m) {
			if (!(motor[m].flags & Motor::ACTIVE))
				continue;
			//debug("check %d", m);
			// Check sense pins.
			if (motor[m].sense_pin < NUM_DIGITAL_PINS) {
				if (GET(motor[m].sense_pin) ^ bool(motor[m].flags & Motor::SENSE_STATE)) {
					//debug("sense %d %x", m, motor[m].flags);
					motor[m].flags ^= Motor::SENSE_STATE;
					motor[m].flags |= (motor[m].flags & Motor::SENSE_STATE ? Motor::SENSE1 : Motor::SENSE0);
					arch_record_sense(motor[m].flags & Motor::SENSE_STATE);
				}
			}
			// Check limit switches.
			if (stopping < 0) {
				uint8_t limit_pin = buffer[cf][m][cs] < 0 ? motor[m].limit_min_pin : motor[m].limit_max_pin;
				if (limit_pin < NUM_DIGITAL_PINS) {
					bool inverted = motor[m].flags & (buffer[cf][m][cs] < 0 ? Motor::INVERT_LIMIT_MIN : Motor::INVERT_LIMIT_MAX);
					if (GET(limit_pin) ^ inverted) {
						//debug("hit %d %d", state, buffer[cf][m][cs]);
						stopping = m;
						motor[m].flags |= Motor::LIMIT;
						break;
					}
				}
			}
		}
	}
	if (stopping >= 0) {
		// Hit endstop or probe; disable timer interrupt.
		step_state = 1;
		//debug("hit limit %d curpos %ld cf %d ncf %d lf %d cfp %d", m, F(motor[m].current_pos), cf, notified_current_fragment, last_fragment, cs);
		// Notify host.
		limit_fragment_pos = cs;
		set_speed(0);
		return;
	}
	if (homers > 0) {
		// Homing.
		if (state == 0) {
			probed = true;
			for (uint8_t m = 0; m < active_motors; ++m) {
				if (!(motor[m].flags & Motor::ACTIVE))
					continue;
				// Get twe "wrong" limit pin for the given direction.
				uint8_t limit_pin = (buffer[cf][m][cs] < 0 ? motor[m].limit_max_pin : motor[m].limit_min_pin);
				bool inverted = motor[m].flags & (buffer[cf][m][cs] < 0 ? Motor::INVERT_LIMIT_MAX : Motor::INVERT_LIMIT_MIN);
				if (limit_pin >= NUM_DIGITAL_PINS || GET(limit_pin) ^ inverted) {
					// Limit pin still triggered; continue moving.
					continue;
				}
				// Limit pin no longer triggered.  Stop moving and possibly notify host.
				motor[m].flags &= ~Motor::ACTIVE;
				if (!--homers) {
					set_speed(0);
					return;
				}
			}
		}
		else
			probed = false;
	}
	if (state == 0 && probed) {
		if (homers > 0)
			current_sample = 0;	// Use only the first sample for homing.
		step_state = homers > 0 || settings[cf].probing ? 2 : 3;
	}
}
