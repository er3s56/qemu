/*
 * GD32 CAN Controller Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the GD32C10x bxCAN controller with:
 * - 3 transmit mailboxes
 * - 2 receive FIFOs (3 messages each)
 * - 28 configurable filters
 * - SocketCAN integration
 */

#ifndef HW_GD32_CAN_H
#define HW_GD32_CAN_H

#include "hw/sysbus.h"
#include "net/can_emu.h"
#include "qom/object.h"

/* GD32 CAN Register Offsets */
#define GD32_CAN_CTL        0x00    /* Control register */
#define GD32_CAN_STAT       0x04    /* Status register */
#define GD32_CAN_TSTAT      0x08    /* Transmit status register */
#define GD32_CAN_RFIFO0     0x0C    /* Receive FIFO0 register */
#define GD32_CAN_RFIFO1     0x10    /* Receive FIFO1 register */
#define GD32_CAN_INTEN      0x14    /* Interrupt enable register */
#define GD32_CAN_ERR        0x18    /* Error register */
#define GD32_CAN_BT         0x1C    /* Bit timing register */

/* Transmit mailbox registers (3 mailboxes, 0x10 bytes each) */
#define GD32_CAN_TMI0       0x180   /* TX mailbox 0 identifier */
#define GD32_CAN_TMP0       0x184   /* TX mailbox 0 property */
#define GD32_CAN_TMDATA00   0x188   /* TX mailbox 0 data0 */
#define GD32_CAN_TMDATA10   0x18C   /* TX mailbox 0 data1 */
#define GD32_CAN_TMI1       0x190   /* TX mailbox 1 identifier */
#define GD32_CAN_TMP1       0x194   /* TX mailbox 1 property */
#define GD32_CAN_TMDATA01   0x198   /* TX mailbox 1 data0 */
#define GD32_CAN_TMDATA11   0x19C   /* TX mailbox 1 data1 */
#define GD32_CAN_TMI2       0x1A0   /* TX mailbox 2 identifier */
#define GD32_CAN_TMP2       0x1A4   /* TX mailbox 2 property */
#define GD32_CAN_TMDATA02   0x1A8   /* TX mailbox 2 data0 */
#define GD32_CAN_TMDATA12   0x1AC   /* TX mailbox 2 data1 */

/* Receive FIFO mailbox registers (2 FIFOs) */
#define GD32_CAN_RFIFOMI0   0x1B0   /* RX FIFO0 mailbox identifier */
#define GD32_CAN_RFIFOMP0   0x1B4   /* RX FIFO0 mailbox property */
#define GD32_CAN_RFIFOMDATA00 0x1B8 /* RX FIFO0 mailbox data0 */
#define GD32_CAN_RFIFOMDATA10 0x1BC /* RX FIFO0 mailbox data1 */
#define GD32_CAN_RFIFOMI1   0x1C0   /* RX FIFO1 mailbox identifier */
#define GD32_CAN_RFIFOMP1   0x1C4   /* RX FIFO1 mailbox property */
#define GD32_CAN_RFIFOMDATA01 0x1C8 /* RX FIFO1 mailbox data0 */
#define GD32_CAN_RFIFOMDATA11 0x1CC /* RX FIFO1 mailbox data1 */

/* Filter registers */
#define GD32_CAN_FCTL       0x200   /* Filter control register */
#define GD32_CAN_FMCFG      0x204   /* Filter mode configuration */
#define GD32_CAN_FSCFG      0x20C   /* Filter scale configuration */
#define GD32_CAN_FAFIFO     0x214   /* Filter associated FIFO */
#define GD32_CAN_FW         0x21C   /* Filter working register */
#define GD32_CAN_F0DATA0    0x240   /* Filter 0 data 0 */
/* Filter data registers: 0x240 + filter_num * 8 */

