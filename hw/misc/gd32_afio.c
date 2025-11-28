/*
 * GD32 AFIO (Alternate Function I/O)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This emulates the GD32C10x AFIO for pin remapping configuration.
 * Note: Actual remapping is not implemented; registers are stored
 * but do not affect peripheral behavior.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/gd32_afio.h"
#include "migration/vmstate.h"

static uint64_t gd32_afio_read(void *opaque, hwaddr addr, unsigned int size)
{
    GD32AfioState *s = GD32_AFIO(opaque);
    uint64_t value = 0;

    switch (addr) {
    case GD32_AFIO_EC:
        value = s->ec;
        break;
    case GD32_AFIO_PCF0:
        value = s->pcf0;
        break;
    case GD32_AFIO_EXTISS0:
        value = s->extiss0;
        break;
    case GD32_AFIO_EXTISS1:
        value = s->extiss1;
        break;
    case GD32_AFIO_EXTISS2:
        value = s->extiss2;
        break;
    case GD32_AFIO_EXTISS3:
        value = s->extiss3;
        break;
    case GD32_AFIO_PCF1:
        value = s->pcf1;
        break;
    case GD32_AFIO_CPSCTL:
        value = s->cpsctl;
        /* CPS_RDY is set when CPS_EN is set */
        if (s->cpsctl & GD32_AFIO_CPSCTL_CPS_EN) {
            value |= GD32_AFIO_CPSCTL_CPS_RDY;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    return value;
}

static void gd32_afio_write(void *opaque, hwaddr addr,
                            uint64_t val64, unsigned int size)
{
    GD32AfioState *s = GD32_AFIO(opaque);
    uint32_t value = val64;

    switch (addr) {
    case GD32_AFIO_EC:
        s->ec = value & (GD32_AFIO_EC_PIN_MASK | GD32_AFIO_EC_PORT_MASK |
                         GD32_AFIO_EC_EOE);
        break;
    case GD32_AFIO_PCF0:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Pin remapping (PCF0) not implemented\n", __func__);
        s->pcf0 = value;
        break;
    case GD32_AFIO_EXTISS0:
        s->extiss0 = value & 0xFFFF;
        break;
    case GD32_AFIO_EXTISS1:
        s->extiss1 = value & 0xFFFF;
        break;
    case GD32_AFIO_EXTISS2:
        s->extiss2 = value & 0xFFFF;
        break;
    case GD32_AFIO_EXTISS3:
        s->extiss3 = value & 0xFFFF;
        break;
    case GD32_AFIO_PCF1:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Pin remapping (PCF1) not implemented\n", __func__);
        s->pcf1 = value;
        break;
    case GD32_AFIO_CPSCTL:
        s->cpsctl = value & GD32_AFIO_CPSCTL_CPS_EN;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps gd32_afio_ops = {
    .read = gd32_afio_read,
    .write = gd32_afio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void gd32_afio_reset(DeviceState *dev)
{
    GD32AfioState *s = GD32_AFIO(dev);

    s->ec = 0x00000000;
    s->pcf0 = 0x00000000;
    s->extiss0 = 0x00000000;
    s->extiss1 = 0x00000000;
    s->extiss2 = 0x00000000;
    s->extiss3 = 0x00000000;
    s->pcf1 = 0x00000000;
    s->cpsctl = 0x00000000;
}

static void gd32_afio_init(Object *obj)
{
    GD32AfioState *s = GD32_AFIO(obj);

    memory_region_init_io(&s->mmio, obj, &gd32_afio_ops, s,
                          TYPE_GD32_AFIO, GD32_AFIO_PERIPHERAL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_gd32_afio = {
    .name = TYPE_GD32_AFIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ec, GD32AfioState),
        VMSTATE_UINT32(pcf0, GD32AfioState),
        VMSTATE_UINT32(extiss0, GD32AfioState),
        VMSTATE_UINT32(extiss1, GD32AfioState),
        VMSTATE_UINT32(extiss2, GD32AfioState),
        VMSTATE_UINT32(extiss3, GD32AfioState),
        VMSTATE_UINT32(pcf1, GD32AfioState),
        VMSTATE_UINT32(cpsctl, GD32AfioState),
        VMSTATE_END_OF_LIST()
    }
};

static void gd32_afio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_afio_reset);
    dc->vmsd = &vmstate_gd32_afio;
}

static const TypeInfo gd32_afio_info = {
    .name          = TYPE_GD32_AFIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32AfioState),
    .instance_init = gd32_afio_init,
    .class_init    = gd32_afio_class_init,
};

static void gd32_afio_register_types(void)
{
    type_register_static(&gd32_afio_info);
}

type_init(gd32_afio_register_types)

