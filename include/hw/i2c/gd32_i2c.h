/*
 * GD32 I2C Controller Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the GD32C10x I2C controller with:
 * - Master and slave mode
 * - 7-bit and 10-bit addressing
 * - Standard mode (100kHz) and fast mode (400kHz)
 * - Event and error interrupts
 */

#ifndef HW_GD32_I2C_H
#define HW_GD32_I2C_H

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

/* GD32 I2C Register Offsets */
#define GD32_I2C_CTL0       0x00    /* Control register 0 */
#define GD32_I2C_CTL1       0x04    /* Control register 1 */
#define GD32_I2C_SADDR0     0x08    /* Slave address register 0 */
#define GD32_I2C_SADDR1     0x0C    /* Slave address register 1 */
#define GD32_I2C_DATA       0x10    /* Data register */
#define GD32_I2C_STAT0      0x14    /* Status register 0 */
#define GD32_I2C_STAT1      0x18    /* Status register 1 */
#define GD32_I2C_CKCFG      0x1C    /* Clock configuration register */
#define GD32_I2C_RT         0x20    /* Rise time register */
#define GD32_I2C_SAMCS      0x80    /* SAM control and status register */
#define GD32_I2C_FMPCFG     0x90    /* Fast mode plus configuration */

/* CTL0 register bits */
#define GD32_I2C_CTL0_I2CEN     (1 << 0)    /* I2C peripheral enable */
#define GD32_I2C_CTL0_SMBEN     (1 << 1)    /* SMBus mode enable */
#define GD32_I2C_CTL0_SMBSEL    (1 << 3)    /* SMBus type selection */
#define GD32_I2C_CTL0_ARPEN     (1 << 4)    /* ARP enable */
#define GD32_I2C_CTL0_PECEN     (1 << 5)    /* PEC enable */
#define GD32_I2C_CTL0_GCEN      (1 << 6)    /* General call enable */
#define GD32_I2C_CTL0_SS        (1 << 7)    /* Clock stretching disable */
#define GD32_I2C_CTL0_START     (1 << 8)    /* Start generation */
#define GD32_I2C_CTL0_STOP      (1 << 9)    /* Stop generation */
#define GD32_I2C_CTL0_ACKEN     (1 << 10)   /* ACK enable */
#define GD32_I2C_CTL0_POAP      (1 << 11)   /* ACK/PEC position */
#define GD32_I2C_CTL0_PECTRANS  (1 << 12)   /* PEC transfer */
#define GD32_I2C_CTL0_SALT      (1 << 13)   /* SMBus alert */
#define GD32_I2C_CTL0_SRESET    (1 << 15)   /* Software reset */

/* CTL1 register bits */
#define GD32_I2C_CTL1_I2CCLK_MASK   0x3F    /* Peripheral clock frequency */
#define GD32_I2C_CTL1_ERRIE     (1 << 8)    /* Error interrupt enable */
#define GD32_I2C_CTL1_EVIE      (1 << 9)    /* Event interrupt enable */
#define GD32_I2C_CTL1_BUFIE     (1 << 10)   /* Buffer interrupt enable */
#define GD32_I2C_CTL1_DMAON     (1 << 11)   /* DMA enable */
#define GD32_I2C_CTL1_DMALST    (1 << 12)   /* DMA last transfer */

/* SADDR0 register bits */
#define GD32_I2C_SADDR0_ADDRESS0    (1 << 0)    /* Bit 0 of 10-bit address */
#define GD32_I2C_SADDR0_ADDRESS_MASK (0x7F << 1) /* 7-bit address */
#define GD32_I2C_SADDR0_ADDRESS_H   (3 << 8)    /* High bits of 10-bit address */
#define GD32_I2C_SADDR0_ADDFORMAT   (1 << 15)   /* Address mode (0=7bit, 1=10bit) */

/* SADDR1 register bits */
#define GD32_I2C_SADDR1_DUADEN      (1 << 0)    /* Dual address mode enable */
#define GD32_I2C_SADDR1_ADDRESS2_MASK (0x7F << 1) /* Second address */