/* CTL register bits */
#define GD32_CAN_CTL_IWMOD      (1 << 0)    /* Initial working mode */
#define GD32_CAN_CTL_SLPWMOD    (1 << 1)    /* Sleep working mode */
#define GD32_CAN_CTL_TFO        (1 << 2)    /* Transmit FIFO order */
#define GD32_CAN_CTL_RFOD       (1 << 3)    /* Receive FIFO overwrite disable */
#define GD32_CAN_CTL_ARD        (1 << 4)    /* Automatic retransmission disable */
#define GD32_CAN_CTL_AWU        (1 << 5)    /* Automatic wakeup */
#define GD32_CAN_CTL_ABOR       (1 << 6)    /* Automatic bus-off recovery */
#define GD32_CAN_CTL_TTC        (1 << 7)    /* Time triggered communication */
#define GD32_CAN_CTL_SWRST      (1 << 15)   /* Software reset */
#define GD32_CAN_CTL_DFZ        (1 << 16)   /* Debug freeze */

/* STAT register bits */
#define GD32_CAN_STAT_IWS       (1 << 0)    /* Initial working state */
#define GD32_CAN_STAT_SLPWS     (1 << 1)    /* Sleep working state */
#define GD32_CAN_STAT_ERRIF     (1 << 2)    /* Error interrupt flag */
#define GD32_CAN_STAT_WUIF      (1 << 3)    /* Wakeup interrupt flag */
#define GD32_CAN_STAT_SLPIF     (1 << 4)    /* Sleep interrupt flag */
#define GD32_CAN_STAT_TS        (1 << 8)    /* Transmitting state */
#define GD32_CAN_STAT_RS        (1 << 9)    /* Receiving state */
#define GD32_CAN_STAT_LASTRX    (1 << 10)   /* Last sample value of RX */
#define GD32_CAN_STAT_RXL       (1 << 11)   /* CAN RX signal */

/* TSTAT register bits */
#define GD32_CAN_TSTAT_MTF0     (1 << 0)    /* Mailbox 0 transmit finished */
#define GD32_CAN_TSTAT_MTFNERR0 (1 << 1)    /* Mailbox 0 transmit finished, no error */
#define GD32_CAN_TSTAT_MAL0     (1 << 2)    /* Mailbox 0 arbitration lost */
#define GD32_CAN_TSTAT_MTE0     (1 << 3)    /* Mailbox 0 transmit error */
#define GD32_CAN_TSTAT_MST0     (1 << 7)    /* Mailbox 0 stop transmitting */
#define GD32_CAN_TSTAT_MTF1     (1 << 8)    /* Mailbox 1 transmit finished */
#define GD32_CAN_TSTAT_MTFNERR1 (1 << 9)    /* Mailbox 1 transmit finished, no error */
#define GD32_CAN_TSTAT_MAL1     (1 << 10)   /* Mailbox 1 arbitration lost */
#define GD32_CAN_TSTAT_MTE1     (1 << 11)   /* Mailbox 1 transmit error */
#define GD32_CAN_TSTAT_MST1     (1 << 15)   /* Mailbox 1 stop transmitting */
#define GD32_CAN_TSTAT_MTF2     (1 << 16)   /* Mailbox 2 transmit finished */
#define GD32_CAN_TSTAT_MTFNERR2 (1 << 17)   /* Mailbox 2 transmit finished, no error */
#define GD32_CAN_TSTAT_MAL2     (1 << 18)   /* Mailbox 2 arbitration lost */
#define GD32_CAN_TSTAT_MTE2     (1 << 19)   /* Mailbox 2 transmit error */
#define GD32_CAN_TSTAT_MST2     (1 << 23)   /* Mailbox 2 stop transmitting */
#define GD32_CAN_TSTAT_NUM_MASK (3 << 24)   /* Mailbox number */
#define GD32_CAN_TSTAT_TME0     (1 << 26)   /* Transmit mailbox 0 empty */
#define GD32_CAN_TSTAT_TME1     (1 << 27)   /* Transmit mailbox 1 empty */
#define GD32_CAN_TSTAT_TME2     (1 << 28)   /* Transmit mailbox 2 empty */
#define GD32_CAN_TSTAT_TMLS0    (1 << 29)   /* Transmit mailbox 0 last sending */
#define GD32_CAN_TSTAT_TMLS1    (1 << 30)   /* Transmit mailbox 1 last sending */
#define GD32_CAN_TSTAT_TMLS2    (1 << 31)   /* Transmit mailbox 2 last sending */

