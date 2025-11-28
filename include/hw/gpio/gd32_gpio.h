/*
 * GD32 GPIO (General Purpose I/O)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GD32C10x GPIO register layout and basic I/O emulation.
 */

#ifndef HW_GD32_GPIO_H
#define HW_GD32_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* GPIO Register Offsets */
#define GD32_GPIO_CTL0      0x00    /* Control register 0 (Pin 0-7) */
#define GD32_GPIO_CTL1      0x04    /* Control register 1 (Pin 8-15) */
#define GD32_GPIO_ISTAT     0x08    /* Input status register */
#define GD32_GPIO_OCTL      0x0C    /* Output control register */
#define GD32_GPIO_BOP       0x10    /* Bit operation register */
#define GD32_GPIO_BC        0x14    /* Bit clear register */
#define GD32_GPIO_LOCK      0x18    /* Configuration lock register */
#define GD32_GPIO_SPD       0x3C    /* Port bit speed register */

/* LOCK register bits */
#define GD32_GPIO_LOCK_LKK  (1 << 16)   /* Lock key */

/* Number of pins per GPIO port */
#define GD32_GPIO_NUM_PINS  16

#define GD32_GPIO_PERIPHERAL_SIZE   0x400

#define TYPE_GD32_GPIO "gd32-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(GD32GpioState, GD32_GPIO)

struct GD32GpioState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /* Port name for debug messages */
    char *name;

    /* Registers */
    uint32_t ctl0;      /* Control register 0 */
    uint32_t ctl1;      /* Control register 1 */
    uint32_t istat;     /* Input status */
    uint32_t octl;      /* Output control */
    uint32_t lock;      /* Configuration lock */
    uint32_t spd;       /* Speed register */

    /* Lock sequence state */
    uint8_t lock_state;

    /* External input lines (directly connected) */
    uint16_t input;

    /* Output IRQs for connecting to other devices */
    qemu_irq output[GD32_GPIO_NUM_PINS];
};

#endif /* HW_GD32_GPIO_H */

