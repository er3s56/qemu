/*
 * GD32 Timer Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This implements the GD32C103 TIMER peripherals with support for:
 * - Periodic timer interrupts (update events)
 * - Prescaler and auto-reload configuration
 * - Up/down counting modes
 * - Basic channel compare functionality
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/timer/gd32_timer.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef GD32_TIMER_DEBUG
#define GD32_TIMER_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (GD32_TIMER_DEBUG) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0)

static void gd32_timer_set_alarm(GD32TimerState *s, int64_t now);

static void gd32_timer_update_irq(GD32TimerState *s)
{
    int level = 0;

    /* Check if any enabled interrupt has its flag set */
    if ((s->dmainten & GD32_TIMER_DMAINTEN_UPIE) &&
        (s->intf & GD32_TIMER_INTF_UPIF)) {
        level = 1;
    }
    if ((s->dmainten & GD32_TIMER_DMAINTEN_CH0IE) &&
        (s->intf & GD32_TIMER_INTF_CH0IF)) {
        level = 1;
    }
    if ((s->dmainten & GD32_TIMER_DMAINTEN_CH1IE) &&
        (s->intf & GD32_TIMER_INTF_CH1IF)) {
        level = 1;
    }
    if ((s->dmainten & GD32_TIMER_DMAINTEN_CH2IE) &&
        (s->intf & GD32_TIMER_INTF_CH2IF)) {
        level = 1;
    }
    if ((s->dmainten & GD32_TIMER_DMAINTEN_CH3IE) &&
        (s->intf & GD32_TIMER_INTF_CH3IF)) {
        level = 1;
    }

    DB_PRINT("%s: irq level=%d (intf=0x%x, dmainten=0x%x)\n",
             s->name ? s->name : "TIMER", level, s->intf, s->dmainten);

    qemu_set_irq(s->irq, level);
}

static void gd32_timer_interrupt(void *opaque)
{
    GD32TimerState *s = opaque;

    DB_PRINT("%s: timer interrupt\n", s->name ? s->name : "TIMER");

    /* Set update interrupt flag if counter is enabled */
    if ((s->ctl0 & GD32_TIMER_CTL0_CEN)) {
        s->intf |= GD32_TIMER_INTF_UPIF;
        gd32_timer_update_irq(s);

        /* Reschedule for next period (unless single pulse mode) */
        if (!(s->ctl0 & GD32_TIMER_CTL0_SPM)) {
            gd32_timer_set_alarm(s, s->hit_time);
        } else {
            /* Single pulse mode: disable counter after one pulse */
            s->ctl0 &= ~GD32_TIMER_CTL0_CEN;
        }
    }
}

static inline int64_t gd32_timer_ns_to_ticks(GD32TimerState *s, int64_t t)
{
    /* Convert nanoseconds to timer ticks considering prescaler */
    return muldiv64(t, s->freq_hz, 1000000000ULL) / (s->psc + 1);
}

static inline int64_t gd32_timer_ticks_to_ns(GD32TimerState *s, int64_t ticks)
{
    /* Convert timer ticks to nanoseconds considering prescaler */
    return muldiv64(ticks * (s->psc + 1), 1000000000ULL, s->freq_hz);
}

static void gd32_timer_set_alarm(GD32TimerState *s, int64_t now)
{
    uint64_t ticks;
    int64_t now_ticks;

    /* Don't set alarm if counter is disabled or CAR is 0 */
    if (!(s->ctl0 & GD32_TIMER_CTL0_CEN) || s->car == 0) {
        timer_del(s->timer);
        return;
    }

    now_ticks = gd32_timer_ns_to_ticks(s, now);
    
    /* Calculate ticks until next overflow/underflow */
    if (s->ctl0 & GD32_TIMER_CTL0_DIR) {
        /* Down counting: ticks until reaching 0 */
        int64_t current = (now_ticks - s->tick_offset) % (s->car + 1);
        ticks = current > 0 ? current : (s->car + 1);
    } else {
        /* Up counting: ticks until reaching CAR */
        int64_t current = (now_ticks - s->tick_offset) % (s->car + 1);
        ticks = s->car + 1 - current;
    }

    /* Calculate next hit time in nanoseconds */
    s->hit_time = now + gd32_timer_ticks_to_ns(s, ticks);

    DB_PRINT("%s: alarm set in %lu ticks, hit_time=%ld\n",
             s->name ? s->name : "TIMER", (unsigned long)ticks, (long)s->hit_time);

    timer_mod(s->timer, s->hit_time);
}

