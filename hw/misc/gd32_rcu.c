/*
 * GD32 RCU (Reset and Clock Unit)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This emulates the GD32C10x RCU for clock initialization.
 * Key feature: When a clock source is enabled, the corresponding
 * stabilization flag is immediately set (instant lock simulation).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/gd32_rcu.h"
#include "migration/vmstate.h"

#ifndef GD32_RCU_DEBUG
#define GD32_RCU_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (GD32_RCU_DEBUG) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

/*
 * Update CTL register: set stabilization flags when oscillators are enabled.
 * In real hardware, there's a delay; in QEMU we set them immediately.
 */
static uint32_t gd32_rcu_update_ctl(uint32_t ctl)
{
    uint32_t result = ctl;

    /* IRC8M: enable -> stable */
    if (ctl & GD32_RCU_CTL_IRC8MEN) {
        result |= GD32_RCU_CTL_IRC8MSTB;
    } else {
        result &= ~GD32_RCU_CTL_IRC8MSTB;
    }

    /* HXTAL: enable -> stable */
    if (ctl & GD32_RCU_CTL_HXTALEN) {
        result |= GD32_RCU_CTL_HXTALSTB;
    } else {
        result &= ~GD32_RCU_CTL_HXTALSTB;
    }

    /* PLL: enable -> stable */
    if (ctl & GD32_RCU_CTL_PLLEN) {
        result |= GD32_RCU_CTL_PLLSTB;
    } else {
        result &= ~GD32_RCU_CTL_PLLSTB;
    }

    /* PLL1: enable -> stable */
    if (ctl & GD32_RCU_CTL_PLL1EN) {
        result |= GD32_RCU_CTL_PLL1STB;
    } else {
        result &= ~GD32_RCU_CTL_PLL1STB;
    }

    /* PLL2: enable -> stable */
    if (ctl & GD32_RCU_CTL_PLL2EN) {
        result |= GD32_RCU_CTL_PLL2STB;
    } else {
        result &= ~GD32_RCU_CTL_PLL2STB;
    }

    return result;
}

/*
 * Update CFG0 register: mirror SCS to SCSS (clock switch status).
 * In real hardware, SCSS reflects the actual clock source after switching.
 * We simulate instant switching.
 */
static uint32_t gd32_rcu_update_cfg0(uint32_t cfg0)
{
    uint32_t scs = cfg0 & GD32_RCU_CFG0_SCS_MASK;
    uint32_t result = cfg0 & ~GD32_RCU_CFG0_SCSS_MASK;

    /* Mirror SCS to SCSS (instant clock switch) */
    result |= (scs << GD32_RCU_CFG0_SCSS_SHIFT);

    return result;
}

/*
 * Update RSTSCK register: set IRC40K stable flag when enabled.
 */
static uint32_t gd32_rcu_update_rstsck(uint32_t rstsck)
{
    uint32_t result = rstsck;

    if (rstsck & GD32_RCU_RSTSCK_IRC40KEN) {
        result |= GD32_RCU_RSTSCK_IRC40KSTB;
    } else {
        result &= ~GD32_RCU_RSTSCK_IRC40KSTB;
    }

    return result;
}

/*
 * Update BDCTL register: set LXTAL stable flag when enabled.
 */
static uint32_t gd32_rcu_update_bdctl(uint32_t bdctl)
{
    uint32_t result = bdctl;

    if (bdctl & GD32_RCU_BDCTL_LXTALEN) {
        result |= GD32_RCU_BDCTL_LXTALSTB;
    } else {
        result &= ~GD32_RCU_BDCTL_LXTALSTB;
    }

    return result;
}

/*
 * Update ADDCTL register: set IRC48M stable flag when enabled.
 */
static uint32_t gd32_rcu_update_addctl(uint32_t addctl)
{
    uint32_t result = addctl;

    if (addctl & GD32_RCU_ADDCTL_IRC48MEN) {
        result |= GD32_RCU_ADDCTL_IRC48MSTB;
    } else {
        result &= ~GD32_RCU_ADDCTL_IRC48MSTB;
    }

    return result;
}

