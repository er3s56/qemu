/*
 * GD32 FMC (Flash Memory Controller)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This emulates the GD32C10x FMC for Flash wait state configuration
 * and Flash programming/erasing operations.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/gd32_fmc.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"

#ifndef GD32_FMC_DEBUG
#define GD32_FMC_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (GD32_FMC_DEBUG) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

/*
 * Perform page erase operation.
 * Erases a 1KB page by writing 0xFF to all bytes.
 */
static void gd32_fmc_do_page_erase(GD32FmcState *s)
{
    uint32_t page_addr;
    uint32_t offset;
    uint8_t *flash_ptr;
    MemoryRegion *mr;
    void *host_ptr;

    if (!s->flash_mr) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Flash memory region not set\n",
                      __func__);
        s->stat |= GD32_FMC_STAT_PGERR;
        return;
    }

    /* Align address to page boundary */
    page_addr = s->addr & ~(GD32_FMC_PAGE_SIZE - 1);

    /* Check if address is within flash range */
    if (page_addr < s->flash_base ||
        page_addr >= s->flash_base + s->flash_size) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Address 0x%08x out of flash range\n",
                      __func__, page_addr);
        s->stat |= GD32_FMC_STAT_PGERR;
        return;
    }

    offset = page_addr - s->flash_base;

    /* Get host pointer to flash memory */
    mr = s->flash_mr;
    host_ptr = memory_region_get_ram_ptr(mr);
    if (!host_ptr) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Cannot get flash RAM pointer\n",
                      __func__);
        s->stat |= GD32_FMC_STAT_PGERR;
        return;
    }

    flash_ptr = (uint8_t *)host_ptr + offset;

    /* Erase page (set all bytes to 0xFF) */
    memset(flash_ptr, 0xFF, GD32_FMC_PAGE_SIZE);

    /* Invalidate memory region to ensure QEMU sees the change */
    memory_region_set_dirty(mr, offset, GD32_FMC_PAGE_SIZE);

    DB_PRINT("Erased page at 0x%08x\n", page_addr);

    /* Operation complete */
    s->stat |= GD32_FMC_STAT_ENDF;
}

/*
 * Perform mass erase operation.
 * Erases entire flash by writing 0xFF to all bytes.
 */
static void gd32_fmc_do_mass_erase(GD32FmcState *s)
{
    void *host_ptr;

    if (!s->flash_mr) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Flash memory region not set\n",
                      __func__);
        s->stat |= GD32_FMC_STAT_PGERR;
        return;
    }

    host_ptr = memory_region_get_ram_ptr(s->flash_mr);
    if (!host_ptr) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Cannot get flash RAM pointer\n",
                      __func__);
        s->stat |= GD32_FMC_STAT_PGERR;
        return;
    }

    /* Erase entire flash */
    memset(host_ptr, 0xFF, s->flash_size);
    memory_region_set_dirty(s->flash_mr, 0, s->flash_size);

    DB_PRINT("Mass erase complete\n");

    s->stat |= GD32_FMC_STAT_ENDF;
}

