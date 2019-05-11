// SPDX-License-Identifier: MIT
// Copyright (c) 2018 David Lechner

#include <string.h>

#include "pbdrv/battery.h"
#include "pbdrv/bluetooth.h"
#include "pbdrv/config.h"
#include "pbdrv/light.h"

#include "pbio/button.h"
#include "pbio/motor.h"
#include "pbio/event.h"
#include "pbio/light.h"

#include "pbsys/sys.h"

#include "sys/clock.h"
#include "sys/etimer.h"
#include "sys/process.h"

#include "stm32f070xb.h"

// workaround upstream NVIC_SystemReset() not decorated with noreturn
void NVIC_SystemReset(void) __attribute__((noreturn));

typedef enum {
    LED_STATUS_BUTTON_PRESSED   = 1 << 0,
    LED_STATUS_BATTERY_LOW      = 1 << 1,
} led_status_flags_t;

// Bootloader reads this address to know if firmware loader should run
uint32_t bootloader_magic_addr __attribute__((section(".magic")));
#define BOOTLOADER_MAGIC_VALUE  0xAAAAAAAA

// period over which the battery voltage is averaged (in milliseconds)
#define BATTERY_PERIOD_MS       2500

#define BATTERY_OK_MV           6000    // 1.0V per cell
#define BATTERY_LOW_MV          5400    // 0.9V per cell
#define BATTERY_CRITICAL_MV     4800    // 0.8V per cell

// ring buffer size for stdin data - must be power of 2!
#define STDIN_BUF_SIZE 128

// Bitmask of status indicators
static led_status_flags_t led_status_flags;

// the previous timestamp from when _pbsys_poll() was called
static clock_time_t prev_poll_time;

// values for keeping track of how long button has been pressed
static bool button_pressed;
static clock_time_t button_press_start_time;

// the battery voltage averaged over BATTERY_PERIOD_MS
static uint16_t avg_battery_voltage;

// user program stop function
static pbsys_stop_callback_t user_stop_func;
// user program stdin event function
static pbsys_stdin_event_callback_t user_stdin_event_func;

// stdin ring buffer
static uint8_t stdin_buf[STDIN_BUF_SIZE];
static uint8_t stdin_buf_head, stdin_buf_tail;

PROCESS(pbsys_process, "System");

void pbsys_prepare_user_program(const pbsys_user_program_callbacks_t *callbacks) {
    if (callbacks) {
        user_stop_func = callbacks->stop;
        user_stdin_event_func = callbacks->stdin_event;
    }
    else {
        user_stop_func = NULL;
        user_stdin_event_func = NULL;
    }
    _pbio_light_set_user_mode(true);
    pbio_light_on_with_pattern(PBIO_PORT_SELF, PBIO_LIGHT_COLOR_GREEN, PBIO_LIGHT_PATTERN_BREATHE);
}

void pbsys_unprepare_user_program(void) {
    uint8_t r, g, b;

    user_stop_func = NULL;
    user_stdin_event_func = NULL;
    _pbio_light_set_user_mode(false);
    pbdrv_light_get_rgb_for_color(PBIO_PORT_SELF, PBIO_LIGHT_COLOR_BLUE, &r, &g, &b);
    pbdrv_light_set_rgb(PBIO_PORT_SELF, r, g, b);

    // TODO: would be nice to have something like _pbio_light_set_user_mode() for motors
    for (int i = 0; i < PBDRV_CONFIG_NUM_MOTOR_CONTROLLER; i++) {
        pbio_motor_coast(&motor[i]);
    }
}

pbio_error_t pbsys_stdin_get_char(uint8_t *c) {
    if (stdin_buf_head == stdin_buf_tail) {
        return PBIO_ERROR_AGAIN;
    }

    *c = stdin_buf[stdin_buf_tail];
    stdin_buf_tail = (stdin_buf_tail + 1) & (STDIN_BUF_SIZE - 1);

    return PBIO_SUCCESS;
}

pbio_error_t pbsys_stdout_put_char(uint8_t c) {
    return pbdrv_bluetooth_tx(c);
}

