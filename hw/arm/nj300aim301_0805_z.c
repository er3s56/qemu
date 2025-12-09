/*
 * NJ300AIM301-0805-Z Machine Definition
 *
 * Copyright (c) 2025
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file defines the NJ300AIM301-0805-Z product machine,
 * which is based on the GD32C103 SoC with external AD7792 ADC.
 *
 * Hardware Configuration:
 * - SoC: GD32C103RBT6 (Cortex-M4, 128KB Flash, 32KB SRAM)
 * - External ADC: AD7792 connected via GPIO bit-banging SPI
 *   - CLK:  PB3 (GPIOB pin 3)
 *   - MOSI: PB5 (GPIOB pin 5)
 *   - MISO: PB4 (GPIOB pin 4)
 *   - CS:   PA0 (GPIOA pin 0)
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "hw/irq.h"
#include "hw/arm/gd32c103_soc.h"
#include "hw/ssi/ad7792.h"
#include "hw/misc/led.h"
#include "hw/arm/boot.h"

/* Main SYSCLK frequency: 120MHz (max for GD32C103) */
#define GD32_SYSCLK_FRQ 120000000ULL

/*
 * NJ300AIM301-0805-Z Machine State
 */
struct NJ300AIM301MachineState {
    MachineState parent;

    GD32C103State soc;
    AD7792State ad7792;
    CanBusState *canbus[GD32_NUM_CANS];

    /* Slot address (0-127), set via -machine slot-address=N */
    uint8_t slot_address;
};

#define TYPE_NJ300AIM301_0805_Z_MACHINE MACHINE_TYPE_NAME("nj300aim301-0805-z")
OBJECT_DECLARE_SIMPLE_TYPE(NJ300AIM301MachineState, NJ300AIM301_0805_Z_MACHINE)

static void nj300aim301_0805_z_init(MachineState *machine)
{
    NJ300AIM301MachineState *s = NJ300AIM301_0805_Z_MACHINE(machine);
    DeviceState *dev;
    Clock *sysclk;
    int i;

    /* Create system clock (fixed frequency, no migration needed) */
    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, GD32_SYSCLK_FRQ);

    /* Create and configure SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_GD32C103_SOC);
    dev = DEVICE(&s->soc);
    qdev_connect_clock_in(dev, "sysclk", sysclk);

    /* Connect CAN buses if provided */
    for (i = 0; i < GD32_NUM_CANS; i++) {
        if (s->canbus[i]) {
            g_autofree char *bus_name = g_strdup_printf("canbus%d", i);
            object_property_set_link(OBJECT(&s->soc), bus_name,
                                     OBJECT(s->canbus[i]), &error_abort);
        }
    }

    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    /*
     * Configure slot address GPIO inputs.
     *
     * The module address is calculated from 7 GPIO pins:
     * MODULE_ADDR = (SW3<<2 | SW2<<1 | SW1) * 15 + (MX4<<3 | MX3<<2 | MX2<<1 | MX1)
     *
     * GPIO pin mapping for NJ300AIM301-0805-Z:
     * - MX1: PB10 (GPIOB pin 10)
     * - MX2: PB11 (GPIOB pin 11)
     * - MX3: PC9  (GPIOC pin 9)
     * - MX4: PC8  (GPIOC pin 8)
     * - SW1: PC11 (GPIOC pin 11)
     * - SW2: PC10 (GPIOC pin 10)
     * - SW3: PD2  (GPIOD pin 2)
     */
    {
        uint8_t sw = s->slot_address / 15;  /* 0-8 (uses SW1-SW3) */
        uint8_t mx = s->slot_address % 15;  /* 0-14 (uses MX1-MX4) */

        /* Set MX1-MX4 (lower nibble of slot address) */
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->soc.gpio[1]), 10), (mx >> 0) & 1);
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->soc.gpio[1]), 11), (mx >> 1) & 1);
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->soc.gpio[2]), 9),  (mx >> 2) & 1);
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->soc.gpio[2]), 8),  (mx >> 3) & 1);

        /* Set SW1-SW3 (upper bits of slot address) */
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->soc.gpio[2]), 11), (sw >> 0) & 1);
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->soc.gpio[2]), 10), (sw >> 1) & 1);
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&s->soc.gpio[3]), 2),  (sw >> 2) & 1);
    }

    /*
     * Initialize AD7792 External ADC
     * This is an external chip connected via GPIO bit-banging SPI
     */
    object_initialize_child(OBJECT(machine), "ad7792", &s->ad7792, TYPE_AD7792);
    dev = DEVICE(&s->ad7792);
    qdev_prop_set_string(dev, "name", "AD7792");
    if (!qdev_realize(dev, NULL, &error_fatal)) {
        return;
    }

    /*
     * Connect GPIO outputs to AD7792 inputs:
     * NJ300 Hardware Configuration:
     * - PA0 (GPIOA pin 0) -> CS
     * - PB3 (GPIOB pin 3) -> CLK
     * - PB5 (GPIOB pin 5) -> MOSI
     */
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio[0]), 0,
                          qdev_get_gpio_in_named(dev, "cs", 0));
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio[1]), 3,
                          qdev_get_gpio_in_named(dev, "clk", 0));
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio[1]), 5,
                          qdev_get_gpio_in_named(dev, "mosi", 0));

    /*
     * Connect AD7792 MISO output to GPIO input:
     * - AD7792 MISO -> PB4 (GPIOB pin 4)
     */
    qdev_connect_gpio_out_named(dev, "miso", 0,
                                qdev_get_gpio_in(DEVICE(&s->soc.gpio[1]), 4));

    /*
     * Initialize LED devices
     * NJ300 has 4 status LEDs, active low (lit when GPIO output is 0)
     * - LOGO:  PA7 (GPIOA pin 7) - Blue
     * - RUN:   PA5 (GPIOA pin 5) - Green
     * - ACK:   PC3 (GPIOC pin 3) - Green
     * - FAULT: PC1 (GPIOC pin 1) - Red
     */
    LEDState *led_logo = led_create_simple(OBJECT(machine),
        GPIO_POLARITY_ACTIVE_LOW, LED_COLOR_BLUE, "LED-LOGO");
    LEDState *led_run = led_create_simple(OBJECT(machine),
        GPIO_POLARITY_ACTIVE_LOW, LED_COLOR_GREEN, "LED-RUN");
    LEDState *led_ack = led_create_simple(OBJECT(machine),
        GPIO_POLARITY_ACTIVE_LOW, LED_COLOR_GREEN, "LED-ACK");
    LEDState *led_fault = led_create_simple(OBJECT(machine),
        GPIO_POLARITY_ACTIVE_LOW, LED_COLOR_RED, "LED-FAULT");

    /* Connect GPIO outputs to LED inputs */
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio[0]), 7,
                          qdev_get_gpio_in(DEVICE(led_logo), 0));   /* PA7 */
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio[0]), 5,
                          qdev_get_gpio_in(DEVICE(led_run), 0));    /* PA5 */
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio[2]), 3,
                          qdev_get_gpio_in(DEVICE(led_ack), 0));    /* PC3 */
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio[2]), 1,
                          qdev_get_gpio_in(DEVICE(led_fault), 0));  /* PC1 */

    /* Load kernel/firmware if provided */
    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0, GD32_FLASH_SIZE);
}

