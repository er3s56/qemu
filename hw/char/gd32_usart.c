/*
 * GD32 USART
 *
 * Copyright (c) 2025
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Based on stm32f2xx_usart.c, adapted for GD32C10x register layout.
 */

#include "qemu/osdep.h"
#include "hw/char/gd32_usart.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef GD32_USART_ERR_DEBUG
#define GD32_USART_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (GD32_USART_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static int gd32_usart_can_receive(void *opaque)
{
    GD32UsartState *s = opaque;

    /* Can receive if RBNE (Read Buffer Not Empty) is not set */
    if (!(s->stat0 & GD32_USART_STAT0_RBNE)) {
        return 1;
    }

    return 0;
}

static void gd32_usart_update_irq(GD32UsartState *s)
{
    uint32_t mask = 0;

    /*
     * Check interrupt conditions:
     * - TBE (Transmit Buffer Empty) with TBEIE enabled
     * - TC (Transmission Complete) with TCIE enabled
     * - RBNE (Read Buffer Not Empty) with RBNEIE enabled
     */
    if ((s->stat0 & GD32_USART_STAT0_TBE) && (s->ctl0 & GD32_USART_CTL0_TBEIE)) {
        mask = 1;
    }
    if ((s->stat0 & GD32_USART_STAT0_TC) && (s->ctl0 & GD32_USART_CTL0_TCIE)) {
        mask = 1;
    }
    if ((s->stat0 & GD32_USART_STAT0_RBNE) && (s->ctl0 & GD32_USART_CTL0_RBNEIE)) {
        mask = 1;
    }

    qemu_set_irq(s->irq, mask);
}

static void gd32_usart_receive(void *opaque, const uint8_t *buf, int size)
{
    GD32UsartState *s = opaque;

    /* Check if USART is enabled and receiver is enabled */
    if (!(s->ctl0 & GD32_USART_CTL0_UEN) || !(s->ctl0 & GD32_USART_CTL0_REN)) {
        DB_PRINT("Dropping chars - USART not enabled\n");
        return;
    }

    s->data = *buf;
    s->stat0 |= GD32_USART_STAT0_RBNE;

    gd32_usart_update_irq(s);

    DB_PRINT("Received: 0x%02x '%c'\n", s->data, (char)s->data);
}

static void gd32_usart_reset(DeviceState *dev)
{
    GD32UsartState *s = GD32_USART(dev);

    s->stat0 = GD32_USART_STAT0_RESET;
    s->data = 0x00000000;
    s->baud = 0x00000000;
    s->ctl0 = 0x00000000;
    s->ctl1 = 0x00000000;
    s->ctl2 = 0x00000000;
    s->gp = 0x00000000;

    gd32_usart_update_irq(s);
}

static uint64_t gd32_usart_read(void *opaque, hwaddr addr, unsigned int size)
{
    GD32UsartState *s = opaque;
    uint64_t retvalue;

    DB_PRINT("Read 0x%"HWADDR_PRIx"\n", addr);

    switch (addr) {
    case GD32_USART_STAT0:
        retvalue = s->stat0;
        qemu_chr_fe_accept_input(&s->chr);
        return retvalue;

    case GD32_USART_DATA:
        DB_PRINT("DATA: 0x%" PRIx32 " '%c'\n", s->data, (char)s->data);
        retvalue = s->data & 0x1FF;  /* Only 9 bits valid */
        s->stat0 &= ~GD32_USART_STAT0_RBNE;
        qemu_chr_fe_accept_input(&s->chr);
        gd32_usart_update_irq(s);
        return retvalue;

    case GD32_USART_BAUD:
        return s->baud;

    case GD32_USART_CTL0:
        return s->ctl0;

    case GD32_USART_CTL1:
        return s->ctl1;

    case GD32_USART_CTL2:
        return s->ctl2;

    case GD32_USART_GP:
        return s->gp;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }
}

static void gd32_usart_write(void *opaque, hwaddr addr,
                             uint64_t val64, unsigned int size)
{
    GD32UsartState *s = opaque;
    uint32_t value = val64;
    unsigned char ch;

    DB_PRINT("Write 0x%" PRIx32 " to 0x%"HWADDR_PRIx"\n", value, addr);

    switch (addr) {
    case GD32_USART_STAT0:
        /*
         * Some bits are read-only or write-0-to-clear.
         * TBE is always set (I/O is synchronous).
         */
        if (value <= 0x3FF) {
            s->stat0 = value | GD32_USART_STAT0_TBE;
        } else {
            s->stat0 &= value;
        }
        gd32_usart_update_irq(s);
        return;

    case GD32_USART_DATA:
        if (value < 0xF000) {
            ch = value & 0xFF;
            /*
             * Write character to backend.
             * Note: This is synchronous and may block.
             */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            /*
             * Set TC (Transmission Complete) since I/O is synchronous.
             * Software can clear TC by writing 0 to STAT0.
             */
            s->stat0 |= GD32_USART_STAT0_TC;
            gd32_usart_update_irq(s);
        }
        return;

    case GD32_USART_BAUD:
        s->baud = value;
        return;

    case GD32_USART_CTL0:
        s->ctl0 = value;
        gd32_usart_update_irq(s);
        return;

    case GD32_USART_CTL1:
        s->ctl1 = value;
        return;

    case GD32_USART_CTL2:
        s->ctl2 = value;
        return;

    case GD32_USART_GP:
        s->gp = value;
        return;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps gd32_usart_ops = {
    .read = gd32_usart_read,
    .write = gd32_usart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static Property gd32_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", GD32UsartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void gd32_usart_init(Object *obj)
{
    GD32UsartState *s = GD32_USART(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &gd32_usart_ops, s,
                          TYPE_GD32_USART, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void gd32_usart_realize(DeviceState *dev, Error **errp)
{
    GD32UsartState *s = GD32_USART(dev);

    qemu_chr_fe_set_handlers(&s->chr, gd32_usart_can_receive,
                             gd32_usart_receive, NULL, NULL,
                             s, NULL, true);
}

static void gd32_usart_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_usart_reset);
    device_class_set_props(dc, gd32_usart_properties);
    dc->realize = gd32_usart_realize;
}

static const TypeInfo gd32_usart_info = {
    .name          = TYPE_GD32_USART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32UsartState),
    .instance_init = gd32_usart_init,
    .class_init    = gd32_usart_class_init,
};

static void gd32_usart_register_types(void)
{
    type_register_static(&gd32_usart_info);
}

type_init(gd32_usart_register_types)