void pbsys_reboot(bool fw_update) {
    if (fw_update) {
        bootloader_magic_addr = BOOTLOADER_MAGIC_VALUE;
    }
    // this function never returns
    NVIC_SystemReset();
}

void pbsys_power_off(void) {
    int i;

    // blink pattern like LEGO firmware
    for (i = 0; i < 3; i++) {
        pbdrv_light_set_rgb(PBIO_PORT_SELF, 255, 140, 60); // white
        clock_delay_usec(50000);
        pbdrv_light_set_rgb(PBIO_PORT_SELF, 0, 0, 0);
        clock_delay_usec(30000);
    }

    // PWM doesn't work while IRQs are disabled? so this needs to be after
    __disable_irq();

    // need to loop because power will stay on as long as button is pressed
    while (true) {
        // setting PB11 low cuts the power
        GPIOB->BRR = GPIO_BRR_BR_11;
    }
}

static void init(void) {
    uint16_t battery_voltage;
    uint8_t r, g, b;

    pbdrv_battery_get_voltage_now(PBIO_PORT_SELF, &battery_voltage);
    avg_battery_voltage = battery_voltage;

    _pbio_light_set_user_mode(false);
    pbdrv_light_get_rgb_for_color(PBIO_PORT_SELF, PBIO_LIGHT_COLOR_BLUE, &r, &g, &b);
    pbdrv_light_set_rgb(PBIO_PORT_SELF, r, g, b);
}

static void update_button(clock_time_t now) {
    pbio_button_flags_t btn;

    pbio_button_is_pressed(PBIO_PORT_SELF, &btn);

    if (btn & PBIO_BUTTON_CENTER) {
        if (button_pressed) {

            // if the button is held down for 5 seconds, power off
            if (now - button_press_start_time > clock_from_msec(5000)) {
                // turn off light briefly like official LEGO firmware
                pbdrv_light_set_rgb(PBIO_PORT_SELF, 0, 0, 0);
                for (int i = 0; i < 10; i++) {
                    clock_delay_usec(58000);
                }

                pbsys_power_off();
            }
        }
        else {
            button_press_start_time = now;
            button_pressed = true;
            led_status_flags |= LED_STATUS_BUTTON_PRESSED;
            if (user_stop_func) {
                user_stop_func();
            }
        }
    }
    else {
        button_pressed = false;
        led_status_flags &= ~LED_STATUS_BUTTON_PRESSED;
    }
}

static void update_battery(clock_time_t now) {
    uint32_t poll_interval;
    uint16_t battery_voltage;

    poll_interval = clock_to_msec(now - prev_poll_time);
    prev_poll_time = now;

    pbdrv_battery_get_voltage_now(PBIO_PORT_SELF, &battery_voltage);

    avg_battery_voltage = (avg_battery_voltage * (BATTERY_PERIOD_MS - poll_interval)
        + battery_voltage * poll_interval) / BATTERY_PERIOD_MS;

    if (avg_battery_voltage <= BATTERY_CRITICAL_MV) {
        // don't want to damage rechargeable batteries
        pbsys_power_off();
    }

    if (avg_battery_voltage <= BATTERY_LOW_MV) {
        led_status_flags |= LED_STATUS_BATTERY_LOW;
    }
    else if (avg_battery_voltage >= BATTERY_OK_MV) {
        led_status_flags &= ~LED_STATUS_BATTERY_LOW;
    }
}

static void handle_stdin_char(uint8_t c) {
    uint8_t new_head = (stdin_buf_head + 1) & (STDIN_BUF_SIZE - 1);

    // optional hook function can steal the character
    if (user_stdin_event_func && user_stdin_event_func(c)) {
        return;
    }

    // otherwise write character to ring buffer

    if (new_head == stdin_buf_tail) {
        // overflow. drop the data :-(
        return;
    }
    stdin_buf[stdin_buf_head] = c;
    stdin_buf_head = new_head;
}

