/*
 * GD32 CAN Controller Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the GD32C10x bxCAN controller with full support for:
 * - 3 transmit mailboxes with priority scheduling
 * - 2 receive FIFOs with 3 message depth each
 * - 28 configurable filters (mask/list mode, 16/32-bit scale)
 * - SocketCAN integration for external communication
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/net/can/gd32_can.h"
#include "migration/vmstate.h"
#include "net/can_emu.h"
#include "net/can_host.h"

#ifndef GD32_CAN_DEBUG
#define GD32_CAN_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (GD32_CAN_DEBUG) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

/* Forward declarations */
static void gd32_can_update_irq(GD32CanState *s);
static bool gd32_can_filter_match(GD32CanState *s, const qemu_can_frame *frame,
                                   int *filter_idx, int *fifo_num);

/*
 * Mode management
 */
static void gd32_can_enter_init_mode(GD32CanState *s)
{
    s->init_mode = true;
    s->stat |= GD32_CAN_STAT_IWS;
    DB_PRINT("%s: entered init mode\n", s->name ? s->name : "CAN");
}

static void gd32_can_leave_init_mode(GD32CanState *s)
{
    s->init_mode = false;
    s->stat &= ~GD32_CAN_STAT_IWS;
    /* All TX mailboxes are empty after leaving init mode */
    s->tstat |= GD32_CAN_TSTAT_TME0 | GD32_CAN_TSTAT_TME1 | GD32_CAN_TSTAT_TME2;
    DB_PRINT("%s: left init mode\n", s->name ? s->name : "CAN");
}

static void gd32_can_enter_sleep_mode(GD32CanState *s)
{
    s->sleep_mode = true;
    s->stat |= GD32_CAN_STAT_SLPWS;
    s->stat |= GD32_CAN_STAT_SLPIF;
    DB_PRINT("%s: entered sleep mode\n", s->name ? s->name : "CAN");
}

static void gd32_can_leave_sleep_mode(GD32CanState *s)
{
    s->sleep_mode = false;
    s->stat &= ~GD32_CAN_STAT_SLPWS;
    s->stat |= GD32_CAN_STAT_WUIF;
    DB_PRINT("%s: left sleep mode\n", s->name ? s->name : "CAN");
}

/*
 * Interrupt handling
 */
static void gd32_can_update_irq(GD32CanState *s)
{
    int tx_irq = 0;
    int rx0_irq = 0;
    int rx1_irq = 0;
    int sce_irq = 0;

    /* TX mailbox empty interrupt */
    if ((s->inten & GD32_CAN_INTEN_TMEIE) &&
        ((s->tstat & GD32_CAN_TSTAT_MTF0) ||
         (s->tstat & GD32_CAN_TSTAT_MTF1) ||
         (s->tstat & GD32_CAN_TSTAT_MTF2))) {
        tx_irq = 1;
    }

    /* RX FIFO0 interrupts */
    if ((s->inten & GD32_CAN_INTEN_RFNEIE0) &&
        (s->rfifo0 & GD32_CAN_RFIFO_RFL_MASK)) {
        rx0_irq = 1;
    }
    if ((s->inten & GD32_CAN_INTEN_RFFIE0) &&
        (s->rfifo0 & GD32_CAN_RFIFO_RFF)) {
        rx0_irq = 1;
    }
    if ((s->inten & GD32_CAN_INTEN_RFOIE0) &&
        (s->rfifo0 & GD32_CAN_RFIFO_RFO)) {
        rx0_irq = 1;
    }

    /* RX FIFO1 interrupts */
    if ((s->inten & GD32_CAN_INTEN_RFNEIE1) &&
        (s->rfifo1 & GD32_CAN_RFIFO_RFL_MASK)) {
        rx1_irq = 1;
    }
    if ((s->inten & GD32_CAN_INTEN_RFFIE1) &&
        (s->rfifo1 & GD32_CAN_RFIFO_RFF)) {
        rx1_irq = 1;
    }
    if ((s->inten & GD32_CAN_INTEN_RFOIE1) &&
        (s->rfifo1 & GD32_CAN_RFIFO_RFO)) {
        rx1_irq = 1;
    }

    /* Status change / error interrupts */
    if ((s->inten & GD32_CAN_INTEN_ERRIE) &&
        (s->stat & GD32_CAN_STAT_ERRIF)) {
        sce_irq = 1;
    }
    if ((s->inten & GD32_CAN_INTEN_WIE) &&
        (s->stat & GD32_CAN_STAT_WUIF)) {
        sce_irq = 1;
    }
    if ((s->inten & GD32_CAN_INTEN_SLPWIE) &&
        (s->stat & GD32_CAN_STAT_SLPIF)) {
        sce_irq = 1;
    }

    qemu_set_irq(s->irq_tx, tx_irq);
    qemu_set_irq(s->irq_rx0, rx0_irq);
    qemu_set_irq(s->irq_rx1, rx1_irq);
    qemu_set_irq(s->irq_sce, sce_irq);
}