static uint64_t gd32_rcu_read(void *opaque, hwaddr addr, unsigned int size)
{
    GD32RcuState *s = GD32_RCU(opaque);
    uint64_t value = 0;

    switch (addr) {
    case GD32_RCU_CTL:
        value = s->ctl;
        break;
    case GD32_RCU_CFG0:
        value = s->cfg0;
        break;
    case GD32_RCU_INT:
        value = s->intr;
        break;
    case GD32_RCU_APB2RST:
        value = s->apb2rst;
        break;
    case GD32_RCU_APB1RST:
        value = s->apb1rst;
        break;
    case GD32_RCU_AHBEN:
        value = s->ahben;
        break;
    case GD32_RCU_APB2EN:
        value = s->apb2en;
        break;
    case GD32_RCU_APB1EN:
        value = s->apb1en;
        break;
    case GD32_RCU_BDCTL:
        value = s->bdctl;
        break;
    case GD32_RCU_RSTSCK:
        value = s->rstsck;
        break;
    case GD32_RCU_AHBRST:
        value = s->ahbrst;
        break;
    case GD32_RCU_CFG1:
        value = s->cfg1;
        break;
    case GD32_RCU_DSV:
        value = s->dsv;
        break;
    case GD32_RCU_ADDCTL:
        value = s->addctl;
        break;
    case GD32_RCU_ADDINT:
        value = s->addint;
        break;
    case GD32_RCU_ADDAPB1RST:
        value = s->addapb1rst;
        break;
    case GD32_RCU_ADDAPB1EN:
        value = s->addapb1en;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    DB_PRINT("Read 0x%08lx from 0x%02lx\n", (unsigned long)value,
             (unsigned long)addr);

    return value;
}

static void gd32_rcu_write(void *opaque, hwaddr addr,
                           uint64_t val64, unsigned int size)
{
    GD32RcuState *s = GD32_RCU(opaque);
    uint32_t value = val64;

    DB_PRINT("Write 0x%08x to 0x%02lx\n", value, (unsigned long)addr);

    switch (addr) {
    case GD32_RCU_CTL:
        s->ctl = gd32_rcu_update_ctl(value);
        break;
    case GD32_RCU_CFG0:
        s->cfg0 = gd32_rcu_update_cfg0(value);
        break;
    case GD32_RCU_INT:
        /* Some bits are write-1-to-clear (interrupt flags) */
        s->intr = value;
        break;
    case GD32_RCU_APB2RST:
        s->apb2rst = value;
        break;
    case GD32_RCU_APB1RST:
        s->apb1rst = value;
        break;
    case GD32_RCU_AHBEN:
        s->ahben = value;
        break;
    case GD32_RCU_APB2EN:
        s->apb2en = value;
        break;
    case GD32_RCU_APB1EN:
        s->apb1en = value;
        break;
    case GD32_RCU_BDCTL:
        s->bdctl = gd32_rcu_update_bdctl(value);
        break;
    case GD32_RCU_RSTSCK:
        s->rstsck = gd32_rcu_update_rstsck(value);
        break;
    case GD32_RCU_AHBRST:
        s->ahbrst = value;
        break;
    case GD32_RCU_CFG1:
        s->cfg1 = value;
        break;
    case GD32_RCU_DSV:
        s->dsv = value;
        break;
    case GD32_RCU_ADDCTL:
        s->addctl = gd32_rcu_update_addctl(value);
        break;
    case GD32_RCU_ADDINT:
        s->addint = value;
        break;
    case GD32_RCU_ADDAPB1RST:
        s->addapb1rst = value;
        break;
    case GD32_RCU_ADDAPB1EN:
        s->addapb1en = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps gd32_rcu_ops = {
    .read = gd32_rcu_read,
    .write = gd32_rcu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void gd32_rcu_reset(DeviceState *dev)
{
    GD32RcuState *s = GD32_RCU(dev);

    /*
     * Reset values from GD32C10x reference manual.
     * IRC8M is enabled and stable by default after reset.
     */
    s->ctl = GD32_RCU_CTL_IRC8MEN | GD32_RCU_CTL_IRC8MSTB;
    s->cfg0 = 0x00000000;
    s->intr = 0x00000000;
    s->apb2rst = 0x00000000;
    s->apb1rst = 0x00000000;
    s->ahben = 0x00000000;
    s->apb2en = 0x00000000;
    s->apb1en = 0x00000000;
    s->bdctl = 0x00000000;
    s->rstsck = 0x0C000000;  /* Reset flags set after power-on */
    s->ahbrst = 0x00000000;
    s->cfg1 = 0x00000000;
    s->dsv = 0x00000000;
    s->addctl = 0x00000000;
    s->addint = 0x00000000;
    s->addapb1rst = 0x00000000;
    s->addapb1en = 0x00000000;
}

static void gd32_rcu_init(Object *obj)
{
    GD32RcuState *s = GD32_RCU(obj);

    memory_region_init_io(&s->mmio, obj, &gd32_rcu_ops, s,
                          TYPE_GD32_RCU, GD32_RCU_PERIPHERAL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription vmstate_gd32_rcu = {
    .name = TYPE_GD32_RCU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctl, GD32RcuState),
        VMSTATE_UINT32(cfg0, GD32RcuState),
        VMSTATE_UINT32(intr, GD32RcuState),
        VMSTATE_UINT32(apb2rst, GD32RcuState),
        VMSTATE_UINT32(apb1rst, GD32RcuState),
        VMSTATE_UINT32(ahben, GD32RcuState),
        VMSTATE_UINT32(apb2en, GD32RcuState),
        VMSTATE_UINT32(apb1en, GD32RcuState),
        VMSTATE_UINT32(bdctl, GD32RcuState),
        VMSTATE_UINT32(rstsck, GD32RcuState),
        VMSTATE_UINT32(ahbrst, GD32RcuState),
        VMSTATE_UINT32(cfg1, GD32RcuState),
        VMSTATE_UINT32(dsv, GD32RcuState),
        VMSTATE_UINT32(addctl, GD32RcuState),
        VMSTATE_UINT32(addint, GD32RcuState),
        VMSTATE_UINT32(addapb1rst, GD32RcuState),
        VMSTATE_UINT32(addapb1en, GD32RcuState),
        VMSTATE_END_OF_LIST()
    }
};

static void gd32_rcu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_rcu_reset);
    dc->vmsd = &vmstate_gd32_rcu;
}

static const TypeInfo gd32_rcu_info = {
    .name          = TYPE_GD32_RCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32RcuState),
    .instance_init = gd32_rcu_init,
    .class_init    = gd32_rcu_class_init,
};

static void gd32_rcu_register_types(void)
{
    type_register_static(&gd32_rcu_info);
}

type_init(gd32_rcu_register_types)

