/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMB03_CHARGE_POLICY_H
#define EMB03_CHARGE_POLICY_H

#include <linux/atomic.h>
#include <linux/kref.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

/* Provider owns one reference; every consumer holds a devm-managed reference.
 * IRQ invalidation uses a generation so a concurrent reset cannot be lost.
 * dead/client are serialized with transactions by lock, not by device links.
 */
struct emb03_charge {
	struct i2c_client *client;
	struct mutex lock;
	struct kref refs;
	bool dead; /* protected by lock; no client access after detach */
	bool batteryless; /* immutable after provider publication, not IRQ state */
	bool reassert_stopped; /* protected by lock */
	struct delayed_work reassert_work;
	atomic_t generation;
	int initialized_generation;
	bool opened;
	int level;
};

void emb03_charge_init(struct emb03_charge *charge, struct i2c_client *client);
void emb03_charge_detach(struct emb03_charge *charge);
int emb03_charge_inhibit(struct emb03_charge *charge);
void emb03_charge_invalidate(struct emb03_charge *charge);
void emb03_charge_close(struct emb03_charge *charge);
int emb03_charge_open(struct emb03_charge *charge);
int emb03_charge_set_level(struct emb03_charge *charge, unsigned int level);
#endif
