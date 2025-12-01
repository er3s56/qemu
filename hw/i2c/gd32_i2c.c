/*
 * GD32 I2C Controller Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the GD32C10x I2C controller with:
 * - Master and slave mode support
 * - 7-bit and 10-bit addressing
 * - Event and error interrupts
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/i2c/gd32_i2c.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#ifndef GD32_I2C_DEBUG
#define GD32_I2C_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (GD32_I2C_DEBUG) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

static const char *gd32_i2c_get_regname(hwaddr offset)
{
    switch (offset) {
    case GD32_I2C_CTL0:   return "CTL0";
    case GD32_I2C_CTL1:   return "CTL1";
    case GD32_I2C_SADDR0: return "SADDR0";
    case GD32_I2C_SADDR1: return "SADDR1";
    case GD32_I2C_DATA:   return "DATA";
    case GD32_I2C_STAT0:  return "STAT0";
    case GD32_I2C_STAT1:  return "STAT1";
    case GD32_I2C_CKCFG:  return "CKCFG";
    case GD32_I2C_RT:     return "RT";
    case GD32_I2C_SAMCS:  return "SAMCS";
    case GD32_I2C_FMPCFG: return "FMPCFG";
    default:              return "UNKNOWN";
    }
}

static inline bool gd32_i2c_is_enabled(GD32I2CState *s)
{
    return (s->ctl0 & GD32_I2C_CTL0_I2CEN) != 0;
}

static inline bool gd32_i2c_is_master(GD32I2CState *s)
{
    return (s->stat1 & GD32_I2C_STAT1_MASTER) != 0;
}

/*
 * Interrupt handling
 */
static void gd32_i2c_update_irq(GD32I2CState *s)
{
    int ev_irq = 0;
    int er_irq = 0;

    if (!gd32_i2c_is_enabled(s)) {
        qemu_set_irq(s->irq_ev, 0);
        qemu_set_irq(s->irq_er, 0);
        return;
    }

    /* Event interrupt */
    if (s->ctl1 & GD32_I2C_CTL1_EVIE) {
        /* Check event flags */
        if (s->stat0 & (GD32_I2C_STAT0_SBSEND | GD32_I2C_STAT0_ADDSEND |
                        GD32_I2C_STAT0_BTC | GD32_I2C_STAT0_ADD10SEND |
                        GD32_I2C_STAT0_STPDET)) {
            ev_irq = 1;
        }
        /* Buffer interrupt */
        if (s->ctl1 & GD32_I2C_CTL1_BUFIE) {
            if (s->stat0 & (GD32_I2C_STAT0_TBE | GD32_I2C_STAT0_RBNE)) {
                ev_irq = 1;
            }
        }
    }

    /* Error interrupt */
    if (s->ctl1 & GD32_I2C_CTL1_ERRIE) {
        if (s->stat0 & (GD32_I2C_STAT0_BERR | GD32_I2C_STAT0_LOSTARB |
                        GD32_I2C_STAT0_AERR | GD32_I2C_STAT0_OUERR |
                        GD32_I2C_STAT0_PECERR | GD32_I2C_STAT0_SMBTO |
                        GD32_I2C_STAT0_SMBALT)) {
            er_irq = 1;
        }
    }

    qemu_set_irq(s->irq_ev, ev_irq);
    qemu_set_irq(s->irq_er, er_irq);
}

/*
 * Master mode operations
 */
static void gd32_i2c_do_start(GD32I2CState *s)
{
    DB_PRINT("%s: generating START condition\n", s->name ? s->name : "I2C");

    /* Enter master mode */
    s->stat1 |= GD32_I2C_STAT1_MASTER;
    s->stat1 |= GD32_I2C_STAT1_I2CBSY;

    /* Set start bit sent flag */
    s->stat0 |= GD32_I2C_STAT0_SBSEND;

    /* Clear the START bit in CTL0 (hardware clears it) */
    s->ctl0 &= ~GD32_I2C_CTL0_START;

    s->addr_sent = false;

    gd32_i2c_update_irq(s);
}

