/*
 * GD32 AFIO (Alternate Function I/O)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GD32C10x AFIO register layout for pin remapping and EXTI configuration.
 * Note: Actual remapping functionality is not implemented; registers
 * are stored but do not affect peripheral behavior.
 */

#ifndef HW_GD32_AFIO_H
#define HW_GD32_AFIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* AFIO Register Offsets */
#define GD32_AFIO_EC        0x00    /* Event control register */
#define GD32_AFIO_PCF0      0x04    /* Port configuration register 0 */
#define GD32_AFIO_EXTISS0   0x08    /* EXTI sources selection register 0 */
#define GD32_AFIO_EXTISS1   0x0C    /* EXTI sources selection register 1 */
#define GD32_AFIO_EXTISS2   0x10    /* EXTI sources selection register 2 */
#define GD32_AFIO_EXTISS3   0x14    /* EXTI sources selection register 3 */
#define GD32_AFIO_PCF1      0x1C    /* Port configuration register 1 */
#define GD32_AFIO_CPSCTL    0x20    /* I/O compensation control register */

/* EC register bits */
#define GD32_AFIO_EC_PIN_MASK   (0xF << 0)  /* Event output pin selection */
#define GD32_AFIO_EC_PORT_MASK  (0x7 << 4)  /* Event output port selection */
#define GD32_AFIO_EC_EOE        (1 << 7)    /* Event output enable */

/* CPSCTL register bits */
#define GD32_AFIO_CPSCTL_CPS_EN     (1 << 0)    /* I/O compensation cell enable */
#define GD32_AFIO_CPSCTL_CPS_RDY    (1 << 8)    /* I/O compensation cell ready */

#define GD32_AFIO_PERIPHERAL_SIZE   0x400

#define TYPE_GD32_AFIO "gd32-afio"
OBJECT_DECLARE_SIMPLE_TYPE(GD32AfioState, GD32_AFIO)

struct GD32AfioState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /* Registers */
    uint32_t ec;        /* Event control */
    uint32_t pcf0;      /* Port configuration 0 */
    uint32_t extiss0;   /* EXTI source selection 0 */
    uint32_t extiss1;   /* EXTI source selection 1 */
    uint32_t extiss2;   /* EXTI source selection 2 */
    uint32_t extiss3;   /* EXTI source selection 3 */
    uint32_t pcf1;      /* Port configuration 1 */
    uint32_t cpsctl;    /* Compensation control */
};

#endif /* HW_GD32_AFIO_H */

