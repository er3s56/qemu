/*
 * GD32C103 SoC Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GD32C103RBT6 Configuration:
 * - CPU: Cortex-M4 with FPU
 * - Flash: 128KB
 * - SRAM: 32KB
 */

#ifndef HW_ARM_GD32C103_SOC_H
#define HW_ARM_GD32C103_SOC_H

#include "hw/arm/armv7m.h"
#include "hw/char/gd32_usart.h"
#include "hw/misc/gd32_rcu.h"
#include "hw/misc/gd32_fmc.h"
#include "hw/misc/gd32_afio.h"
#include "hw/gpio/gd32_gpio.h"
#include "hw/timer/gd32_timer.h"
#include "hw/net/can/gd32_can.h"
#include "hw/i2c/gd32_i2c.h"
#include "net/can_emu.h"
#include "qom/object.h"

#define TYPE_GD32C103_SOC "gd32c103-soc"
OBJECT_DECLARE_SIMPLE_TYPE(GD32C103State, GD32C103_SOC)

/* Number of peripherals */
#define GD32_NUM_USARTS 5   /* USART0, USART1, USART2, UART3, UART4 */
#define GD32_NUM_GPIOS  5   /* GPIOA, GPIOB, GPIOC, GPIOD, GPIOE */
#define GD32_NUM_TIMERS 14  /* TIMER0-13 */
#define GD32_NUM_CANS   2   /* CAN0, CAN1 */
#define GD32_NUM_I2CS   2   /* I2C0, I2C1 */

/* Memory Map - GD32C103RBT6 */
#define GD32_FLASH_BASE     0x08000000
#define GD32_FLASH_SIZE     (128 * 1024)  /* 128KB for RBT6 variant */
#define GD32_SRAM_BASE      0x20000000
#define GD32_SRAM_SIZE      (32 * 1024)   /* 32KB */

struct GD32C103State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMv7MState armv7m;

    /* RCU (Reset and Clock Unit) */
    GD32RcuState rcu;

    /* FMC (Flash Memory Controller) */
    GD32FmcState fmc;

    /* AFIO (Alternate Function I/O) */
    GD32AfioState afio;

    /* GPIO ports: GPIOA, GPIOB, GPIOC, GPIOD, GPIOE */
    GD32GpioState gpio[GD32_NUM_GPIOS];

    /* USART/UART peripherals: USART0, USART1, USART2, UART3, UART4 */
    GD32UsartState usart[GD32_NUM_USARTS];

    /* TIMER peripherals: TIMER0-13 */
    GD32TimerState timer[GD32_NUM_TIMERS];

    /* CAN peripherals: CAN0, CAN1 */
    GD32CanState can[GD32_NUM_CANS];

    /* I2C peripherals: I2C0, I2C1 */
    GD32I2CState i2c[GD32_NUM_I2CS];

    /* Memory regions */
    MemoryRegion flash;
    MemoryRegion flash_alias;
    MemoryRegion sram;

    /* Clocks */
    Clock *sysclk;
    Clock *refclk;

    /* CAN bus connections (optional, for SocketCAN integration) */
    CanBusState *canbus[GD32_NUM_CANS];

    /* Flash data file for persistence (optional) */
    char *flash_file;
};

#endif /* HW_ARM_GD32C103_SOC_H */
