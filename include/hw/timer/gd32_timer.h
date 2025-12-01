/*
 * GD32 Timer Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the GD32C103 TIMER peripherals:
 * - Advanced timers: TIMER0, TIMER7 (4 channels, complementary outputs)
 * - General L0 timers: TIMER1-4 (4 channels)
 * - General L1 timers: TIMER8, TIMER11 (2 channels)
 * - General L2 timers: TIMER9, TIMER10, TIMER12, TIMER13 (1 channel)
 * - Basic timers: TIMER5, TIMER6 (no channels)
 */

#ifndef HW_GD32_TIMER_H
#define HW_GD32_TIMER_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

/* GD32 TIMER Register Offsets */
#define GD32_TIMER_CTL0     0x00    /* Control register 0 */
#define GD32_TIMER_CTL1     0x04    /* Control register 1 */
#define GD32_TIMER_SMCFG    0x08    /* Slave mode configuration */
#define GD32_TIMER_DMAINTEN 0x0C    /* DMA/interrupt enable */
#define GD32_TIMER_INTF     0x10    /* Interrupt flag */
#define GD32_TIMER_SWEVG    0x14    /* Software event generation */
#define GD32_TIMER_CHCTL0   0x18    /* Channel control 0 */
#define GD32_TIMER_CHCTL1   0x1C    /* Channel control 1 */
#define GD32_TIMER_CHCTL2   0x20    /* Channel control 2 */
#define GD32_TIMER_CNT      0x24    /* Counter */
#define GD32_TIMER_PSC      0x28    /* Prescaler */
#define GD32_TIMER_CAR      0x2C    /* Counter auto reload */
#define GD32_TIMER_CREP     0x30    /* Counter repetition */
#define GD32_TIMER_CH0CV    0x34    /* Channel 0 compare value */
#define GD32_TIMER_CH1CV    0x38    /* Channel 1 compare value */
#define GD32_TIMER_CH2CV    0x3C    /* Channel 2 compare value */
#define GD32_TIMER_CH3CV    0x40    /* Channel 3 compare value */
#define GD32_TIMER_CCHP     0x44    /* Complementary channel protection */
#define GD32_TIMER_DMACFG   0x48    /* DMA configuration */
#define GD32_TIMER_DMATB    0x4C    /* DMA transfer buffer */
#define GD32_TIMER_CFG      0xFC    /* Configuration */

/* CTL0 bits */
#define GD32_TIMER_CTL0_CEN     (1 << 0)    /* Counter enable */
#define GD32_TIMER_CTL0_UPDIS   (1 << 1)    /* Update disable */
#define GD32_TIMER_CTL0_UPS     (1 << 2)    /* Update source */
#define GD32_TIMER_CTL0_SPM     (1 << 3)    /* Single pulse mode */
#define GD32_TIMER_CTL0_DIR     (1 << 4)    /* Direction (0=up, 1=down) */
#define GD32_TIMER_CTL0_CAM     (3 << 5)    /* Center-aligned mode */
#define GD32_TIMER_CTL0_ARSE    (1 << 7)    /* Auto-reload shadow enable */
#define GD32_TIMER_CTL0_CKDIV   (3 << 8)    /* Clock division */

/* DMAINTEN bits (interrupt enable) */
#define GD32_TIMER_DMAINTEN_UPIE    (1 << 0)    /* Update interrupt enable */
#define GD32_TIMER_DMAINTEN_CH0IE   (1 << 1)    /* Channel 0 interrupt enable */
#define GD32_TIMER_DMAINTEN_CH1IE   (1 << 2)    /* Channel 1 interrupt enable */
#define GD32_TIMER_DMAINTEN_CH2IE   (1 << 3)    /* Channel 2 interrupt enable */
#define GD32_TIMER_DMAINTEN_CH3IE   (1 << 4)    /* Channel 3 interrupt enable */
#define GD32_TIMER_DMAINTEN_CMTIE   (1 << 5)    /* Commutation interrupt enable */
#define GD32_TIMER_DMAINTEN_TRGIE   (1 << 6)    /* Trigger interrupt enable */
#define GD32_TIMER_DMAINTEN_BRKIE   (1 << 7)    /* Break interrupt enable */

/* INTF bits (interrupt flags) */
#define GD32_TIMER_INTF_UPIF    (1 << 0)    /* Update interrupt flag */
#define GD32_TIMER_INTF_CH0IF   (1 << 1)    /* Channel 0 interrupt flag */
#define GD32_TIMER_INTF_CH1IF   (1 << 2)    /* Channel 1 interrupt flag */
#define GD32_TIMER_INTF_CH2IF   (1 << 3)    /* Channel 2 interrupt flag */
#define GD32_TIMER_INTF_CH3IF   (1 << 4)    /* Channel 3 interrupt flag */
#define GD32_TIMER_INTF_CMTIF   (1 << 5)    /* Commutation interrupt flag */
#define GD32_TIMER_INTF_TRGIF   (1 << 6)    /* Trigger interrupt flag */
#define GD32_TIMER_INTF_BRKIF   (1 << 7)    /* Break interrupt flag */

/* SWEVG bits (software event generation) */
#define GD32_TIMER_SWEVG_UPG    (1 << 0)    /* Update event generate */

/* CCHP bits (for advanced timers) */
#define GD32_TIMER_CCHP_POEN    (1 << 15)   /* Primary output enable */

/* Timer types */
typedef enum {
    GD32_TIMER_TYPE_ADVANCED,   /* TIMER0, TIMER7 */
    GD32_TIMER_TYPE_GENERAL_L0, /* TIMER1-4 */
    GD32_TIMER_TYPE_GENERAL_L1, /* TIMER8, TIMER11 */
    GD32_TIMER_TYPE_GENERAL_L2, /* TIMER9, TIMER10, TIMER12, TIMER13 */
    GD32_TIMER_TYPE_BASIC,      /* TIMER5, TIMER6 */
} Gd32TimerType;

#define TYPE_GD32_TIMER "gd32-timer"
OBJECT_DECLARE_SIMPLE_TYPE(GD32TimerState, GD32_TIMER)

struct GD32TimerState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    QEMUTimer *timer;
    qemu_irq irq;

    /* Timer type configuration */
    Gd32TimerType timer_type;
    char *name;

    /* Timing state */
    int64_t tick_offset;
    uint64_t hit_time;
    uint64_t freq_hz;

    /* Registers */
    uint32_t ctl0;      /* Control register 0 */
    uint32_t ctl1;      /* Control register 1 */
    uint32_t smcfg;     /* Slave mode configuration */
    uint32_t dmainten;  /* DMA/interrupt enable */
    uint32_t intf;      /* Interrupt flag */
    uint32_t chctl0;    /* Channel control 0 */
    uint32_t chctl1;    /* Channel control 1 */
    uint32_t chctl2;    /* Channel control 2 */
    uint32_t cnt;       /* Counter (shadow for read) */
    uint32_t psc;       /* Prescaler */
    uint32_t car;       /* Counter auto reload */
    uint32_t crep;      /* Counter repetition (advanced only) */
    uint32_t ch0cv;     /* Channel 0 compare value */
    uint32_t ch1cv;     /* Channel 1 compare value */
    uint32_t ch2cv;     /* Channel 2 compare value */
    uint32_t ch3cv;     /* Channel 3 compare value */
    uint32_t cchp;      /* Complementary channel protection (advanced) */
    uint32_t dmacfg;    /* DMA configuration */
    uint32_t dmatb;     /* DMA transfer buffer */
    uint32_t cfg;       /* Configuration */
};

#endif /* HW_GD32_TIMER_H */

