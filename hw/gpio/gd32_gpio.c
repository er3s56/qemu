/*
 * GD32 GPIO (General Purpose I/O)
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This emulates the GD32C10x GPIO for basic I/O operations.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/gpio/gd32_gpio.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "migration/vmstate.h"

/*
 * Update ISTAT register based on pin mode and output/input values.
 * For output pins, ISTAT reflects OCTL.
 * For input pins, ISTAT reflects external input.
 */
static void gd32_gpio_update_istat(GD32GpioState *s)
{
    uint32_t mode;
    uint16_t new_istat = 0;
    int i;

    for (i = 0; i < GD32_GPIO_NUM_PINS; i++) {
        /* Get mode bits for this pin (2 bits per pin) */
        if (i < 8) {
            mode = (s->ctl0 >> (i * 4)) & 0x3;
        } else {
            mode = (s->ctl1 >> ((i - 8) * 4)) & 0x3;
        }

        if (mode == 0) {
            /* Input mode: use external input */
            if (s->input & (1 << i)) {
                new_istat |= (1 << i);
            }
        } else {
            /* Output mode: reflect OCTL */
            if (s->octl & (1 << i)) {
                new_istat |= (1 << i);
            }
        }
    }

    s->istat = new_istat;
}

/*
 * Update output IRQs when OCTL changes.
 */
static void gd32_gpio_update_output(GD32GpioState *s)
{
    int i;

    for (i = 0; i < GD32_GPIO_NUM_PINS; i++) {
        qemu_set_irq(s->output[i], (s->octl >> i) & 1);
    }
}

/*
 * Handle external input from connected devices.
 */
static void gd32_gpio_set_input(void *opaque, int line, int level)
{
    GD32GpioState *s = GD32_GPIO(opaque);

    if (line >= GD32_GPIO_NUM_PINS) {
        return;
    }

    if (level) {
        s->input |= (1 << line);
    } else {
        s->input &= ~(1 << line);
    }

    gd32_gpio_update_istat(s);
}

static uint64_t gd32_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    GD32GpioState *s = GD32_GPIO(opaque);
    uint64_t value = 0;

    switch (addr) {
    case GD32_GPIO_CTL0:
        value = s->ctl0;
        break;
    case GD32_GPIO_CTL1:
        value = s->ctl1;
        break;
    case GD32_GPIO_ISTAT:
        gd32_gpio_update_istat(s);
        value = s->istat;
        break;
    case GD32_GPIO_OCTL:
        value = s->octl;
        break;
    case GD32_GPIO_BOP:
        /* BOP is write-only, reads return 0 */
        value = 0;
        break;
    case GD32_GPIO_BC:
        /* BC is write-only, reads return 0 */
        value = 0;
        break;
    case GD32_GPIO_LOCK:
        value = s->lock;
        break;
    case GD32_GPIO_SPD:
        value = s->spd;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, s->name, addr);
        break;
    }

    return value;
}

static void gd32_gpio_write(void *opaque, hwaddr addr,
                            uint64_t val64, unsigned int size)
{
    GD32GpioState *s = GD32_GPIO(opaque);
    uint32_t value = val64;

    switch (addr) {
    case GD32_GPIO_CTL0:
        s->ctl0 = value;
        gd32_gpio_update_istat(s);
        break;
    case GD32_GPIO_CTL1:
        s->ctl1 = value;
        gd32_gpio_update_istat(s);
        break;
    case GD32_GPIO_ISTAT:
        /* ISTAT is read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: ISTAT is read-only\n", __func__, s->name);
        break;
    case GD32_GPIO_OCTL:
        s->octl = value & 0xFFFF;
        gd32_gpio_update_istat(s);
        gd32_gpio_update_output(s);
        break;
    case GD32_GPIO_BOP:
        /*
         * BOP: Bit Operation register
         * Bits 0-15: Set corresponding OCTL bit
         * Bits 16-31: Clear corresponding OCTL bit
         * If both set and clear are specified, set has priority
         */
        {
            uint16_t bits_to_set = value & 0xFFFF;
            uint16_t bits_to_clear = (value >> 16) & 0xFFFF;

            s->octl &= ~bits_to_clear;
            s->octl |= bits_to_set;
            gd32_gpio_update_istat(s);
            gd32_gpio_update_output(s);
        }
        break;
    case GD32_GPIO_BC:
        /* BC: Bit Clear register - clear corresponding OCTL bits */
        s->octl &= ~(value & 0xFFFF);
        gd32_gpio_update_istat(s);
        gd32_gpio_update_output(s);
        break;
    case GD32_GPIO_LOCK:
        /*
         * Lock sequence: Write LKK=1, Write LKK=0, Write LKK=1, Read
         * After correct sequence, LKK reads as 1 and config is locked.
         * Simplified implementation: just store the value.
         */
        s->lock = value & 0x1FFFF;
        break;
    case GD32_GPIO_SPD:
        s->spd = value & 0xFFFF;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, s->name, addr);
        break;
    }
}

static const MemoryRegionOps gd32_gpio_ops = {
    .read = gd32_gpio_read,
    .write = gd32_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void gd32_gpio_reset(DeviceState *dev)
{
    GD32GpioState *s = GD32_GPIO(dev);

    /* Reset values - all pins default to input floating mode */
    s->ctl0 = 0x44444444;
    s->ctl1 = 0x44444444;
    s->istat = 0x00000000;
    s->octl = 0x00000000;
    s->lock = 0x00000000;
    s->spd = 0x00000000;
    s->lock_state = 0;
    /* Note: s->input is NOT cleared on reset because it represents
     * external hardware signals (e.g., DIP switches, jumpers) that
     * are not affected by MCU reset. */
}

static void gd32_gpio_init(Object *obj)
{
    GD32GpioState *s = GD32_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &gd32_gpio_ops, s,
                          TYPE_GD32_GPIO, GD32_GPIO_PERIPHERAL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    /* Initialize input lines */
    qdev_init_gpio_in(DEVICE(obj), gd32_gpio_set_input, GD32_GPIO_NUM_PINS);

    /* Initialize output lines */
    qdev_init_gpio_out(DEVICE(obj), s->output, GD32_GPIO_NUM_PINS);
}

static Property gd32_gpio_properties[] = {
    DEFINE_PROP_STRING("name", GD32GpioState, name),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_gd32_gpio = {
    .name = TYPE_GD32_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctl0, GD32GpioState),
        VMSTATE_UINT32(ctl1, GD32GpioState),
        VMSTATE_UINT32(istat, GD32GpioState),
        VMSTATE_UINT32(octl, GD32GpioState),
        VMSTATE_UINT32(lock, GD32GpioState),
        VMSTATE_UINT32(spd, GD32GpioState),
        VMSTATE_UINT8(lock_state, GD32GpioState),
        VMSTATE_UINT16(input, GD32GpioState),
        VMSTATE_END_OF_LIST()
    }
};

static void gd32_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, gd32_gpio_reset);
    dc->vmsd = &vmstate_gd32_gpio;
    device_class_set_props(dc, gd32_gpio_properties);
}

static const TypeInfo gd32_gpio_info = {
    .name          = TYPE_GD32_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32GpioState),
    .instance_init = gd32_gpio_init,
    .class_init    = gd32_gpio_class_init,
};

static void gd32_gpio_register_types(void)
{
    type_register_static(&gd32_gpio_info);
}

type_init(gd32_gpio_register_types)