PROCESS_THREAD(pbsys_process, ev, data) {
    static struct etimer timer;

    PROCESS_BEGIN();

    init();
    etimer_set(&timer, clock_from_msec(50));

    while (true) {
        PROCESS_WAIT_EVENT();
        if (ev == PROCESS_EVENT_TIMER && etimer_expired(&timer)) {
            clock_time_t now = clock_time();
            etimer_reset(&timer);
            update_button(now);
            update_battery(now);
        }
        else if (ev == PBIO_EVENT_UART_RX) {
            pbio_event_uart_rx_data_t rx = { .data = data };
            handle_stdin_char(rx.byte);
        }
        else if (ev == PBIO_EVENT_COM_CMD) {
            pbio_com_cmd_t cmd = (uint32_t)data;

            switch (cmd) {
            case PBIO_COM_CMD_START_USER_PROGRAM:
                break;
            case PBIO_COM_CMD_STOP_USER_PROGRAM:
                if (user_stop_func) {
                    user_stop_func();
                }
                break;
            }
        }
    }

    PROCESS_END();
}

// this seem to be missing from the header file
#ifndef RCC_CFGR3_ADCSW
#define RCC_CFGR3_ADCSW (1 << 8)
#endif

// special memory addresses defined in linker script
extern uint32_t _fw_isr_vector_src[48];
extern uint32_t _fw_isr_vector_dst[48];

// Called from assembly code in startup_stm32f070xb.s
// this function is a mash up of ports/stm32/system_stm32f0.c from MicroPython
// and the official LEGO firmware
void SystemInit(void) {
    // normally, the system clock would be setup here, but it is already
    // configured by the bootloader, so no need to do it again.

    // dpgeorge: enable 8-byte stack alignment for IRQ handlers, in accord with EABI
    SCB->CCR |= SCB_CCR_STKALIGN_Msk;

    // Enable all of the shared hardware modules we are using

    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN
                |  RCC_AHBENR_GPIODEN | RCC_AHBENR_GPIOFEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;


    // Keep BOOST alive
    GPIOB->BSRR = GPIO_BSRR_BS_11;
    GPIOB->MODER = (GPIOB->MODER & ~GPIO_MODER_MODER11_Msk) | (1 << GPIO_MODER_MODER11_Pos);

    // not sure what the rest of these pins do

    // PF0 output, high
    GPIOF->BSRR = GPIO_BSRR_BS_0;
    GPIOF->MODER = (GPIOF->MODER & ~GPIO_MODER_MODER0_Msk) | (1 << GPIO_MODER_MODER0_Pos);

    // PA15 output, high
    GPIOA->BSRR = GPIO_BSRR_BS_15;
    GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER15_Msk) | (1 << GPIO_MODER_MODER15_Pos);

    // PB5 output, high
    GPIOB->BSRR = GPIO_BSRR_BS_5;
    GPIOB->MODER = (GPIOB->MODER & ~GPIO_MODER_MODER5_Msk) | (1 << GPIO_MODER_MODER5_Pos);

    // PC12 output, high
    GPIOC->BSRR = GPIO_BSRR_BS_12;
    GPIOC->MODER = (GPIOC->MODER & ~GPIO_MODER_MODER12_Msk) | (1 << GPIO_MODER_MODER12_Pos);

    // PD2 output, high
    GPIOD->BSRR = GPIO_BSRR_BS_2;
    GPIOD->MODER = (GPIOD->MODER & ~GPIO_MODER_MODER2_Msk) | (1 << GPIO_MODER_MODER2_Pos);

    // PF1 output, high
    GPIOF->BSRR = GPIO_BSRR_BS_1;
    GPIOF->MODER = (GPIOF->MODER & ~GPIO_MODER_MODER1_Msk) | (1 << GPIO_MODER_MODER1_Pos);

    // since the firmware starts at 0x08005000, we need to relocate the
    // interrupt vector table to a place where the CPU knows about it.
    // The space at the start of SRAM is reserved in via the linker script.
    memcpy(_fw_isr_vector_dst, _fw_isr_vector_src, sizeof(_fw_isr_vector_dst));

    // this maps SRAM to 0x00000000
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE_Msk) | (3 << SYSCFG_CFGR1_MEM_MODE_Pos);
}
