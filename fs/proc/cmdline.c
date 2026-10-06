// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/ctype.h>
#include <linux/string.h>

/* Software view for the dedicated EMB03 test terminal. Never modify the
 * command line consumed by the kernel or the physical bootloader state.
 */
#define BOOT_VIEW(key, value) { key, sizeof(key) - 1, value }
static const struct {
	const char *key;
	size_t key_len;
	const char *value;
} test_boot_view[] = {
	BOOT_VIEW("androidboot.verifiedbootstate=", "green"),
	BOOT_VIEW("androidboot.flash.locked=", "1"),
	BOOT_VIEW("androidboot.vbmeta.device_state=", "locked"),
	BOOT_VIEW("androidboot.veritymode=", "enforcing"),
	BOOT_VIEW("androidboot.vendor.sysrq=", NULL),
};
#undef BOOT_VIEW

static int cmdline_proc_show(struct seq_file *m, void *v)
{
	const char *p = saved_command_line;

	while (*p) {
		const char *start = p;
		bool quoted = false;
		size_t len;
		unsigned int i;

		if (isspace(*p)) {
			while (isspace(*p))
				p++;
			seq_write(m, start, p - start);
			continue;
		}
		do {
			if (*p == '"')
				quoted = !quoted;
			p++;
		} while (*p && (quoted || !isspace(*p)));
		len = p - start;
		for (i = 0; i < ARRAY_SIZE(test_boot_view); i++) {
			size_t key_len = test_boot_view[i].key_len;

			if (len < key_len ||
			    strncmp(start, test_boot_view[i].key, key_len))
				continue;
			if (test_boot_view[i].value) {
				seq_puts(m, test_boot_view[i].key);
				seq_puts(m, test_boot_view[i].value);
			}
			break;
		}
		if (i == ARRAY_SIZE(test_boot_view))
			seq_write(m, start, len);
	}
	seq_putc(m, '\n');
	return 0;
}

static int __init proc_cmdline_init(void)
{
	proc_create_single("cmdline", 0, NULL, cmdline_proc_show);
	return 0;
}
fs_initcall(proc_cmdline_init);
