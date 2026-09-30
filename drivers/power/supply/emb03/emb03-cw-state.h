/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMB03_CW_STATE_H
#define EMB03_CW_STATE_H

/* Pure state helper shared with host tests. virtual_config is immutable DT
 * policy; failed samples never turn into synthetic measurements.
 */
struct emb03_cw_state {
	bool virtual_config;
	bool sample_valid;
	bool communication_failed;
};

static inline void emb03_cw_sample_result(struct emb03_cw_state *s, bool valid)
{
	s->sample_valid = valid;
	s->communication_failed = !valid;
}

static inline bool emb03_cw_can_open(const struct emb03_cw_state *s,
				   int capacity, int voltage, int online)
{
	return !s->virtual_config && s->sample_valid &&
		!s->communication_failed && online > 0 &&
		capacity >= 0 && capacity <= 100 &&
		(voltage > 3615 || (voltage >= 3001 && voltage <= 3604));
}
#endif
