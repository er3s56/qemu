/*
 * GD32 FMC (Flash Memory Controller)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GD32C10x FMC register layout and Flash programming emulation.
 */

#ifndef HW_GD32_FMC_H
#define HW_GD32_FMC_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* FMC Register Offsets */
#define GD32_FMC_REG_WS         0x00    /* Wait state register */
#define GD32_FMC_REG_KEY        0x04    /* Unlock key register */
#define GD32_FMC_REG_OBKEY      0x08    /* Option bytes unlock key register */
#define GD32_FMC_REG_STAT       0x0C    /* Status register */
#define GD32_FMC_REG_CTL        0x10    /* Control register */
#define GD32_FMC_REG_ADDR       0x14    /* Address register */
#define GD32_FMC_REG_OBSTAT     0x1C    /* Option bytes status register */
#define GD32_FMC_REG_WP         0x20    /* Write protection register */
#define GD32_FMC_REG_PID        0x100   /* Product ID register */

/* FMC_WS bits */
#define GD32_FMC_WS_WSCNT_MASK  (0x7 << 0)   /* Wait state counter */
#define GD32_FMC_WS_PFEN        (1 << 4)     /* Pre-fetch enable */
#define GD32_FMC_WS_ICEN        (1 << 9)     /* IBUS cache enable */
#define GD32_FMC_WS_DCEN        (1 << 10)    /* DBUS cache enable */
#define GD32_FMC_WS_ICRST       (1 << 11)    /* IBUS cache reset */
#define GD32_FMC_WS_DCRST       (1 << 12)    /* DBUS cache reset */
#define GD32_FMC_WS_PGW         (1 << 15)    /* Program width */

/* FMC_STAT bits */
#define GD32_FMC_STAT_BUSY      (1 << 0)     /* Flash busy flag */
#define GD32_FMC_STAT_PGERR     (1 << 2)     /* Program error flag */
#define GD32_FMC_STAT_PGAERR    (1 << 3)     /* Program alignment error */
#define GD32_FMC_STAT_WPERR     (1 << 4)     /* Write protection error */
#define GD32_FMC_STAT_ENDF      (1 << 5)     /* End of operation flag */

/* FMC_CTL bits */
#define GD32_FMC_CTL_PG         (1 << 0)     /* Program command */
#define GD32_FMC_CTL_PER        (1 << 1)     /* Page erase command */
#define GD32_FMC_CTL_MER        (1 << 2)     /* Mass erase command */
#define GD32_FMC_CTL_OBPG       (1 << 4)     /* Option bytes program */
#define GD32_FMC_CTL_OBER       (1 << 5)     /* Option bytes erase */
#define GD32_FMC_CTL_START      (1 << 6)     /* Start erase command */
#define GD32_FMC_CTL_LK         (1 << 7)     /* Lock bit */
#define GD32_FMC_CTL_OBWEN      (1 << 9)     /* Option bytes write enable */
#define GD32_FMC_CTL_ERRIE      (1 << 10)    /* Error interrupt enable */
#define GD32_FMC_CTL_ENDIE      (1 << 12)    /* End of operation interrupt enable */

/* Unlock keys */
#define GD32_FMC_UNLOCK_KEY0    0x45670123U
#define GD32_FMC_UNLOCK_KEY1    0xCDEF89ABU

/* Flash parameters */
#define GD32_FMC_PAGE_SIZE      1024         /* 1KB page size */

#define GD32_FMC_PERIPHERAL_SIZE 0x400

#define TYPE_GD32_FMC "gd32-fmc"
OBJECT_DECLARE_SIMPLE_TYPE(GD32FmcState, GD32_FMC)

struct GD32FmcState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /* Registers */
    uint32_t ws;        /* Wait state register */
    uint32_t stat;      /* Status register */
    uint32_t ctl;       /* Control register */
    uint32_t addr;      /* Address register */
    uint32_t obstat;    /* Option bytes status register */
    uint32_t wp;        /* Write protection register */

    /* Unlock state machine */
    uint8_t unlock_state;   /* 0: locked, 1: key0 received, 2: unlocked */

    /* Link to Flash memory region for programming */
    MemoryRegion *flash_mr;
    uint32_t flash_base;
    uint32_t flash_size;
};

#endif /* HW_GD32_FMC_H */

