// SPDX-License-Identifier: GPL-2.0-only
/* Firmware-derived EMB03 SY6915 transactions, used by the provider.
 * No user register-write interface; transactions require a live handle.
 * Auxiliary OMU/ZWB loads are excluded by the requested product scope.
 * See evidence/reverse/power-state-recovery.md and cw-irq-recovery.md.
 */
#include <linux/errno.h>
#include "emb03-charge-policy.h"

static int emb03_read_pair(struct emb03_charge *c, u8 reg)
{
	u8 buf[2];
	int ret = i2c_smbus_read_i2c_block_data(c->client, reg, 2, buf);

	return ret == 2 ? 0 : (ret < 0 ? ret : -EIO);
}

static int emb03_write_pair(struct emb03_charge *c, u8 reg, u8 low, u8 high)
{
	u8 buf[2] = { low, high };
	int ret = i2c_smbus_write_i2c_block_data(c->client, reg, 2, buf);

	if (ret < 0)
		return ret;
	/* Original code reads back; it does not compare against written bytes. */
	return emb03_read_pair(c, reg);
}

/* Batteryless writes use the existing low-byte-first two-byte block protocol.
 * Always read actual data, including after a failed write. Never infer inhibit
 * from software state. Do not overwrite unknown option bits after a bad read.
 */
static int emb03_read_value(struct emb03_charge *c, u8 reg, u16 *value)
{
	u8 buf[2];
	int ret = i2c_smbus_read_i2c_block_data(c->client, reg, 2, buf);

	if (ret != 2)
		return ret < 0 ? ret : -EIO;
	*value = buf[0] | (buf[1] << 8);
	return 0;
}

static int emb03_write_verify(struct emb03_charge *c, u8 reg, u16 value,
			      u16 mask)
{
	u8 buf[2] = { value & 0xff, value >> 8 };
	u16 actual;
	int wr, rd;

	wr = i2c_smbus_write_i2c_block_data(c->client, reg, 2, buf);
	rd = emb03_read_value(c, reg, &actual);
	if (wr < 0)
		return wr;
	if (rd)
		return rd;
	return (actual & mask) == (value & mask) ? 0 : -EIO;
}

int emb03_charge_inhibit(struct emb03_charge *c)
{
	u16 option;
	int inhibit, current_ret;

	mutex_lock(&c->lock);
	if (c->dead || !c->batteryless) {
		inhibit = c->dead ? -ENODEV : -EPERM;
		goto out;
	}
	c->opened = false;
	c->level = -1;
	inhibit = emb03_read_value(c, 0x12, &option);
	if (!inhibit)
		inhibit = emb03_write_verify(c, 0x12, option | 1, 1);
	/* Independent second stop mechanism, even if option read/write failed. */
	current_ret = emb03_write_verify(c, 0x14, 0, 0xffff);
	if (inhibit || current_ret)
		dev_err_ratelimited(&c->client->dev,
			"batteryless stop unverified: inhibit=%d current=%d\n",
			inhibit, current_ret);
	if (!inhibit)
		inhibit = current_ret;
out:
	mutex_unlock(&c->lock);
	return inhibit;
}

void emb03_charge_init(struct emb03_charge *c, struct i2c_client *client)
{
	c->client = client;
	mutex_init(&c->lock);
	kref_init(&c->refs);
	c->dead = false;
	c->batteryless = false;
	c->reassert_stopped = true;
	atomic_set(&c->generation, 0);
	c->initialized_generation = 0;
	c->opened = false;
	c->level = -1;
}

/* Wait for the in-flight transaction, then prohibit all future client access.
 * This is NOT a hardware charging-disable operation.
 */
void emb03_charge_detach(struct emb03_charge *c)
{
	mutex_lock(&c->lock);
	c->dead = true;
	c->opened = false;
	c->client = NULL;
	mutex_unlock(&c->lock);
}

void emb03_charge_invalidate(struct emb03_charge *c)
{
	/* IRQ-safe; no GPIO/I2C access and no sleeping lock. */
	atomic_inc(&c->generation);
}

void emb03_charge_close(struct emb03_charge *c)
{
	mutex_lock(&c->lock);
	/* Vendor close only clears software state; do not invent a HW write. */
	c->opened = false;
	mutex_unlock(&c->lock);
}

int emb03_charge_open(struct emb03_charge *c)
{
	int ret, generation;

	mutex_lock(&c->lock);
	if (c->dead) {
		ret = -ENODEV;
		goto out;
	}
	if (c->batteryless) {
		ret = -EPERM;
		goto out;
	}
	generation = atomic_read(&c->generation);
	if (c->opened && generation == c->initialized_generation) {
		ret = 0;
		goto out;
	}
	c->opened = false;
	ret = emb03_read_pair(c, 0xff);
	if (ret)
		goto out;
	ret = emb03_read_pair(c, 0xfe);
	if (ret)
		goto out;
	ret = emb03_write_pair(c, 0x12, 0x12, 0x9f);
	if (ret)
		goto out;
	ret = emb03_write_pair(c, 0x15, 0xd0, 0x10);
	if (ret)
		goto out;
	ret = emb03_read_pair(c, 0x3f);
	if (ret)
		goto out;
	ret = emb03_write_pair(c, 0x14, 0x00, 0x10);
	if (ret)
		goto out;
	/* New IRQ generation remains pending even if raised during this call. */
	c->initialized_generation = generation;
	c->opened = true;
	c->level = 3;
out:
	mutex_unlock(&c->lock);
	return ret;
}

int emb03_charge_set_level(struct emb03_charge *c, unsigned int level)
{
	static const u8 levels[][2] = {
		{ 0x80, 0x00 }, { 0x80, 0x0c },
		{ 0x00, 0x10 }, { 0x80, 0x10 },
	};
	int ret = 0;

	/* Unlike original default-zero write, reject invalid level requests. */
	if (level < 1 || level > ARRAY_SIZE(levels))
		return -EINVAL;
	mutex_lock(&c->lock);
	if (c->dead) {
		ret = -ENODEV;
		goto out;
	}
	if (c->batteryless) {
		ret = -EPERM;
		goto out;
	}
	if (!c->opened || c->initialized_generation != atomic_read(&c->generation))
		goto out;
	if (c->level == level)
		goto out;
	ret = emb03_write_pair(c, 0x14, levels[level - 1][0], levels[level - 1][1]);
	/* Unlike original pre-write assignment, permit retry after I2C failure. */
	if (!ret)
		c->level = level;
out:
	mutex_unlock(&c->lock);
	return ret;
}