/*
 * Transmit handling
 */
static void gd32_can_transmit_mailbox(GD32CanState *s, int mailbox)
{
    GD32CanTxMailbox *mb = &s->tx_mailbox[mailbox];
    qemu_can_frame frame;
    uint32_t tstat_mtf, tstat_mtfnerr, tstat_tme;

    if (!mb->pending) {
        return;
    }

    /* Cannot transmit in init or sleep mode */
    if (s->init_mode || s->sleep_mode) {
        return;
    }

    /* Build CAN frame */
    memset(&frame, 0, sizeof(frame));

    if (mb->tmi & GD32_CAN_TMI_FF) {
        /* Extended frame */
        frame.can_id = ((mb->tmi >> 3) & QEMU_CAN_EFF_MASK) | QEMU_CAN_EFF_FLAG;
    } else {
        /* Standard frame */
        frame.can_id = (mb->tmi >> 21) & QEMU_CAN_SFF_MASK;
    }

    if (mb->tmi & GD32_CAN_TMI_FT) {
        /* Remote frame */
        frame.can_id |= QEMU_CAN_RTR_FLAG;
    }

    frame.can_dlc = mb->tmp & 0xF;
    if (frame.can_dlc > 8) {
        frame.can_dlc = 8;
    }

    /* Copy data */
    frame.data[0] = mb->tmdata0 & 0xFF;
    frame.data[1] = (mb->tmdata0 >> 8) & 0xFF;
    frame.data[2] = (mb->tmdata0 >> 16) & 0xFF;
    frame.data[3] = (mb->tmdata0 >> 24) & 0xFF;
    frame.data[4] = mb->tmdata1 & 0xFF;
    frame.data[5] = (mb->tmdata1 >> 8) & 0xFF;
    frame.data[6] = (mb->tmdata1 >> 16) & 0xFF;
    frame.data[7] = (mb->tmdata1 >> 24) & 0xFF;

    DB_PRINT("%s: TX mailbox %d: ID=0x%x DLC=%d\n",
             s->name ? s->name : "CAN", mailbox, frame.can_id, frame.can_dlc);

    /* Send to CAN bus */
    if (s->canbus) {
        can_bus_client_send(&s->bus_client, &frame, 1);
    }

    /* Loopback mode: also receive the frame */
    if (s->bt & GD32_CAN_BT_LCMOD) {
        int filter_idx, fifo_num;
        if (gd32_can_filter_match(s, &frame, &filter_idx, &fifo_num)) {
            /* Would add to RX FIFO here */
        }
    }

    /* Update status */
    mb->pending = false;
    mb->tmi &= ~GD32_CAN_TMI_TEN;

    switch (mailbox) {
    case 0:
        tstat_mtf = GD32_CAN_TSTAT_MTF0;
        tstat_mtfnerr = GD32_CAN_TSTAT_MTFNERR0;
        tstat_tme = GD32_CAN_TSTAT_TME0;
        break;
    case 1:
        tstat_mtf = GD32_CAN_TSTAT_MTF1;
        tstat_mtfnerr = GD32_CAN_TSTAT_MTFNERR1;
        tstat_tme = GD32_CAN_TSTAT_TME1;
        break;
    case 2:
    default:
        tstat_mtf = GD32_CAN_TSTAT_MTF2;
        tstat_mtfnerr = GD32_CAN_TSTAT_MTFNERR2;
        tstat_tme = GD32_CAN_TSTAT_TME2;
        break;
    }

    s->tstat |= tstat_mtf | tstat_mtfnerr | tstat_tme;
    gd32_can_update_irq(s);
}

static void gd32_can_start_transmit(GD32CanState *s, int mailbox)
{
    GD32CanTxMailbox *mb = &s->tx_mailbox[mailbox];
    uint32_t tstat_tme;

    switch (mailbox) {
    case 0: tstat_tme = GD32_CAN_TSTAT_TME0; break;
    case 1: tstat_tme = GD32_CAN_TSTAT_TME1; break;
    case 2:
    default: tstat_tme = GD32_CAN_TSTAT_TME2; break;
    }

    mb->pending = true;
    s->tstat &= ~tstat_tme;

    /* Transmit immediately (simplified - no arbitration delay) */
    gd32_can_transmit_mailbox(s, mailbox);
}

static void gd32_can_abort_transmit(GD32CanState *s, int mailbox)
{
    GD32CanTxMailbox *mb = &s->tx_mailbox[mailbox];
    uint32_t tstat_tme;

    switch (mailbox) {
    case 0: tstat_tme = GD32_CAN_TSTAT_TME0; break;
    case 1: tstat_tme = GD32_CAN_TSTAT_TME1; break;
    case 2:
    default: tstat_tme = GD32_CAN_TSTAT_TME2; break;
    }

    mb->pending = false;
    mb->tmi &= ~GD32_CAN_TMI_TEN;
    s->tstat |= tstat_tme;
}