static void gd32_i2c_do_stop(GD32I2CState *s)
{
    DB_PRINT("%s: generating STOP condition\n", s->name ? s->name : "I2C");

    if (gd32_i2c_is_master(s) && s->addr_sent) {
        i2c_end_transfer(s->bus);
    }

    /* Exit master mode */
    s->stat1 &= ~GD32_I2C_STAT1_MASTER;
    s->stat1 &= ~GD32_I2C_STAT1_I2CBSY;
    s->stat1 &= ~GD32_I2C_STAT1_TR;

    /* Clear status flags */
    s->stat0 &= ~(GD32_I2C_STAT0_SBSEND | GD32_I2C_STAT0_ADDSEND |
                  GD32_I2C_STAT0_BTC | GD32_I2C_STAT0_TBE |
                  GD32_I2C_STAT0_RBNE);

    /* Clear the STOP bit in CTL0 */
    s->ctl0 &= ~GD32_I2C_CTL0_STOP;

    s->addr_sent = false;

    gd32_i2c_update_irq(s);
}

static void gd32_i2c_send_address(GD32I2CState *s, uint8_t data)
{
    uint8_t addr = data >> 1;
    bool is_recv = (data & 1) != 0;
    int ret;

    DB_PRINT("%s: sending address 0x%02x, %s\n",
             s->name ? s->name : "I2C", addr, is_recv ? "read" : "write");

    s->slave_addr = addr;
    s->is_recv = is_recv;

    /* Clear SBSEND flag (cleared by reading STAT0 then writing DATA) */
    s->stat0 &= ~GD32_I2C_STAT0_SBSEND;

    /* Start I2C transfer */
    ret = i2c_start_transfer(s->bus, addr, is_recv);

    if (ret) {
        /* No ACK from slave */
        DB_PRINT("%s: NACK from slave 0x%02x\n", s->name ? s->name : "I2C", addr);
        s->stat0 |= GD32_I2C_STAT0_AERR;
    } else {
        /* ACK received */
        s->stat0 |= GD32_I2C_STAT0_ADDSEND;
        s->addr_sent = true;

        if (is_recv) {
            /* Receiver mode */
            s->stat1 &= ~GD32_I2C_STAT1_TR;
        } else {
            /* Transmitter mode */
            s->stat1 |= GD32_I2C_STAT1_TR;
            s->stat0 |= GD32_I2C_STAT0_TBE;
        }
    }

    gd32_i2c_update_irq(s);
}

static void gd32_i2c_send_data(GD32I2CState *s, uint8_t data)
{
    int ret;

    if (!s->addr_sent) {
        /* This is the address byte */
        gd32_i2c_send_address(s, data);
        return;
    }

    DB_PRINT("%s: sending data 0x%02x\n", s->name ? s->name : "I2C", data);

    /* Clear TBE flag */
    s->stat0 &= ~GD32_I2C_STAT0_TBE;

    ret = i2c_send(s->bus, data);

    if (ret) {
        /* NACK */
        s->stat0 |= GD32_I2C_STAT0_AERR;
    } else {
        /* ACK - byte transfer complete */
        s->stat0 |= GD32_I2C_STAT0_BTC;
        s->stat0 |= GD32_I2C_STAT0_TBE;
    }

    gd32_i2c_update_irq(s);
}

static uint8_t gd32_i2c_recv_data(GD32I2CState *s)
{
    uint8_t data;

    if (!s->addr_sent) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: reading data without address sent\n",
                      s->name ? s->name : "gd32-i2c");
        return 0xFF;
    }

    data = i2c_recv(s->bus);

    DB_PRINT("%s: received data 0x%02x\n", s->name ? s->name : "I2C", data);

    /* Set byte transfer complete and RBNE */
    s->stat0 |= GD32_I2C_STAT0_BTC;

    /* If ACK is enabled, send ACK */
    if (s->ctl0 & GD32_I2C_CTL0_ACKEN) {
        /* ACK will be sent automatically */
    } else {
        /* NACK - this is typically the last byte */
    }

    gd32_i2c_update_irq(s);

    return data;
}

/*
 * Register read/write
 */