static uint32_t gd32_timer_get_count(GD32TimerState *s)
{
    int64_t now, now_ticks, count;

    if (!(s->ctl0 & GD32_TIMER_CTL0_CEN)) {
        /* Timer stopped, return last count */
        return s->cnt;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    now_ticks = gd32_timer_ns_to_ticks(s, now);

    if (s->car == 0) {
        return 0;
    }

    if (s->ctl0 & GD32_TIMER_CTL0_DIR) {
        /* Down counting */
        count = s->car - ((now_ticks - s->tick_offset) % (s->car + 1));
    } else {
        /* Up counting */
        count = (now_ticks - s->tick_offset) % (s->car + 1);
    }

    return (uint32_t)(count & 0xFFFF);
}

static uint64_t gd32_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    GD32TimerState *s = opaque;
    uint32_t value = 0;

    switch (offset) {
    case GD32_TIMER_CTL0:
        value = s->ctl0;
        break;
    case GD32_TIMER_CTL1:
        value = s->ctl1;
        break;
    case GD32_TIMER_SMCFG:
        value = s->smcfg;
        break;
    case GD32_TIMER_DMAINTEN:
        value = s->dmainten;
        break;
    case GD32_TIMER_INTF:
        value = s->intf;
        break;
    case GD32_TIMER_SWEVG:
        /* Write-only register, reads as 0 */
        value = 0;
        break;
    case GD32_TIMER_CHCTL0:
        value = s->chctl0;
        break;
    case GD32_TIMER_CHCTL1:
        value = s->chctl1;
        break;
    case GD32_TIMER_CHCTL2:
        value = s->chctl2;
        break;
    case GD32_TIMER_CNT:
        value = gd32_timer_get_count(s);
        break;
    case GD32_TIMER_PSC:
        value = s->psc;
        break;
    case GD32_TIMER_CAR:
        value = s->car;
        break;
    case GD32_TIMER_CREP:
        value = s->crep;
        break;
    case GD32_TIMER_CH0CV:
        value = s->ch0cv;
        break;
    case GD32_TIMER_CH1CV:
        value = s->ch1cv;
        break;
    case GD32_TIMER_CH2CV:
        value = s->ch2cv;
        break;
    case GD32_TIMER_CH3CV:
        value = s->ch3cv;
        break;
    case GD32_TIMER_CCHP:
        value = s->cchp;
        break;
    case GD32_TIMER_DMACFG:
        value = s->dmacfg;
        break;
    case GD32_TIMER_DMATB:
        value = s->dmatb;
        break;
    case GD32_TIMER_CFG:
        value = s->cfg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%"HWADDR_PRIx"\n",
                      s->name ? s->name : "gd32-timer", offset);
        break;
    }

    DB_PRINT("%s: read 0x%"HWADDR_PRIx" = 0x%x\n",
             s->name ? s->name : "TIMER", offset, value);

    return value;
}

static void gd32_timer_write(void *opaque, hwaddr offset,
                             uint64_t val64, unsigned size)
{
    GD32TimerState *s = opaque;
    uint32_t value = val64;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    bool recalc_alarm = false;

    DB_PRINT("%s: write 0x%"HWADDR_PRIx" = 0x%x\n",
             s->name ? s->name : "TIMER", offset, value);

    switch (offset) {
    case GD32_TIMER_CTL0:
        {
            uint32_t old_ctl0 = s->ctl0;
            s->ctl0 = value & 0x3FF;  /* Bits 0-9 valid */

            /* Check if counter enable changed */
            if ((value & GD32_TIMER_CTL0_CEN) &&
                !(old_ctl0 & GD32_TIMER_CTL0_CEN)) {
                /* Counter just enabled, reset tick offset */
                s->tick_offset = gd32_timer_ns_to_ticks(s, now);
                recalc_alarm = true;
            } else if (!(value & GD32_TIMER_CTL0_CEN) &&
                       (old_ctl0 & GD32_TIMER_CTL0_CEN)) {
                /* Counter just disabled, save current count */
                s->cnt = gd32_timer_get_count(s);
                timer_del(s->timer);
            }
        }
        break;

    case GD32_TIMER_CTL1:
        s->ctl1 = value;
        break;

    case GD32_TIMER_SMCFG:
        s->smcfg = value;
        break;

    case GD32_TIMER_DMAINTEN:
        s->dmainten = value;
        gd32_timer_update_irq(s);
        break;

    case GD32_TIMER_INTF:
        /* Write 0 to clear flags (write 1 has no effect) */
        s->intf &= value;
        gd32_timer_update_irq(s);
        break;

    case GD32_TIMER_SWEVG:
        /* Software event generation */
        if (value & GD32_TIMER_SWEVG_UPG) {
            /* Generate update event - reset counter */
            s->tick_offset = gd32_timer_ns_to_ticks(s, now);
            s->cnt = 0;
            if (!(s->ctl0 & GD32_TIMER_CTL0_UPDIS)) {
                s->intf |= GD32_TIMER_INTF_UPIF;
                gd32_timer_update_irq(s);
            }
            recalc_alarm = true;
        }
        break;

    case GD32_TIMER_CHCTL0:
        s->chctl0 = value;
        break;

    case GD32_TIMER_CHCTL1:
        s->chctl1 = value;
        break;

    case GD32_TIMER_CHCTL2:
        s->chctl2 = value;
        break;

    case GD32_TIMER_CNT:
        /* Writing CNT resets the tick offset */
        s->cnt = value & 0xFFFF;
        if (s->ctl0 & GD32_TIMER_CTL0_CEN) {
            s->tick_offset = gd32_timer_ns_to_ticks(s, now) - s->cnt;
            recalc_alarm = true;
        }
        break;

    case GD32_TIMER_PSC:
        s->psc = value & 0xFFFF;
        /* Prescaler change takes effect at next update event */
        recalc_alarm = true;
        break;

    case GD32_TIMER_CAR:
        s->car = value & 0xFFFF;
        recalc_alarm = true;
        break;

    case GD32_TIMER_CREP:
        s->crep = value & 0xFF;
        break;

    case GD32_TIMER_CH0CV:
        s->ch0cv = value & 0xFFFF;
        break;

    case GD32_TIMER_CH1CV:
        s->ch1cv = value & 0xFFFF;
        break;

    case GD32_TIMER_CH2CV:
        s->ch2cv = value & 0xFFFF;
        break;

    case GD32_TIMER_CH3CV:
        s->ch3cv = value & 0xFFFF;
        break;

    case GD32_TIMER_CCHP:
        s->cchp = value;
        break;

    case GD32_TIMER_DMACFG:
        s->dmacfg = value;
        break;

    case GD32_TIMER_DMATB:
        s->dmatb = value;
        break;

    case GD32_TIMER_CFG:
        s->cfg = value & 0x3;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%"HWADDR_PRIx"\n",
                      s->name ? s->name : "gd32-timer", offset);
        break;
    }

    if (recalc_alarm && (s->ctl0 & GD32_TIMER_CTL0_CEN)) {
        gd32_timer_set_alarm(s, now);
    }
}