static void nj300aim301_0805_z_machine_instance_init(Object *obj)
{
    NJ300AIM301MachineState *s = NJ300AIM301_0805_Z_MACHINE(obj);

    /* Default slot address (can be overridden via -machine slot-address=N) */
    s->slot_address = 0;

    /* Add CAN bus links for SocketCAN integration */
    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&s->canbus[0],
                             object_property_allow_set_link, 0);
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&s->canbus[1],
                             object_property_allow_set_link, 0);
}

static void nj300aim301_slot_address_get(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    NJ300AIM301MachineState *s = NJ300AIM301_0805_Z_MACHINE(obj);
    uint8_t value = s->slot_address;
    visit_type_uint8(v, name, &value, errp);
}

static void nj300aim301_slot_address_set(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    NJ300AIM301MachineState *s = NJ300AIM301_0805_Z_MACHINE(obj);
    uint8_t value;

    if (!visit_type_uint8(v, name, &value, errp)) {
        return;
    }
    if (value >= 128) {
        error_setg(errp, "slot-address must be 0-127");
        return;
    }
    s->slot_address = value;
}

static void nj300aim301_0805_z_machine_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "NJ300AIM301-0805-Z (GD32C103 + AD7792)";
    mc->init = nj300aim301_0805_z_init;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 0;  /* SRAM is part of SoC, not machine RAM */

    /* Add slot-address property (0-127) for module address configuration */
    object_class_property_add(oc, "slot-address", "uint8",
                              nj300aim301_slot_address_get,
                              nj300aim301_slot_address_set,
                              NULL, NULL);
    object_class_property_set_description(oc, "slot-address",
        "Module slot address (0-127), calculated as SW*15+MX");
}

static const TypeInfo nj300aim301_0805_z_machine_type = {
    .name = TYPE_NJ300AIM301_0805_Z_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(NJ300AIM301MachineState),
    .instance_init = nj300aim301_0805_z_machine_instance_init,
    .class_init = nj300aim301_0805_z_machine_class_init,
};

static void nj300aim301_0805_z_machine_register_types(void)
{
    type_register_static(&nj300aim301_0805_z_machine_type);
}

type_init(nj300aim301_0805_z_machine_register_types)