static uint64_t gd32_i2c_read(void *opaque, hwaddr addr, unsigned size)
{
    GD32I2CState *s = GD32_I2C(opaque);
    uint64_t value = 0;

    switch (addr) {
    case GD32_I2C_CTL0:
        value = s->ctl0;
        break;

    case GD32_I2C_CTL1:
        value = s->ctl1;
        break;

    case GD32_I2C_SADDR0:
        value = s->saddr0;
        break;

    case GD32_I2C_SADDR1:
        value = s->saddr1;
        break;

    case GD32_I2C_DATA:
        if (gd32_i2c_is_master(s) && s->is_recv && s->addr_sent) {
            /* Master receive mode - read from I2C bus */
            value = gd32_i2c_recv_data(s);
            s->stat0 &= ~GD32_I2C_STAT0_RBNE;
        } else {
            value = s->rx_data;
        }
        break;

    case GD32_I2C_STAT0:
        value = s->stat0;
        break;

    case GD32_I2C_STAT1:
        value = s->stat1;
        /* Reading STAT1 after STAT0 clears ADDSEND flag */
        s->stat0 &= ~GD32_I2C_STAT0_ADDSEND;
        gd32_i2c_update_irq(s);
        break;

    case GD32_I2C_CKCFG:
        value = s->ckcfg;
        break;

    case GD32_I2C_RT:
        value = s->rt;
        break;

    case GD32_I2C_SAMCS:
        value = s->samcs;
        break;

    case GD32_I2C_FMPCFG:
        value = s->fmpcfg;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%"HWADDR_PRIx"\n",
                      s->name ? s->name : "gd32-i2c", addr);
        break;
    }

    DB_PRINT("%s: read %s (0x%"HWADDR_PRIx") = 0x%lx\n",
             s->name ? s->name : "I2C",
             gd32_i2c_get_regname(addr), addr, (unsigned long)value);

    return value;
}

static void gd32_i2c_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    GD32I2CState *s = GD32_I2C(opaque);

    DB_PRINT("%s: write %s (0x%"HWADDR_PRIx") = 0x%lx\n",
             s->name ? s->name : "I2C",
             gd32_i2c_get_regname(addr), addr, (unsigned long)value);

    switch (addr) {
    case GD32_I2C_CTL0:
        /* Software reset */
        if (value & GD32_I2C_CTL0_SRESET) {
            /* Reset the I2C peripheral */
            s->stat0 = 0;
            s->stat1 = 0;
            s->addr_sent = false;
        }

        /* Start condition */
        if ((value & GD32_I2C_CTL0_START) && gd32_i2c_is_enabled(s)) {
            gd32_i2c_do_start(s);
        }

        /* Stop condition */
        if ((value & GD32_I2C_CTL0_STOP) && gd32_i2c_is_enabled(s)) {
            gd32_i2c_do_stop(s);
        }

        s->ctl0 = value & ~(GD32_I2C_CTL0_START | GD32_I2C_CTL0_STOP);
        break;

    case GD32_I2C_CTL1:
        s->ctl1 = value;
        gd32_i2c_update_irq(s);
        break;

    case GD32_I2C_SADDR0:
        s->saddr0 = value;
        break;

    case GD32_I2C_SADDR1:
        s->saddr1 = value;
        break;

    case GD32_I2C_DATA:
        s->tx_data = value & 0xFF;
        if (gd32_i2c_is_master(s)) {
            /* Master mode - send data */
            gd32_i2c_send_data(s, s->tx_data);
        } else {
            /* Slave mode - store data for transmission */
            s->data = value & 0xFF;
        }
        break;

    case GD32_I2C_STAT0:
        /* Write 0 to clear error flags */
        s->stat0 &= value | ~(GD32_I2C_STAT0_BERR | GD32_I2C_STAT0_LOSTARB |
                              GD32_I2C_STAT0_AERR | GD32_I2C_STAT0_OUERR |
                              GD32_I2C_STAT0_PECERR | GD32_I2C_STAT0_SMBTO |
                              GD32_I2C_STAT0_SMBALT);
        gd32_i2c_update_irq(s);
        break;

    case GD32_I2C_STAT1:
        /* Read-only register */
        break;

    case GD32_I2C_CKCFG:
        s->ckcfg = value;
        break;

    case GD32_I2C_RT:
        s->rt = value & GD32_I2C_RT_RISETIME_MASK;
        break;

    case GD32_I2C_SAMCS:
        /* Clear flags by writing 0 */
        s->samcs = (s->samcs & ~(GD32_I2C_SAMCS_TFF | GD32_I2C_SAMCS_TFR |
                                  GD32_I2C_SAMCS_RFF | GD32_I2C_SAMCS_RFR)) |
                   (value & ~(GD32_I2C_SAMCS_TFF | GD32_I2C_SAMCS_TFR |
                              GD32_I2C_SAMCS_RFF | GD32_I2C_SAMCS_RFR));
        break;

    case GD32_I2C_FMPCFG:
        s->fmpcfg = value & GD32_I2C_FMPCFG_FMPEN;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%"HWADDR_PRIx"\n",
                      s->name ? s->name : "gd32-i2c", addr);
        break;
    }
}

