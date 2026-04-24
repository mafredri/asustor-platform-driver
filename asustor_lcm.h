/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ASUSTOR_LCM_H
#define ASUSTOR_LCM_H

int __init asustor_lcm_init(const char *model_name);
void __exit asustor_lcm_cleanup(void);

#endif /* ASUSTOR_LCM_H */
