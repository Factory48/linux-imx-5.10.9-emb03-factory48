/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef EMB03_CHARGE_PROVIDER_H
#define EMB03_CHARGE_PROVIDER_H
#include <linux/device.h>
#include "emb03-charge-policy.h"

struct emb03_charge *emb03_charge_acquire(struct device *consumer);
bool emb03_charge_is_batteryless(const struct emb03_charge *charge);
#endif