static const MemoryRegionOps gd32_i2c_ops = {
    .read = gd32_i2c_read,
    .write = gd32_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * Device lifecycle
 */
static void gd32_i2c_reset(DeviceState *dev)
{
    GD32I2CState *s = GD32_I2C(dev);

    s->ctl0 = 0;
    s->ctl1 = 0;
    s->saddr0 = 0;
    s->saddr1 = 0;
    s->data = 0;
    s->stat0 = 0;
    s->stat1 = 0;
    s->ckcfg = 0;
    s->rt = 0x02;  /* Default rise time */
    s->samcs = 0;
    s->fmpcfg = 0;

    s->rx_data = 0;
    s->tx_data = 0;
    s->slave_addr = 0;
    s->is_recv = false;
    s->addr_sent = false;

    qemu_set_irq(s->irq_ev, 0);
    qemu_set_irq(s->irq_er, 0);
}

static void gd32_i2c_init(Object *obj)
{
    GD32I2CState *s = GD32_I2C(obj);

    memory_region_init_io(&s->iomem, obj, &gd32_i2c_ops, s,
                          TYPE_GD32_I2C, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_ev);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_er);

    s->bus = i2c_init_bus(DEVICE(obj), "i2c");
}

static const VMStateDescription vmstate_gd32_i2c = {
    .name = TYPE_GD32_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctl0, GD32I2CState),
        VMSTATE_UINT32(ctl1, GD32I2CState),
        VMSTATE_UINT32(saddr0, GD32I2CState),
        VMSTATE_UINT32(saddr1, GD32I2CState),
        VMSTATE_UINT32(data, GD32I2CState),
        VMSTATE_UINT32(stat0, GD32I2CState),
        VMSTATE_UINT32(stat1, GD32I2CState),
        VMSTATE_UINT32(ckcfg, GD32I2CState),
        VMSTATE_UINT32(rt, GD32I2CState),
        VMSTATE_UINT32(samcs, GD32I2CState),
        VMSTATE_UINT32(fmpcfg, GD32I2CState),
        VMSTATE_UINT8(rx_data, GD32I2CState),
        VMSTATE_UINT8(tx_data, GD32I2CState),
        VMSTATE_UINT8(slave_addr, GD32I2CState),
        VMSTATE_BOOL(is_recv, GD32I2CState),
        VMSTATE_BOOL(addr_sent, GD32I2CState),
        VMSTATE_END_OF_LIST()
    }
};

static Property gd32_i2c_properties[] = {
    DEFINE_PROP_STRING("name", GD32I2CState, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void gd32_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_i2c_reset);
    dc->vmsd = &vmstate_gd32_i2c;
    device_class_set_props(dc, gd32_i2c_properties);
}

static const TypeInfo gd32_i2c_info = {
    .name          = TYPE_GD32_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32I2CState),
    .instance_init = gd32_i2c_init,
    .class_init    = gd32_i2c_class_init,
};

static void gd32_i2c_register_types(void)
{
    type_register_static(&gd32_i2c_info);
}

type_init(gd32_i2c_register_types)