/* STAT0 register bits */
#define GD32_I2C_STAT0_SBSEND       (1 << 0)    /* Start bit sent (master) */
#define GD32_I2C_STAT0_ADDSEND      (1 << 1)    /* Address sent/matched */
#define GD32_I2C_STAT0_BTC          (1 << 2)    /* Byte transfer complete */
#define GD32_I2C_STAT0_ADD10SEND    (1 << 3)    /* 10-bit header sent */
#define GD32_I2C_STAT0_STPDET       (1 << 4)    /* Stop detected (slave) */
#define GD32_I2C_STAT0_RBNE         (1 << 6)    /* Receive buffer not empty */
#define GD32_I2C_STAT0_TBE          (1 << 7)    /* Transmit buffer empty */
#define GD32_I2C_STAT0_BERR         (1 << 8)    /* Bus error */
#define GD32_I2C_STAT0_LOSTARB      (1 << 9)    /* Arbitration lost */
#define GD32_I2C_STAT0_AERR         (1 << 10)   /* ACK failure */
#define GD32_I2C_STAT0_OUERR        (1 << 11)   /* Overrun/underrun */
#define GD32_I2C_STAT0_PECERR       (1 << 12)   /* PEC error */
#define GD32_I2C_STAT0_SMBTO        (1 << 14)   /* SMBus timeout */
#define GD32_I2C_STAT0_SMBALT       (1 << 15)   /* SMBus alert */

/* STAT1 register bits */
#define GD32_I2C_STAT1_MASTER       (1 << 0)    /* Master mode */
#define GD32_I2C_STAT1_I2CBSY       (1 << 1)    /* Bus busy */
#define GD32_I2C_STAT1_TR           (1 << 2)    /* Transmitter mode */
#define GD32_I2C_STAT1_RXGC         (1 << 4)    /* General call received */
#define GD32_I2C_STAT1_DEFSMB       (1 << 5)    /* SMBus default address */
#define GD32_I2C_STAT1_HSTSMB       (1 << 6)    /* SMBus host header */
#define GD32_I2C_STAT1_DUMODF       (1 << 7)    /* Dual address flag */
#define GD32_I2C_STAT1_PECV_MASK    (0xFF << 8) /* PEC value */

/* CKCFG register bits */
#define GD32_I2C_CKCFG_CLKC_MASK    0xFFF       /* Clock control */
#define GD32_I2C_CKCFG_DTCY         (1 << 14)   /* Fast mode duty cycle */
#define GD32_I2C_CKCFG_FAST         (1 << 15)   /* Fast mode enable */

/* RT register bits */
#define GD32_I2C_RT_RISETIME_MASK   0x3F        /* Rise time */

/* SAMCS register bits */
#define GD32_I2C_SAMCS_SAMEN        (1 << 0)    /* SAM_V enable */
#define GD32_I2C_SAMCS_STOEN        (1 << 1)    /* Timeout enable */
#define GD32_I2C_SAMCS_TFFIE        (1 << 4)    /* TX frame fall interrupt enable */
#define GD32_I2C_SAMCS_TFRIE        (1 << 5)    /* TX frame rise interrupt enable */
#define GD32_I2C_SAMCS_RFFIE        (1 << 6)    /* RX frame fall interrupt enable */
#define GD32_I2C_SAMCS_RFRIE        (1 << 7)    /* RX frame rise interrupt enable */
#define GD32_I2C_SAMCS_TXF          (1 << 8)    /* TX frame level */
#define GD32_I2C_SAMCS_RXF          (1 << 9)    /* RX frame level */
#define GD32_I2C_SAMCS_TFF          (1 << 12)   /* TX frame fall flag */
#define GD32_I2C_SAMCS_TFR          (1 << 13)   /* TX frame rise flag */
#define GD32_I2C_SAMCS_RFF          (1 << 14)   /* RX frame fall flag */
#define GD32_I2C_SAMCS_RFR          (1 << 15)   /* RX frame rise flag */

/* FMPCFG register bits */
#define GD32_I2C_FMPCFG_FMPEN       (1 << 0)    /* Fast mode plus enable */

#define TYPE_GD32_I2C "gd32-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(GD32I2CState, GD32_I2C)

struct GD32I2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* I2C bus for master mode */
    I2CBus *bus;

    /* Interrupts: event and error */
    qemu_irq irq_ev;
    qemu_irq irq_er;

    /* Device name for debug */
    char *name;

    /* Registers */
    uint32_t ctl0;      /* Control register 0 */
    uint32_t ctl1;      /* Control register 1 */
    uint32_t saddr0;    /* Slave address register 0 */
    uint32_t saddr1;    /* Slave address register 1 */
    uint32_t data;      /* Data register */
    uint32_t stat0;     /* Status register 0 */
    uint32_t stat1;     /* Status register 1 */
    uint32_t ckcfg;     /* Clock configuration */
    uint32_t rt;        /* Rise time */
    uint32_t samcs;     /* SAM control and status */
    uint32_t fmpcfg;    /* Fast mode plus configuration */

    /* Internal state */
    uint8_t rx_data;    /* Received data buffer */
    uint8_t tx_data;    /* Transmit data buffer */
    uint8_t slave_addr; /* Current slave address */
    bool is_recv;       /* Receive mode flag */
    bool addr_sent;     /* Address has been sent */
};

#endif /* HW_GD32_I2C_H */

