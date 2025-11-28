/*
 * GD32 RCU (Reset and Clock Unit)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GD32C10x RCU register layout and clock control emulation.
 */

#ifndef HW_GD32_RCU_H
#define HW_GD32_RCU_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* RCU Register Offsets */
#define GD32_RCU_CTL        0x00    /* Control register */
#define GD32_RCU_CFG0       0x04    /* Clock configuration register 0 */
#define GD32_RCU_INT        0x08    /* Clock interrupt register */
#define GD32_RCU_APB2RST    0x0C    /* APB2 reset register */
#define GD32_RCU_APB1RST    0x10    /* APB1 reset register */
#define GD32_RCU_AHBEN      0x14    /* AHB enable register */
#define GD32_RCU_APB2EN     0x18    /* APB2 enable register */
#define GD32_RCU_APB1EN     0x1C    /* APB1 enable register */
#define GD32_RCU_BDCTL      0x20    /* Backup domain control register */
#define GD32_RCU_RSTSCK     0x24    /* Reset source/clock register */
#define GD32_RCU_AHBRST     0x28    /* AHB reset register */
#define GD32_RCU_CFG1       0x2C    /* Clock configuration register 1 */
#define GD32_RCU_DSV        0x34    /* Deep-sleep mode voltage register */
#define GD32_RCU_ADDCTL     0xC0    /* Additional clock control register */
#define GD32_RCU_ADDINT     0xCC    /* Additional clock interrupt register */
#define GD32_RCU_ADDAPB1RST 0xE0    /* APB1 additional reset register */
#define GD32_RCU_ADDAPB1EN  0xE4    /* APB1 additional enable register */

/* CTL register bits */
#define GD32_RCU_CTL_IRC8MEN    (1 << 0)    /* IRC8M enable */
#define GD32_RCU_CTL_IRC8MSTB   (1 << 1)    /* IRC8M stabilization flag */
#define GD32_RCU_CTL_HXTALEN    (1 << 16)   /* HXTAL enable */
#define GD32_RCU_CTL_HXTALSTB   (1 << 17)   /* HXTAL stabilization flag */
#define GD32_RCU_CTL_HXTALBPS   (1 << 18)   /* HXTAL bypass */
#define GD32_RCU_CTL_CKMEN      (1 << 19)   /* Clock monitor enable */
#define GD32_RCU_CTL_PLLEN      (1 << 24)   /* PLL enable */
#define GD32_RCU_CTL_PLLSTB     (1 << 25)   /* PLL stabilization flag */
#define GD32_RCU_CTL_PLL1EN     (1 << 26)   /* PLL1 enable */
#define GD32_RCU_CTL_PLL1STB    (1 << 27)   /* PLL1 stabilization flag */
#define GD32_RCU_CTL_PLL2EN     (1 << 28)   /* PLL2 enable */
#define GD32_RCU_CTL_PLL2STB    (1 << 29)   /* PLL2 stabilization flag */

/* CFG0 register bits */
#define GD32_RCU_CFG0_SCS_MASK  (0x3 << 0)  /* System clock switch */
#define GD32_RCU_CFG0_SCSS_MASK (0x3 << 2)  /* System clock switch status */
#define GD32_RCU_CFG0_SCSS_SHIFT 2

/* System clock source values */
#define GD32_RCU_SCS_IRC8M      0x0
#define GD32_RCU_SCS_HXTAL      0x1
#define GD32_RCU_SCS_PLL        0x2

/* RSTSCK register bits */
#define GD32_RCU_RSTSCK_IRC40KEN    (1 << 0)    /* IRC40K enable */
#define GD32_RCU_RSTSCK_IRC40KSTB   (1 << 1)    /* IRC40K stabilization flag */

/* BDCTL register bits */
#define GD32_RCU_BDCTL_LXTALEN      (1 << 0)    /* LXTAL enable */
#define GD32_RCU_BDCTL_LXTALSTB     (1 << 1)    /* LXTAL stabilization flag */

/* ADDCTL register bits */
#define GD32_RCU_ADDCTL_IRC48MEN    (1 << 16)   /* IRC48M enable */
#define GD32_RCU_ADDCTL_IRC48MSTB   (1 << 17)   /* IRC48M stabilization flag */

/* Number of registers (up to 0xE4, with gaps) */
#define GD32_RCU_REGS_SIZE  0x100

#define GD32_RCU_PERIPHERAL_SIZE 0x400

#define TYPE_GD32_RCU "gd32-rcu"
OBJECT_DECLARE_SIMPLE_TYPE(GD32RcuState, GD32_RCU)

struct GD32RcuState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /* Registers */
    uint32_t ctl;       /* Control register */
    uint32_t cfg0;      /* Clock configuration register 0 */
    uint32_t intr;      /* Interrupt register */
    uint32_t apb2rst;   /* APB2 reset register */
    uint32_t apb1rst;   /* APB1 reset register */
    uint32_t ahben;     /* AHB enable register */
    uint32_t apb2en;    /* APB2 enable register */
    uint32_t apb1en;    /* APB1 enable register */
    uint32_t bdctl;     /* Backup domain control register */
    uint32_t rstsck;    /* Reset source/clock register */
    uint32_t ahbrst;    /* AHB reset register */
    uint32_t cfg1;      /* Clock configuration register 1 */
    uint32_t dsv;       /* Deep-sleep voltage register */
    uint32_t addctl;    /* Additional clock control register */
    uint32_t addint;    /* Additional clock interrupt register */
    uint32_t addapb1rst;/* APB1 additional reset register */
    uint32_t addapb1en; /* APB1 additional enable register */
};

#endif /* HW_GD32_RCU_H */

