/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMB03_AXS_FRAME_H
#define EMB03_AXS_FRAME_H
#include <linux/types.h>
#include <linux/errno.h>

#define EMB03_AXS_WIRE_SIZE 32
#define EMB03_AXS_BUFFER_SIZE 33
#define EMB03_AXS_MAX_POINTS 5

struct emb03_axs_point {
	u8 id, event, pressure, area;
	int x, y;
};

/* buf[32] must retain 0xff after the 32-byte receive, as on the original.
 * This parser preserves firmware-derived layout, not another board's format.
 * Return decoded entries; slot release policy is handled separately.
 */
static inline int emb03_axs_decode(const u8 *buf, size_t len,
				   struct emb03_axs_point *points)
{
	u8 sum = 0, count;
	int i, n = 0;

	if (len != EMB03_AXS_BUFFER_SIZE)
		return -EMSGSIZE;
	for (i = 2; i < EMB03_AXS_BUFFER_SIZE; i++)
		sum += buf[i];
	if (sum != buf[1])
		return -EBADMSG;
	count = buf[2] & 0x0f;
	if (count > EMB03_AXS_MAX_POINTS)
		return -EPROTO;
	for (i = 0; i < EMB03_AXS_MAX_POINTS; i++) {
		const u8 *p = buf + 3 + i * 6;
		struct emb03_axs_point *point = &points[n];

		if ((p[2] >> 4) >= EMB03_AXS_MAX_POINTS)
			break;
		point->id = p[2] >> 4;
		point->event = p[0] >> 6;
		if ((point->event == 0 || point->event == 2) && !count)
			return -EPROTO;
		point->x = 720 - (((p[0] & 15) << 8) | p[1]);
		point->y = 1280 - (((p[2] & 15) << 8) | p[3]);
		point->pressure = p[4];
		point->area = p[5] >> 4;
		n++;
	}
	return n;
}
#endif
