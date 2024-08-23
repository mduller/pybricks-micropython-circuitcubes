// SPDX-License-Identifier: MIT
// Copyright (c) 2020-2023 The Pybricks Authors

#define PBSYS_CONFIG_APP_BUILTIN_USER_PROGRAMS                        (1)
#define PBSYS_CONFIG_APP_USER_PROG_FORMAT_MULTI_MPY_V6           (1)
#define PBSYS_CONFIG_APP_USER_PROG_FORMAT_MULTI_MPY_V6_1_NATIVE  (1)
#define PBSYS_CONFIG_BATTERY_CHARGER                (1)
#define PBSYS_CONFIG_BLUETOOTH                      (1)
#define PBSYS_CONFIG_BLUETOOTH_TOGGLE               (1)
#define PBSYS_CONFIG_BLUETOOTH_TOGGLE_BUTTON        (512) // PBIO_BUTTON_RIGHT_UP, but enum value cannot be used here.
#define PBSYS_CONFIG_HUB_LIGHT_MATRIX               (1)
#define PBSYS_CONFIG_MAIN                           (1)
#define PBSYS_CONFIG_STORAGE                        (1)
#define PBSYS_CONFIG_STORAGE_RAM_SIZE               (258 * 1024)
#define PBSYS_CONFIG_STORAGE_ROM_SIZE               (PBDRV_CONFIG_BLOCK_DEVICE_W25QXX_STM32_SIZE)
#define PBSYS_CONFIG_STORAGE_OVERLAPS_BOOTLOADER_CHECKSUM (0)
#define PBSYS_CONFIG_STORAGE_USER_DATA_SIZE         (512)
#define PBSYS_CONFIG_STATUS_LIGHT                   (1)
#define PBSYS_CONFIG_STATUS_LIGHT_BATTERY           (1)
#define PBSYS_CONFIG_STATUS_LIGHT_BLUETOOTH         (1)
#define PBSYS_CONFIG_USER_PROGRAM                   (1)
#define PBSYS_CONFIG_PROGRAM_STOP                   (1)
