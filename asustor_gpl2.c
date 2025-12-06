// SPDX-License-Identifier: GPL-2.0-only
/*
 * asustor_gpl2.c - Functions used by asustor_main.c (for asustor.ko) that were copied from the
 *                  Linux kernel and are licensed under GPLv2 ONLY, instead of GPLv2 or later.
 */

#include <linux/dmi.h>

bool asustor_dmi_matches(const struct dmi_system_id *dmi);

/**
 *	dmi_matches - check if dmi_system_id structure matches system DMI data
 *	@dmi: pointer to the dmi_system_id structure to check
 */
// copied from dmi_matches() in Linux 6.11.2 drivers/firmware/dmi_scan.c where it's private (static)
// with only one small change (dmi_val = dmi_get_system_info(s) instead of dmi_ident[s])
bool asustor_dmi_matches(const struct dmi_system_id *dmi)
{
	int i;
	const char *dmi_val;

	for (i = 0; i < ARRAY_SIZE(dmi->matches); i++) {
		int s = dmi->matches[i].slot;
		if (s == DMI_NONE)
			break;
		if (s == DMI_OEM_STRING) {
			/* DMI_OEM_STRING must be exact match */
			const struct dmi_device *valid;

			valid = dmi_find_device(DMI_DEV_TYPE_OEM_STRING,
			                        dmi->matches[i].substr, NULL);
			if (valid)
				continue;
		} else if ((dmi_val = dmi_get_system_info(s)) != NULL) {
			if (dmi->matches[i].exact_match) {
				if (!strcmp(dmi_val, dmi->matches[i].substr))
					continue;
			} else {
				if (strstr(dmi_val, dmi->matches[i].substr))
					continue;
			}
		}

		/* No match */
		return false;
	}
	return true;
}