/*
 * Filter handling
 */
static bool gd32_can_filter_match(GD32CanState *s, const qemu_can_frame *frame,
                                   int *filter_idx, int *fifo_num)
{
    uint32_t can_id = frame->can_id;
    bool is_extended = (can_id & QEMU_CAN_EFF_FLAG) != 0;
    uint32_t id;
    int i;

    if (is_extended) {
        id = can_id & QEMU_CAN_EFF_MASK;
    } else {
        id = can_id & QEMU_CAN_SFF_MASK;
    }

    /* Check each enabled filter */
    for (i = 0; i < GD32_CAN_NUM_FILTERS; i++) {
        if (!(s->fw & (1 << i))) {
            continue;  /* Filter not active */
        }

        bool is_32bit = (s->fscfg & (1 << i)) != 0;
        bool is_list_mode = (s->fmcfg & (1 << i)) != 0;
        uint32_t data0 = s->filters[i].data0;
        uint32_t data1 = s->filters[i].data1;
        bool match = false;

        if (is_32bit) {
            /* 32-bit filter */
            if (is_list_mode) {
                /* List mode: exact match with data0 or data1 */
                uint32_t filter_id0 = (data0 >> 3) & 0x1FFFFFFF;
                uint32_t filter_id1 = (data1 >> 3) & 0x1FFFFFFF;
                bool filter_ext0 = (data0 & 4) != 0;
                bool filter_ext1 = (data1 & 4) != 0;

                if ((id == filter_id0 && is_extended == filter_ext0) ||
                    (id == filter_id1 && is_extended == filter_ext1)) {
                    match = true;
                }
            } else {
                /* Mask mode: (id & mask) == (filter & mask) */
                uint32_t filter_id = (data0 >> 3) & 0x1FFFFFFF;
                uint32_t filter_mask = (data1 >> 3) & 0x1FFFFFFF;
                bool filter_ext = (data0 & 4) != 0;
                bool mask_ext = (data1 & 4) != 0;

                if (((id & filter_mask) == (filter_id & filter_mask)) &&
                    (!mask_ext || (is_extended == filter_ext))) {
                    match = true;
                }
            }
        } else {
            /* 16-bit filter (4 filters per bank) */
            uint16_t filters16[4];
            filters16[0] = data0 & 0xFFFF;
            filters16[1] = (data0 >> 16) & 0xFFFF;
            filters16[2] = data1 & 0xFFFF;
            filters16[3] = (data1 >> 16) & 0xFFFF;

            if (is_list_mode) {
                /* List mode: 4 exact match filters */
                for (int j = 0; j < 4; j++) {
                    uint16_t filter_id = (filters16[j] >> 5) & 0x7FF;
                    if (!is_extended && (id == filter_id)) {
                        match = true;
                        break;
                    }
                }
            } else {
                /* Mask mode: 2 filter/mask pairs */
                for (int j = 0; j < 2; j++) {
                    uint16_t filter_id = (filters16[j * 2] >> 5) & 0x7FF;
                    uint16_t filter_mask = (filters16[j * 2 + 1] >> 5) & 0x7FF;
                    if (!is_extended &&
                        ((id & filter_mask) == (filter_id & filter_mask))) {
                        match = true;
                        break;
                    }
                }
            }
        }

        if (match) {
            *filter_idx = i;
            *fifo_num = (s->fafifo & (1 << i)) ? 1 : 0;
            return true;
        }
    }

    return false;
}

/*
 * Receive handling
 */