/* RFIFO0/1 register bits */
#define GD32_CAN_RFIFO_RFL_MASK (3 << 0)    /* Receive FIFO length */
#define GD32_CAN_RFIFO_RFF      (1 << 3)    /* Receive FIFO full */
#define GD32_CAN_RFIFO_RFO      (1 << 4)    /* Receive FIFO overfull */
#define GD32_CAN_RFIFO_RFD      (1 << 5)    /* Receive FIFO dequeue */

/* INTEN register bits */
#define GD32_CAN_INTEN_TMEIE    (1 << 0)    /* TX mailbox empty interrupt enable */
#define GD32_CAN_INTEN_RFNEIE0  (1 << 1)    /* RX FIFO0 not empty interrupt enable */
#define GD32_CAN_INTEN_RFFIE0   (1 << 2)    /* RX FIFO0 full interrupt enable */
#define GD32_CAN_INTEN_RFOIE0   (1 << 3)    /* RX FIFO0 overfull interrupt enable */
#define GD32_CAN_INTEN_RFNEIE1  (1 << 4)    /* RX FIFO1 not empty interrupt enable */
#define GD32_CAN_INTEN_RFFIE1   (1 << 5)    /* RX FIFO1 full interrupt enable */
#define GD32_CAN_INTEN_RFOIE1   (1 << 6)    /* RX FIFO1 overfull interrupt enable */
#define GD32_CAN_INTEN_WERRIE   (1 << 8)    /* Warning error interrupt enable */
#define GD32_CAN_INTEN_PERRIE   (1 << 9)    /* Passive error interrupt enable */
#define GD32_CAN_INTEN_BOIE     (1 << 10)   /* Bus-off interrupt enable */
#define GD32_CAN_INTEN_ERRNIE   (1 << 11)   /* Error number interrupt enable */
#define GD32_CAN_INTEN_ERRIE    (1 << 15)   /* Error interrupt enable */
#define GD32_CAN_INTEN_WIE      (1 << 16)   /* Wakeup interrupt enable */
#define GD32_CAN_INTEN_SLPWIE   (1 << 17)   /* Sleep working interrupt enable */

/* ERR register bits */
#define GD32_CAN_ERR_WERR       (1 << 0)    /* Warning error */
#define GD32_CAN_ERR_PERR       (1 << 1)    /* Passive error */
#define GD32_CAN_ERR_BOERR      (1 << 2)    /* Bus-off error */
#define GD32_CAN_ERR_ERRN_MASK  (7 << 4)    /* Error number */
#define GD32_CAN_ERR_TECNT_MASK (0xFF << 16) /* Transmit error count */
#define GD32_CAN_ERR_RECNT_MASK (0xFF << 24) /* Receive error count */

/* BT register bits */
#define GD32_CAN_BT_BAUDPSC_MASK (0x3FF << 0)  /* Baud rate prescaler */
#define GD32_CAN_BT_BS1_MASK    (0xF << 16)    /* Bit segment 1 */
#define GD32_CAN_BT_BS2_MASK    (7 << 20)      /* Bit segment 2 */
#define GD32_CAN_BT_SJW_MASK    (3 << 24)      /* Resync jump width */
#define GD32_CAN_BT_LCMOD       (1 << 30)      /* Loopback mode */
#define GD32_CAN_BT_SCMOD       (1 << 31)      /* Silent mode */

/* TMI register bits */
#define GD32_CAN_TMI_TEN        (1 << 0)       /* Transmit enable */
#define GD32_CAN_TMI_FT         (1 << 1)       /* Frame type (0=data, 1=remote) */
#define GD32_CAN_TMI_FF         (1 << 2)       /* Frame format (0=std, 1=ext) */
#define GD32_CAN_TMI_EFID_MASK  (0x1FFFFFFF << 3)  /* Extended frame ID */
#define GD32_CAN_TMI_SFID_MASK  (0x7FF << 21)      /* Standard frame ID */

/* TMP register bits */
#define GD32_CAN_TMP_DLENC_MASK (0xF << 0)     /* Data length code */
#define GD32_CAN_TMP_TSEN       (1 << 8)       /* Timestamp enable */
#define GD32_CAN_TMP_TS_MASK    (0xFFFF << 16) /* Timestamp */

