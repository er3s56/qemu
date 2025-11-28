/*
 * GD32 USART
 *
 * Copyright (c) 2025
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * GD32C10x USART register layout (different from STM32F2xx):
 * - STAT0: 0x00 (Status Register 0)
 * - DATA:  0x04 (Data Register)
 * - BAUD:  0x08 (Baud Rate Register)
 * - CTL0:  0x0C (Control Register 0)
 * - CTL1:  0x10 (Control Register 1)
 * - CTL2:  0x14 (Control Register 2)
 * - GP:    0x18 (Guard Time and Prescaler Register)
 */

#ifndef HW_GD32_USART_H
#define HW_GD32_USART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

/* GD32 USART Register Offsets */
#define GD32_USART_STAT0  0x00  /* Status register 0 */
#define GD32_USART_DATA   0x04  /* Data register */
#define GD32_USART_BAUD   0x08  /* Baud rate register */
#define GD32_USART_CTL0   0x0C  /* Control register 0 */
#define GD32_USART_CTL1   0x10  /* Control register 1 */
#define GD32_USART_CTL2   0x14  /* Control register 2 */
#define GD32_USART_GP     0x18  /* Guard time and prescaler register */

/* STAT0 register bits */
#define GD32_USART_STAT0_PERR   (1 << 0)   /* Parity error flag */
#define GD32_USART_STAT0_FERR   (1 << 1)   /* Frame error flag */
#define GD32_USART_STAT0_NERR   (1 << 2)   /* Noise error flag */
#define GD32_USART_STAT0_ORERR  (1 << 3)   /* Overrun error */
#define GD32_USART_STAT0_IDLEF  (1 << 4)   /* IDLE frame detected flag */
#define GD32_USART_STAT0_RBNE   (1 << 5)   /* Read data buffer not empty */
#define GD32_USART_STAT0_TC     (1 << 6)   /* Transmission complete */
#define GD32_USART_STAT0_TBE    (1 << 7)   /* Transmit data buffer empty */
#define GD32_USART_STAT0_LBDF   (1 << 8)   /* LIN break detected flag */
#define GD32_USART_STAT0_CTSF   (1 << 9)   /* CTS change flag */

/* Reset value: TBE and TC are set after reset */
#define GD32_USART_STAT0_RESET  (GD32_USART_STAT0_TBE | GD32_USART_STAT0_TC)

/* CTL0 register bits */
#define GD32_USART_CTL0_SBKCMD  (1 << 0)   /* Send break command */
#define GD32_USART_CTL0_RWU     (1 << 1)   /* Receiver wakeup from mute mode */
#define GD32_USART_CTL0_REN     (1 << 2)   /* Receiver enable */
#define GD32_USART_CTL0_TEN     (1 << 3)   /* Transmitter enable */
#define GD32_USART_CTL0_IDLEIE  (1 << 4)   /* IDLE interrupt enable */
#define GD32_USART_CTL0_RBNEIE  (1 << 5)   /* RBNE interrupt enable */
#define GD32_USART_CTL0_TCIE    (1 << 6)   /* TC interrupt enable */
#define GD32_USART_CTL0_TBEIE   (1 << 7)   /* TBE interrupt enable */
#define GD32_USART_CTL0_PERRIE  (1 << 8)   /* Parity error interrupt enable */
#define GD32_USART_CTL0_PM      (1 << 9)   /* Parity mode */
#define GD32_USART_CTL0_PCEN    (1 << 10)  /* Parity check enable */
#define GD32_USART_CTL0_WM      (1 << 11)  /* Wakeup method */
#define GD32_USART_CTL0_WL      (1 << 12)  /* Word length */
#define GD32_USART_CTL0_UEN     (1 << 13)  /* USART enable */

#define TYPE_GD32_USART "gd32-usart"
OBJECT_DECLARE_SIMPLE_TYPE(GD32UsartState, GD32_USART)

struct GD32UsartState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /* Registers */
    uint32_t stat0;   /* Status register 0 */
    uint32_t data;    /* Data register (only lower 9 bits used) */
    uint32_t baud;    /* Baud rate register */
    uint32_t ctl0;    /* Control register 0 */
    uint32_t ctl1;    /* Control register 1 */
    uint32_t ctl2;    /* Control register 2 */
    uint32_t gp;      /* Guard time and prescaler register */

    /* Character device backend */
    CharBackend chr;

    /* Interrupt output */
    qemu_irq irq;
};

#endif /* HW_GD32_USART_H */