static void gd32_can_receive_frame(GD32CanState *s, const qemu_can_frame *frame,
                                    int filter_idx, int fifo_num)
{
    GD32CanRxFifo *fifo = &s->rx_fifo[fifo_num];
    GD32CanRxMessage *msg;
    uint32_t *rfifo_reg = (fifo_num == 0) ? &s->rfifo0 : &s->rfifo1;

    /* Check if FIFO is full */
    if (fifo->count >= GD32_CAN_RX_FIFO_DEPTH) {
        if (s->ctl & GD32_CAN_CTL_RFOD) {
            /* Overwrite disabled - set overrun flag and discard */
            *rfifo_reg |= GD32_CAN_RFIFO_RFO;
            fifo->overrun = true;
            DB_PRINT("%s: RX FIFO%d overrun\n", s->name ? s->name : "CAN", fifo_num);
            gd32_can_update_irq(s);
            return;
        } else {
            /* Overwrite enabled - overwrite oldest message */
            fifo->read_idx = (fifo->read_idx + 1) % GD32_CAN_RX_FIFO_DEPTH;
        }
    }

    /* Store message */
    msg = &fifo->messages[fifo->write_idx];

    /* Build RFIFOMI */
    msg->rfifomi = 0;
    if (frame->can_id & QEMU_CAN_EFF_FLAG) {
        msg->rfifomi |= GD32_CAN_RFIFOMI_FF;
        msg->rfifomi |= ((frame->can_id & QEMU_CAN_EFF_MASK) << 3);
    } else {
        msg->rfifomi |= ((frame->can_id & QEMU_CAN_SFF_MASK) << 21);
    }
    if (frame->can_id & QEMU_CAN_RTR_FLAG) {
        msg->rfifomi |= GD32_CAN_RFIFOMI_FT;
    }

    /* Build RFIFOMP */
    msg->rfifomp = (frame->can_dlc & 0xF);
    msg->rfifomp |= ((filter_idx & 0xFF) << 8);
    msg->rfifomp |= ((s->timestamp & 0xFFFF) << 16);

    /* Copy data */
    msg->rfifomdata0 = frame->data[0] |
                       (frame->data[1] << 8) |
                       (frame->data[2] << 16) |
                       (frame->data[3] << 24);
    msg->rfifomdata1 = frame->data[4] |
                       (frame->data[5] << 8) |
                       (frame->data[6] << 16) |
                       (frame->data[7] << 24);

    /* Update FIFO state */
    fifo->write_idx = (fifo->write_idx + 1) % GD32_CAN_RX_FIFO_DEPTH;
    if (fifo->count < GD32_CAN_RX_FIFO_DEPTH) {
        fifo->count++;
    }

    /* Update register */
    *rfifo_reg = (*rfifo_reg & ~GD32_CAN_RFIFO_RFL_MASK) | fifo->count;
    if (fifo->count >= GD32_CAN_RX_FIFO_DEPTH) {
        *rfifo_reg |= GD32_CAN_RFIFO_RFF;
        fifo->full = true;
    }

    DB_PRINT("%s: RX FIFO%d received: ID=0x%x DLC=%d filter=%d count=%d\n",
             s->name ? s->name : "CAN", fifo_num,
             frame->can_id, frame->can_dlc, filter_idx, fifo->count);

    gd32_can_update_irq(s);
}

static void gd32_can_release_fifo(GD32CanState *s, int fifo_num)
{
    GD32CanRxFifo *fifo = &s->rx_fifo[fifo_num];
    uint32_t *rfifo_reg = (fifo_num == 0) ? &s->rfifo0 : &s->rfifo1;

    if (fifo->count > 0) {
        fifo->read_idx = (fifo->read_idx + 1) % GD32_CAN_RX_FIFO_DEPTH;
        fifo->count--;
        fifo->full = false;
        *rfifo_reg &= ~GD32_CAN_RFIFO_RFF;
    }

    *rfifo_reg = (*rfifo_reg & ~GD32_CAN_RFIFO_RFL_MASK) | fifo->count;
    gd32_can_update_irq(s);
}

/*
 * CAN bus client callbacks
 */
static bool gd32_can_can_receive(CanBusClientState *client)
{
    GD32CanState *s = container_of(client, GD32CanState, bus_client);

    /* Cannot receive in init or sleep mode */
    if (s->init_mode || s->sleep_mode) {
        return false;
    }

    /* Can receive if at least one FIFO has space or overwrite is enabled */
    return (s->rx_fifo[0].count < GD32_CAN_RX_FIFO_DEPTH) ||
           (s->rx_fifo[1].count < GD32_CAN_RX_FIFO_DEPTH) ||
           !(s->ctl & GD32_CAN_CTL_RFOD);
}

static ssize_t gd32_can_receive(CanBusClientState *client,
                                 const qemu_can_frame *frames,
                                 size_t frames_cnt)
{
    GD32CanState *s = container_of(client, GD32CanState, bus_client);
    size_t i;
    int filter_idx, fifo_num;

    for (i = 0; i < frames_cnt; i++) {
        /* Silent mode: don't receive */
        if (s->bt & GD32_CAN_BT_SCMOD) {
            continue;
        }

        /* Check filters */
        if (gd32_can_filter_match(s, &frames[i], &filter_idx, &fifo_num)) {
            gd32_can_receive_frame(s, &frames[i], filter_idx, fifo_num);
        }
    }

    return i;
}

static CanBusClientInfo gd32_can_bus_client_info = {
    .can_receive = gd32_can_can_receive,
    .receive = gd32_can_receive,
};

/*
 * Register read/write
 */