/* RFIFOMI register bits */
#define GD32_CAN_RFIFOMI_FT     (1 << 1)       /* Frame type */
#define GD32_CAN_RFIFOMI_FF     (1 << 2)       /* Frame format */
#define GD32_CAN_RFIFOMI_EFID_MASK (0x1FFFFFFF << 3)
#define GD32_CAN_RFIFOMI_SFID_MASK (0x7FF << 21)

/* RFIFOMP register bits */
#define GD32_CAN_RFIFOMP_DLENC_MASK (0xF << 0)
#define GD32_CAN_RFIFOMP_FI_MASK    (0xFF << 8)  /* Filter index */
#define GD32_CAN_RFIFOMP_TS_MASK    (0xFFFF << 16)

/* FCTL register bits */
#define GD32_CAN_FCTL_FLD       (1 << 0)       /* Filter lock disable */
#define GD32_CAN_FCTL_HBC1F_MASK (0x3F << 8)   /* Header bank of CAN1 filter */

/* Constants */
#define GD32_CAN_NUM_TX_MAILBOXES   3
#define GD32_CAN_NUM_RX_FIFOS       2
#define GD32_CAN_RX_FIFO_DEPTH      3
#define GD32_CAN_NUM_FILTERS        28

/* Transmit mailbox state */
typedef struct {
    uint32_t tmi;       /* Identifier register */
    uint32_t tmp;       /* Property register */
    uint32_t tmdata0;   /* Data 0 register */
    uint32_t tmdata1;   /* Data 1 register */
    bool pending;       /* Transmission pending */
} GD32CanTxMailbox;

/* Receive FIFO message */
typedef struct {
    uint32_t rfifomi;   /* Identifier register */
    uint32_t rfifomp;   /* Property register */
    uint32_t rfifomdata0; /* Data 0 register */
    uint32_t rfifomdata1; /* Data 1 register */
} GD32CanRxMessage;

/* Receive FIFO state */
typedef struct {
    GD32CanRxMessage messages[GD32_CAN_RX_FIFO_DEPTH];
    uint8_t count;      /* Number of messages in FIFO */
    uint8_t read_idx;   /* Read index */
    uint8_t write_idx;  /* Write index */
    bool full;          /* FIFO full flag */
    bool overrun;       /* FIFO overrun flag */
} GD32CanRxFifo;

/* Filter state */
typedef struct {
    uint32_t data0;     /* Filter data 0 */
    uint32_t data1;     /* Filter data 1 */
} GD32CanFilter;

#define TYPE_GD32_CAN "gd32-can"
OBJECT_DECLARE_SIMPLE_TYPE(GD32CanState, GD32_CAN)

struct GD32CanState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* Interrupts: TX, RX0, RX1, SCE (status change/error) */
    qemu_irq irq_tx;
    qemu_irq irq_rx0;
    qemu_irq irq_rx1;
    qemu_irq irq_sce;

    /* CAN bus connection */
    CanBusClientState bus_client;
    CanBusState *canbus;

    /* Device name for debug */
    char *name;

    /* Control registers */
    uint32_t ctl;       /* Control register */
    uint32_t stat;      /* Status register */
    uint32_t tstat;     /* Transmit status register */
    uint32_t rfifo0;    /* Receive FIFO0 register */
    uint32_t rfifo1;    /* Receive FIFO1 register */
    uint32_t inten;     /* Interrupt enable register */
    uint32_t err;       /* Error register */
    uint32_t bt;        /* Bit timing register */

    /* Transmit mailboxes */
    GD32CanTxMailbox tx_mailbox[GD32_CAN_NUM_TX_MAILBOXES];

    /* Receive FIFOs */
    GD32CanRxFifo rx_fifo[GD32_CAN_NUM_RX_FIFOS];

    /* Filter registers */
    uint32_t fctl;      /* Filter control */
    uint32_t fmcfg;     /* Filter mode configuration */
    uint32_t fscfg;     /* Filter scale configuration */
    uint32_t fafifo;    /* Filter associated FIFO */
    uint32_t fw;        /* Filter working */
    GD32CanFilter filters[GD32_CAN_NUM_FILTERS];

    /* Internal state */
    bool init_mode;     /* In initialization mode */
    bool sleep_mode;    /* In sleep mode */
    uint16_t timestamp; /* Timestamp counter */
};

#endif /* HW_GD32_CAN_H */