static uint64_t gd32_fmc_read(void *opaque, hwaddr addr, unsigned int size)
{
    GD32FmcState *s = GD32_FMC(opaque);
    uint64_t value = 0;

    switch (addr) {
    case GD32_FMC_REG_WS:
        value = s->ws;
        break;
    case GD32_FMC_REG_KEY:
        /* KEY register is write-only, reads return 0 */
        value = 0;
        break;
    case GD32_FMC_REG_OBKEY:
        /* OBKEY register is write-only */
        value = 0;
        break;
    case GD32_FMC_REG_STAT:
        value = s->stat;
        break;
    case GD32_FMC_REG_CTL:
        value = s->ctl;
        break;
    case GD32_FMC_REG_ADDR:
        value = s->addr;
        break;
    case GD32_FMC_REG_OBSTAT:
        value = s->obstat;
        break;
    case GD32_FMC_REG_WP:
        value = s->wp;
        break;
    case GD32_FMC_REG_PID:
        /* Return a product ID for GD32C103 */
        value = 0x10036410;  /* Example PID */
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

static void gd32_fmc_write(void *opaque, hwaddr addr,
                           uint64_t val64, unsigned int size)
{
    GD32FmcState *s = GD32_FMC(opaque);
    uint32_t value = val64;

    DB_PRINT("Write 0x%08x to 0x%02lx\n", value, (unsigned long)addr);

    switch (addr) {
    case GD32_FMC_REG_WS:
        /* WS register: wait state, prefetch, cache settings */
        s->ws = value & (GD32_FMC_WS_WSCNT_MASK | GD32_FMC_WS_PFEN |
                         GD32_FMC_WS_ICEN | GD32_FMC_WS_DCEN |
                         GD32_FMC_WS_ICRST | GD32_FMC_WS_DCRST |
                         GD32_FMC_WS_PGW);
        /* Clear reset bits after processing */
        s->ws &= ~(GD32_FMC_WS_ICRST | GD32_FMC_WS_DCRST);
        break;

    case GD32_FMC_REG_KEY:
        /* Unlock sequence: KEY0 then KEY1 */
        if (s->unlock_state == 0 && value == GD32_FMC_UNLOCK_KEY0) {
            s->unlock_state = 1;
            DB_PRINT("Unlock key0 received\n");
        } else if (s->unlock_state == 1 && value == GD32_FMC_UNLOCK_KEY1) {
            s->unlock_state = 2;
            s->ctl &= ~GD32_FMC_CTL_LK;  /* Clear lock bit */
            DB_PRINT("FMC unlocked\n");
        } else {
            /* Wrong key sequence, reset state */
            s->unlock_state = 0;
            DB_PRINT("Unlock failed, wrong key\n");
        }
        break;

    case GD32_FMC_REG_OBKEY:
        /* Option bytes unlock - not fully implemented */
        qemu_log_mask(LOG_UNIMP, "%s: OBKEY write not implemented\n", __func__);
        break;

    case GD32_FMC_REG_STAT:
        /* Status register: write 1 to clear error/end flags */
        s->stat &= ~(value & (GD32_FMC_STAT_PGERR | GD32_FMC_STAT_PGAERR |
                              GD32_FMC_STAT_WPERR | GD32_FMC_STAT_ENDF));
        break;

    case GD32_FMC_REG_CTL:
        /* Check if locked */
        if (s->ctl & GD32_FMC_CTL_LK) {
            /* Only LK bit can be written when locked (to trigger re-lock) */
            if (value & GD32_FMC_CTL_LK) {
                s->unlock_state = 0;
            }
            return;
        }

        /* Update control register */
        s->ctl = value & (GD32_FMC_CTL_PG | GD32_FMC_CTL_PER |
                          GD32_FMC_CTL_MER | GD32_FMC_CTL_OBPG |
                          GD32_FMC_CTL_OBER | GD32_FMC_CTL_START |
                          GD32_FMC_CTL_LK | GD32_FMC_CTL_OBWEN |
                          GD32_FMC_CTL_ERRIE | GD32_FMC_CTL_ENDIE);

        /* Handle START bit - triggers erase operation */
        if (value & GD32_FMC_CTL_START) {
            if (s->ctl & GD32_FMC_CTL_PER) {
                /* Page erase */
                gd32_fmc_do_page_erase(s);
            } else if (s->ctl & GD32_FMC_CTL_MER) {
                /* Mass erase */
                gd32_fmc_do_mass_erase(s);
            }
            /* Clear START bit after operation */
            s->ctl &= ~GD32_FMC_CTL_START;
        }

        /* Handle lock bit */
        if (value & GD32_FMC_CTL_LK) {
            s->ctl |= GD32_FMC_CTL_LK;
            s->unlock_state = 0;
        }
        break;

    case GD32_FMC_REG_ADDR:
        s->addr = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps gd32_fmc_ops = {
    .read = gd32_fmc_read,
    .write = gd32_fmc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void gd32_fmc_reset(DeviceState *dev)
{
    GD32FmcState *s = GD32_FMC(dev);

    /* Reset values from GD32C10x reference manual */
    s->ws = 0x00000000;
    s->stat = 0x00000000;
    s->ctl = GD32_FMC_CTL_LK;  /* Locked by default */
    s->addr = 0x00000000;
    s->obstat = 0x00000000;
    s->wp = 0xFFFFFFFF;  /* No write protection */
    s->unlock_state = 0;
}

static void gd32_fmc_init(Object *obj)
{
    GD32FmcState *s = GD32_FMC(obj);

    memory_region_init_io(&s->mmio, obj, &gd32_fmc_ops, s,
                          TYPE_GD32_FMC, GD32_FMC_PERIPHERAL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static Property gd32_fmc_properties[] = {
    DEFINE_PROP_LINK("flash", GD32FmcState, flash_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_UINT32("flash-base", GD32FmcState, flash_base, 0x08000000),
    DEFINE_PROP_UINT32("flash-size", GD32FmcState, flash_size, 128 * 1024),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_gd32_fmc = {
    .name = TYPE_GD32_FMC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ws, GD32FmcState),
        VMSTATE_UINT32(stat, GD32FmcState),
        VMSTATE_UINT32(ctl, GD32FmcState),
        VMSTATE_UINT32(addr, GD32FmcState),
        VMSTATE_UINT32(obstat, GD32FmcState),
        VMSTATE_UINT32(wp, GD32FmcState),
        VMSTATE_UINT8(unlock_state, GD32FmcState),
        VMSTATE_END_OF_LIST()
    }
};

static void gd32_fmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_fmc_reset);
    dc->vmsd = &vmstate_gd32_fmc;
    device_class_set_props(dc, gd32_fmc_properties);
}

static const TypeInfo gd32_fmc_info = {
    .name          = TYPE_GD32_FMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32FmcState),
    .instance_init = gd32_fmc_init,
    .class_init    = gd32_fmc_class_init,
};

static void gd32_fmc_register_types(void)
{
    type_register_static(&gd32_fmc_info);
}

type_init(gd32_fmc_register_types)