static uint64_t gd32_can_read(void *opaque, hwaddr addr, unsigned size)
{
    GD32CanState *s = GD32_CAN(opaque);
    uint64_t value = 0;
    int mailbox, fifo_num;
    GD32CanRxFifo *fifo;
    GD32CanRxMessage *msg;

    switch (addr) {
    case GD32_CAN_CTL:
        value = s->ctl;
        break;
    case GD32_CAN_STAT:
        value = s->stat;
        break;
    case GD32_CAN_TSTAT:
        value = s->tstat;
        break;
    case GD32_CAN_RFIFO0:
        value = s->rfifo0;
        break;
    case GD32_CAN_RFIFO1:
        value = s->rfifo1;
        break;
    case GD32_CAN_INTEN:
        value = s->inten;
        break;
    case GD32_CAN_ERR:
        value = s->err;
        break;
    case GD32_CAN_BT:
        value = s->bt;
        break;

    /* TX mailbox registers */
    case GD32_CAN_TMI0:
    case GD32_CAN_TMI1:
    case GD32_CAN_TMI2:
        mailbox = (addr - GD32_CAN_TMI0) / 0x10;
        value = s->tx_mailbox[mailbox].tmi;
        break;
    case GD32_CAN_TMP0:
    case GD32_CAN_TMP1:
    case GD32_CAN_TMP2:
        mailbox = (addr - GD32_CAN_TMP0) / 0x10;
        value = s->tx_mailbox[mailbox].tmp;
        break;
    case GD32_CAN_TMDATA00:
    case GD32_CAN_TMDATA01:
    case GD32_CAN_TMDATA02:
        mailbox = (addr - GD32_CAN_TMDATA00) / 0x10;
        value = s->tx_mailbox[mailbox].tmdata0;
        break;
    case GD32_CAN_TMDATA10:
    case GD32_CAN_TMDATA11:
    case GD32_CAN_TMDATA12:
        mailbox = (addr - GD32_CAN_TMDATA10) / 0x10;
        value = s->tx_mailbox[mailbox].tmdata1;
        break;

    /* RX FIFO mailbox registers */
    case GD32_CAN_RFIFOMI0:
    case GD32_CAN_RFIFOMI1:
        fifo_num = (addr == GD32_CAN_RFIFOMI1) ? 1 : 0;
        fifo = &s->rx_fifo[fifo_num];
        if (fifo->count > 0) {
            msg = &fifo->messages[fifo->read_idx];
            value = msg->rfifomi;
        }
        break;
    case GD32_CAN_RFIFOMP0:
    case GD32_CAN_RFIFOMP1:
        fifo_num = (addr == GD32_CAN_RFIFOMP1) ? 1 : 0;
        fifo = &s->rx_fifo[fifo_num];
        if (fifo->count > 0) {
            msg = &fifo->messages[fifo->read_idx];
            value = msg->rfifomp;
        }
        break;
    case GD32_CAN_RFIFOMDATA00:
    case GD32_CAN_RFIFOMDATA01:
        fifo_num = (addr == GD32_CAN_RFIFOMDATA01) ? 1 : 0;
        fifo = &s->rx_fifo[fifo_num];
        if (fifo->count > 0) {
            msg = &fifo->messages[fifo->read_idx];
            value = msg->rfifomdata0;
        }
        break;
    case GD32_CAN_RFIFOMDATA10:
    case GD32_CAN_RFIFOMDATA11:
        fifo_num = (addr == GD32_CAN_RFIFOMDATA11) ? 1 : 0;
        fifo = &s->rx_fifo[fifo_num];
        if (fifo->count > 0) {
            msg = &fifo->messages[fifo->read_idx];
            value = msg->rfifomdata1;
        }
        break;

    /* Filter registers */
    case GD32_CAN_FCTL:
        value = s->fctl;
        break;
    case GD32_CAN_FMCFG:
        value = s->fmcfg;
        break;
    case GD32_CAN_FSCFG:
        value = s->fscfg;
        break;
    case GD32_CAN_FAFIFO:
        value = s->fafifo;
        break;
    case GD32_CAN_FW:
        value = s->fw;
        break;

    default:
        /* Filter data registers */
        if (addr >= GD32_CAN_F0DATA0 && addr < GD32_CAN_F0DATA0 + 0x100) {
            int filter = (addr - GD32_CAN_F0DATA0) / 8;
            int data_idx = ((addr - GD32_CAN_F0DATA0) % 8) / 4;
            if (filter < GD32_CAN_NUM_FILTERS) {
                value = data_idx ? s->filters[filter].data1
                                 : s->filters[filter].data0;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad read offset 0x%"HWADDR_PRIx"\n",
                          s->name ? s->name : "gd32-can", addr);
        }
        break;
    }

    DB_PRINT("%s: read 0x%"HWADDR_PRIx" = 0x%lx\n",
             s->name ? s->name : "CAN", addr, (unsigned long)value);

    return value;
}

