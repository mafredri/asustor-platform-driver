/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ASUSTOR_MCU_H
#define ASUSTOR_MCU_H

int __init asustor_mcu_init(const char *model_name);
void __exit asustor_mcu_cleanup(void);

#endif /* ASUSTOR_MCU_H */
