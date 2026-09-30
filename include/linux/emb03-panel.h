/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_EMB03_PANEL_H
#define _LINUX_EMB03_PANEL_H

#include <linux/types.h>

/* Board-local single panel/single touch consumer, not a general DRM API.
 * Registration synchronously replays current state, including absent/OFF.
 * All callbacks may sleep and are serialized with registration/removal.
 * OFF must drain touch I/O before returning. READY must only queue work.
 * Callbacks MUST NOT call these APIs or enter panel/DRM code.
 * Unregister waits for callbacks. The caller owns listener storage until then.
 */
struct emb03_panel_listener {
	void (*notify)(struct emb03_panel_listener *listener, bool ready);
};

int emb03_panel_register_listener(struct emb03_panel_listener *listener);
void emb03_panel_unregister_listener(struct emb03_panel_listener *listener);

#endif