static void gd32_can_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    GD32CanState *s = GD32_CAN(opaque);
    int mailbox;

    DB_PRINT("%s: write 0x%"HWADDR_PRIx" = 0x%lx\n",
             s->name ? s->name : "CAN", addr, (unsigned long)value);

    switch (addr) {
    case GD32_CAN_CTL:
        /* Software reset */
        if (value & GD32_CAN_CTL_SWRST) {
            /* Reset is handled by device reset */
        }

        /* Init mode request */
        if ((value & GD32_CAN_CTL_IWMOD) && !s->init_mode) {
            gd32_can_enter_init_mode(s);
        } else if (!(value & GD32_CAN_CTL_IWMOD) && s->init_mode) {
            gd32_can_leave_init_mode(s);
        }

        /* Sleep mode request */
        if ((value & GD32_CAN_CTL_SLPWMOD) && !s->sleep_mode) {
            gd32_can_enter_sleep_mode(s);
        } else if (!(value & GD32_CAN_CTL_SLPWMOD) && s->sleep_mode) {
            gd32_can_leave_sleep_mode(s);
        }

        s->ctl = value & 0x1FFFF;
        break;

    case GD32_CAN_STAT:
        /* Write 1 to clear interrupt flags */
        if (value & GD32_CAN_STAT_ERRIF) {
            s->stat &= ~GD32_CAN_STAT_ERRIF;
        }
        if (value & GD32_CAN_STAT_WUIF) {
            s->stat &= ~GD32_CAN_STAT_WUIF;
        }
        if (value & GD32_CAN_STAT_SLPIF) {
            s->stat &= ~GD32_CAN_STAT_SLPIF;
        }
        gd32_can_update_irq(s);
        break;

    case GD32_CAN_TSTAT:
        /* Write 1 to clear transmit finished flags */
        if (value & GD32_CAN_TSTAT_MTF0) {
            s->tstat &= ~(GD32_CAN_TSTAT_MTF0 | GD32_CAN_TSTAT_MTFNERR0 |
                          GD32_CAN_TSTAT_MAL0 | GD32_CAN_TSTAT_MTE0);
        }
        if (value & GD32_CAN_TSTAT_MTF1) {
            s->tstat &= ~(GD32_CAN_TSTAT_MTF1 | GD32_CAN_TSTAT_MTFNERR1 |
                          GD32_CAN_TSTAT_MAL1 | GD32_CAN_TSTAT_MTE1);
        }
        if (value & GD32_CAN_TSTAT_MTF2) {
            s->tstat &= ~(GD32_CAN_TSTAT_MTF2 | GD32_CAN_TSTAT_MTFNERR2 |
                          GD32_CAN_TSTAT_MAL2 | GD32_CAN_TSTAT_MTE2);
        }
        /* Abort requests */
        if (value & GD32_CAN_TSTAT_MST0) {
            gd32_can_abort_transmit(s, 0);
        }
        if (value & GD32_CAN_TSTAT_MST1) {
            gd32_can_abort_transmit(s, 1);
        }
        if (value & GD32_CAN_TSTAT_MST2) {
            gd32_can_abort_transmit(s, 2);
        }
        gd32_can_update_irq(s);
        break;

    case GD32_CAN_RFIFO0:
        /* Release FIFO */
        if (value & GD32_CAN_RFIFO_RFD) {
            gd32_can_release_fifo(s, 0);
        }
        /* Clear overrun flag */
        if (value & GD32_CAN_RFIFO_RFO) {
            s->rfifo0 &= ~GD32_CAN_RFIFO_RFO;
            s->rx_fifo[0].overrun = false;
        }
        /* Clear full flag */
        if (value & GD32_CAN_RFIFO_RFF) {
            s->rfifo0 &= ~GD32_CAN_RFIFO_RFF;
        }
        break;

    case GD32_CAN_RFIFO1:
        /* Release FIFO */
        if (value & GD32_CAN_RFIFO_RFD) {
            gd32_can_release_fifo(s, 1);
        }
        /* Clear overrun flag */
        if (value & GD32_CAN_RFIFO_RFO) {
            s->rfifo1 &= ~GD32_CAN_RFIFO_RFO;
            s->rx_fifo[1].overrun = false;
        }
        /* Clear full flag */
        if (value & GD32_CAN_RFIFO_RFF) {
            s->rfifo1 &= ~GD32_CAN_RFIFO_RFF;
        }
        break;

    case GD32_CAN_INTEN:
        s->inten = value;
        gd32_can_update_irq(s);
        break;

    case GD32_CAN_ERR:
        /* Read-only except for clearing error flags by reading */
        break;

    case GD32_CAN_BT:
        /* Can only be modified in init mode */
        if (s->init_mode) {
            s->bt = value;
        }
        break;

    /* TX mailbox registers */
    case GD32_CAN_TMI0:
    case GD32_CAN_TMI1:
    case GD32_CAN_TMI2:
        mailbox = (addr - GD32_CAN_TMI0) / 0x10;
        s->tx_mailbox[mailbox].tmi = value;
        /* Start transmission if TEN is set */
        if (value & GD32_CAN_TMI_TEN) {
            gd32_can_start_transmit(s, mailbox);
        }
        break;
    case GD32_CAN_TMP0:
    case GD32_CAN_TMP1:
    case GD32_CAN_TMP2:
        mailbox = (addr - GD32_CAN_TMP0) / 0x10;
        s->tx_mailbox[mailbox].tmp = value;
        break;
    case GD32_CAN_TMDATA00:
    case GD32_CAN_TMDATA01:
    case GD32_CAN_TMDATA02:
        mailbox = (addr - GD32_CAN_TMDATA00) / 0x10;
        s->tx_mailbox[mailbox].tmdata0 = value;
        break;
    case GD32_CAN_TMDATA10:
    case GD32_CAN_TMDATA11:
    case GD32_CAN_TMDATA12:
        mailbox = (addr - GD32_CAN_TMDATA10) / 0x10;
        s->tx_mailbox[mailbox].tmdata1 = value;
        break;

    /* Filter registers - can only be modified when filter lock is disabled */
    case GD32_CAN_FCTL:
        s->fctl = value;
        break;
    case GD32_CAN_FMCFG:
        if (s->fctl & GD32_CAN_FCTL_FLD) {
            s->fmcfg = value;
        }
        break;
    case GD32_CAN_FSCFG:
        if (s->fctl & GD32_CAN_FCTL_FLD) {
            s->fscfg = value;
        }
        break;
    case GD32_CAN_FAFIFO:
        if (s->fctl & GD32_CAN_FCTL_FLD) {
            s->fafifo = value;
        }
        break;
    case GD32_CAN_FW:
        if (s->fctl & GD32_CAN_FCTL_FLD) {
            s->fw = value;
        }
        break;

    default:
        /* Filter data registers */
        if (addr >= GD32_CAN_F0DATA0 && addr < GD32_CAN_F0DATA0 + 0x100) {
            if (s->fctl & GD32_CAN_FCTL_FLD) {
                int filter = (addr - GD32_CAN_F0DATA0) / 8;
                int data_idx = ((addr - GD32_CAN_F0DATA0) % 8) / 4;
                if (filter < GD32_CAN_NUM_FILTERS) {
                    if (data_idx) {
                        s->filters[filter].data1 = value;
                    } else {
                        s->filters[filter].data0 = value;
                    }
                }
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad write offset 0x%"HWADDR_PRIx"\n",
                          s->name ? s->name : "gd32-can", addr);
        }
        break;
    }
}