static const MemoryRegionOps gd32_timer_ops = {
    .read = gd32_timer_read,
    .write = gd32_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void gd32_timer_reset(DeviceState *dev)
{
    GD32TimerState *s = GD32_TIMER(dev);

    timer_del(s->timer);

    s->ctl0 = 0;
    s->ctl1 = 0;
    s->smcfg = 0;
    s->dmainten = 0;
    s->intf = 0;
    s->chctl0 = 0;
    s->chctl1 = 0;
    s->chctl2 = 0;
    s->cnt = 0;
    s->psc = 0;
    s->car = 0xFFFF;  /* Default to max value */
    s->crep = 0;
    s->ch0cv = 0;
    s->ch1cv = 0;
    s->ch2cv = 0;
    s->ch3cv = 0;
    s->cchp = 0;
    s->dmacfg = 0;
    s->dmatb = 0;
    s->cfg = 0;

    s->tick_offset = 0;
    s->hit_time = 0;

    qemu_set_irq(s->irq, 0);
}

static void gd32_timer_init(Object *obj)
{
    GD32TimerState *s = GD32_TIMER(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->iomem, obj, &gd32_timer_ops, s,
                          "gd32-timer", 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void gd32_timer_realize(DeviceState *dev, Error **errp)
{
    GD32TimerState *s = GD32_TIMER(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, gd32_timer_interrupt, s);
}

static const VMStateDescription vmstate_gd32_timer = {
    .name = TYPE_GD32_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(tick_offset, GD32TimerState),
        VMSTATE_UINT64(hit_time, GD32TimerState),
        VMSTATE_UINT32(ctl0, GD32TimerState),
        VMSTATE_UINT32(ctl1, GD32TimerState),
        VMSTATE_UINT32(smcfg, GD32TimerState),
        VMSTATE_UINT32(dmainten, GD32TimerState),
        VMSTATE_UINT32(intf, GD32TimerState),
        VMSTATE_UINT32(chctl0, GD32TimerState),
        VMSTATE_UINT32(chctl1, GD32TimerState),
        VMSTATE_UINT32(chctl2, GD32TimerState),
        VMSTATE_UINT32(cnt, GD32TimerState),
        VMSTATE_UINT32(psc, GD32TimerState),
        VMSTATE_UINT32(car, GD32TimerState),
        VMSTATE_UINT32(crep, GD32TimerState),
        VMSTATE_UINT32(ch0cv, GD32TimerState),
        VMSTATE_UINT32(ch1cv, GD32TimerState),
        VMSTATE_UINT32(ch2cv, GD32TimerState),
        VMSTATE_UINT32(ch3cv, GD32TimerState),
        VMSTATE_UINT32(cchp, GD32TimerState),
        VMSTATE_UINT32(dmacfg, GD32TimerState),
        VMSTATE_UINT32(dmatb, GD32TimerState),
        VMSTATE_UINT32(cfg, GD32TimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property gd32_timer_properties[] = {
    DEFINE_PROP_UINT64("clock-frequency", GD32TimerState, freq_hz, 120000000),
    DEFINE_PROP_STRING("name", GD32TimerState, name),
    DEFINE_PROP_END_OF_LIST(),
};

static void gd32_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_timer_reset);
    device_class_set_props(dc, gd32_timer_properties);
    dc->vmsd = &vmstate_gd32_timer;
    dc->realize = gd32_timer_realize;
}

static const TypeInfo gd32_timer_info = {
    .name          = TYPE_GD32_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32TimerState),
    .instance_init = gd32_timer_init,
    .class_init    = gd32_timer_class_init,
};

static void gd32_timer_register_types(void)
{
    type_register_static(&gd32_timer_info);
}

type_init(gd32_timer_register_types)

