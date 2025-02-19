/*
 * Functions for saving/restoring console.
 *
 * Originally from swsusp.
 */

#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/vt.h>
#include <linux/module.h>
#include "power.h"

#define SUSPEND_CONSOLE	(MAX_NR_CONSOLES-1)

static int orig_fgconsole, orig_kmsg;

int pm_prepare_console(void)
{
	orig_fgconsole = vt_move_to_console(SUSPEND_CONSOLE, 1);
	if (orig_fgconsole < 0)
		return 1;

	orig_kmsg = vt_kmsg_redirect(SUSPEND_CONSOLE);
	return 0;
}
EXPORT_SYMBOL_GPL(pm_prepare_console);

void pm_restore_console(void)
{
	sleep_trace("1");
	if (orig_fgconsole >= 0) {
		vt_move_to_console(orig_fgconsole, 0);
		sleep_trace("2");
		vt_kmsg_redirect(orig_kmsg);
		sleep_trace("3");
	}
}
EXPORT_SYMBOL_GPL(pm_restore_console);