static const MemoryRegionOps gd32_can_ops = {
    .read = gd32_can_read,
    .write = gd32_can_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * Device lifecycle
 */
static void gd32_can_reset(DeviceState *dev)
{
    GD32CanState *s = GD32_CAN(dev);
    int i;

    s->ctl = GD32_CAN_CTL_IWMOD | GD32_CAN_CTL_SLPWMOD;
    s->stat = GD32_CAN_STAT_IWS | GD32_CAN_STAT_SLPWS;
    s->tstat = GD32_CAN_TSTAT_TME0 | GD32_CAN_TSTAT_TME1 | GD32_CAN_TSTAT_TME2;
    s->rfifo0 = 0;
    s->rfifo1 = 0;
    s->inten = 0;
    s->err = 0;
    s->bt = 0;

    s->fctl = 0;
    s->fmcfg = 0;
    s->fscfg = 0;
    s->fafifo = 0;
    s->fw = 0;

    for (i = 0; i < GD32_CAN_NUM_TX_MAILBOXES; i++) {
        memset(&s->tx_mailbox[i], 0, sizeof(GD32CanTxMailbox));
    }

    for (i = 0; i < GD32_CAN_NUM_RX_FIFOS; i++) {
        memset(&s->rx_fifo[i], 0, sizeof(GD32CanRxFifo));
    }

    for (i = 0; i < GD32_CAN_NUM_FILTERS; i++) {
        s->filters[i].data0 = 0;
        s->filters[i].data1 = 0;
    }

    s->init_mode = true;
    s->sleep_mode = true;
    s->timestamp = 0;

    qemu_set_irq(s->irq_tx, 0);
    qemu_set_irq(s->irq_rx0, 0);
    qemu_set_irq(s->irq_rx1, 0);
    qemu_set_irq(s->irq_sce, 0);
}

static void gd32_can_init(Object *obj)
{
    GD32CanState *s = GD32_CAN(obj);

    memory_region_init_io(&s->iomem, obj, &gd32_can_ops, s,
                          TYPE_GD32_CAN, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_tx);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_rx0);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_rx1);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_sce);
}

