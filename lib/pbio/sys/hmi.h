// SPDX-License-Identifier: MIT
// Copyright (c) 2020 The Pybricks Authors

#ifndef _PBSYS_SYS_HMI_H_
#define _PBSYS_SYS_HMI_H_

#include <contiki.h>
#include <pbsys/config.h>

void pbsys_hmi_init(void);
void pbsys_hmi_handle_event(process_event_t event, process_data_t data);
void pbsys_hmi_poll(void);

#if PBSYS_CONFIG_HMI_NUM_SLOTS
uint8_t pbsys_hmi_get_selected_program_slot(void);
#else
static inline uint8_t pbsys_hmi_get_selected_program_slot(void) {
    return 0;
}
#endif

#endif // _PBSYS_SYS_HMI_H_