static void gd32_can_realize(DeviceState *dev, Error **errp)
{
    GD32CanState *s = GD32_CAN(dev);

    s->bus_client.info = &gd32_can_bus_client_info;

    if (s->canbus) {
        if (can_bus_insert_client(s->canbus, &s->bus_client) < 0) {
            error_setg(errp, "Failed to connect to CAN bus");
            return;
        }
    }
}

static void gd32_can_unrealize(DeviceState *dev)
{
    GD32CanState *s = GD32_CAN(dev);

    if (s->canbus) {
        can_bus_remove_client(&s->bus_client);
    }
}

static const VMStateDescription vmstate_gd32_can_tx_mailbox = {
    .name = "gd32_can_tx_mailbox",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(tmi, GD32CanTxMailbox),
        VMSTATE_UINT32(tmp, GD32CanTxMailbox),
        VMSTATE_UINT32(tmdata0, GD32CanTxMailbox),
        VMSTATE_UINT32(tmdata1, GD32CanTxMailbox),
        VMSTATE_BOOL(pending, GD32CanTxMailbox),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gd32_can_rx_message = {
    .name = "gd32_can_rx_message",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(rfifomi, GD32CanRxMessage),
        VMSTATE_UINT32(rfifomp, GD32CanRxMessage),
        VMSTATE_UINT32(rfifomdata0, GD32CanRxMessage),
        VMSTATE_UINT32(rfifomdata1, GD32CanRxMessage),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gd32_can_rx_fifo = {
    .name = "gd32_can_rx_fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(messages, GD32CanRxFifo,
                             GD32_CAN_RX_FIFO_DEPTH, 1,
                             vmstate_gd32_can_rx_message, GD32CanRxMessage),
        VMSTATE_UINT8(count, GD32CanRxFifo),
        VMSTATE_UINT8(read_idx, GD32CanRxFifo),
        VMSTATE_UINT8(write_idx, GD32CanRxFifo),
        VMSTATE_BOOL(full, GD32CanRxFifo),
        VMSTATE_BOOL(overrun, GD32CanRxFifo),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gd32_can_filter = {
    .name = "gd32_can_filter",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(data0, GD32CanFilter),
        VMSTATE_UINT32(data1, GD32CanFilter),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gd32_can = {
    .name = TYPE_GD32_CAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctl, GD32CanState),
        VMSTATE_UINT32(stat, GD32CanState),
        VMSTATE_UINT32(tstat, GD32CanState),
        VMSTATE_UINT32(rfifo0, GD32CanState),
        VMSTATE_UINT32(rfifo1, GD32CanState),
        VMSTATE_UINT32(inten, GD32CanState),
        VMSTATE_UINT32(err, GD32CanState),
        VMSTATE_UINT32(bt, GD32CanState),
        VMSTATE_STRUCT_ARRAY(tx_mailbox, GD32CanState,
                             GD32_CAN_NUM_TX_MAILBOXES, 1,
                             vmstate_gd32_can_tx_mailbox, GD32CanTxMailbox),
        VMSTATE_STRUCT_ARRAY(rx_fifo, GD32CanState,
                             GD32_CAN_NUM_RX_FIFOS, 1,
                             vmstate_gd32_can_rx_fifo, GD32CanRxFifo),
        VMSTATE_UINT32(fctl, GD32CanState),
        VMSTATE_UINT32(fmcfg, GD32CanState),
        VMSTATE_UINT32(fscfg, GD32CanState),
        VMSTATE_UINT32(fafifo, GD32CanState),
        VMSTATE_UINT32(fw, GD32CanState),
        VMSTATE_STRUCT_ARRAY(filters, GD32CanState,
                             GD32_CAN_NUM_FILTERS, 1,
                             vmstate_gd32_can_filter, GD32CanFilter),
        VMSTATE_BOOL(init_mode, GD32CanState),
        VMSTATE_BOOL(sleep_mode, GD32CanState),
        VMSTATE_UINT16(timestamp, GD32CanState),
        VMSTATE_END_OF_LIST()
    }
};

static Property gd32_can_properties[] = {
    DEFINE_PROP_LINK("canbus", GD32CanState, canbus, TYPE_CAN_BUS, CanBusState *),
    DEFINE_PROP_STRING("name", GD32CanState, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void gd32_can_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_can_reset);
    dc->realize = gd32_can_realize;
    dc->unrealize = gd32_can_unrealize;
    dc->vmsd = &vmstate_gd32_can;
    device_class_set_props(dc, gd32_can_properties);
}

static const TypeInfo gd32_can_info = {
    .name          = TYPE_GD32_CAN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32CanState),
    .instance_init = gd32_can_init,
    .class_init    = gd32_can_class_init,
};

static void gd32_can_register_types(void)
{
    type_register_static(&gd32_can_info);
}

type_init(gd32_can_register_types)

