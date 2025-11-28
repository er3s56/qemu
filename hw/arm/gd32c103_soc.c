/*
 * GD32C103 SoC Emulation
 *
 * Copyright (c) 2025 Your Company Name
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GD32C103RBT6 Configuration:
 * - CPU: Cortex-M4 with FPU
 * - Flash: 128KB at 0x08000000
 * - SRAM: 32KB at 0x20000000
 * - USART0: 0x40013800, IRQ 37 (APB2)
 * - USART1: 0x40004400, IRQ 38 (APB1)
 * - USART2: 0x40004800, IRQ 39 (APB1)
 * - UART3:  0x40004C00, IRQ 52 (APB1)
 * - UART4:  0x40005000, IRQ 53 (APB1)
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/arm/gd32c103_soc.h"
#include "hw/misc/unimp.h"
#include "hw/arm/boot.h"

/* USART/UART base addresses from gd32c10x.h */
#define GD32_USART0_ADDR    0x40013800  /* APB2 */
#define GD32_USART1_ADDR    0x40004400  /* APB1 */
#define GD32_USART2_ADDR    0x40004800  /* APB1 */
#define GD32_UART3_ADDR     0x40004C00  /* APB1 */
#define GD32_UART4_ADDR     0x40005000  /* APB1 */

/* USART/UART IRQ numbers from gd32c10x.h */
#define GD32_USART0_IRQ     37
#define GD32_USART1_IRQ     38
#define GD32_USART2_IRQ     39
#define GD32_UART3_IRQ      52
#define GD32_UART4_IRQ      53

/* Peripheral addresses for unimplemented devices */
#define GD32_RCU_ADDR       0x40021000  /* Reset and Clock Unit */
#define GD32_FMC_ADDR       0x40022000  /* Flash Memory Controller */
#define GD32_AFIO_ADDR      0x40010000  /* Alternate Function I/O */
#define GD32_EXTI_ADDR      0x40010400  /* External Interrupt */
#define GD32_GPIOA_ADDR     0x40010800
#define GD32_GPIOB_ADDR     0x40010C00
#define GD32_GPIOC_ADDR     0x40011000
#define GD32_GPIOD_ADDR     0x40011400
#define GD32_GPIOE_ADDR     0x40011800

static const uint32_t usart_addr[] = {
    GD32_USART0_ADDR,
    GD32_USART1_ADDR,
    GD32_USART2_ADDR,
    GD32_UART3_ADDR,
    GD32_UART4_ADDR
};

static const int usart_irq[] = {
    GD32_USART0_IRQ,
    GD32_USART1_IRQ,
    GD32_USART2_IRQ,
    GD32_UART3_IRQ,
    GD32_UART4_IRQ
};

static void gd32c103_soc_initfn(Object *obj)
{
    GD32C103State *s = GD32C103_SOC(obj);
    int i;

    /* Initialize ARM Cortex-M4 core */
    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    /* Initialize USART/UART peripherals */
    for (i = 0; i < GD32_NUM_USARTS; i++) {
        object_initialize_child(obj, "usart[*]", &s->usart[i], TYPE_GD32_USART);
    }

    /* Initialize clocks */
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    s->refclk = qdev_init_clock_in(DEVICE(s), "refclk", NULL, NULL, 0);
}

static void gd32c103_soc_realize(DeviceState *dev_soc, Error **errp)
{
    GD32C103State *s = GD32C103_SOC(dev_soc);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *dev, *armv7m;
    SysBusDevice *busdev;
    int i;

    /* Validate clock configuration */
    if (clock_has_source(s->refclk)) {
        error_setg(errp, "refclk clock must not be wired up by the board code");
        return;
    }

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    /* Configure reference clock: HCLK / 8 */
    clock_set_mul_div(s->refclk, 8, 1);
    clock_set_source(s->refclk, s->sysclk);

    /*
     * Flash Memory
     * Main flash at 0x08000000, aliased to 0x00000000 for boot
     */
    memory_region_init_rom(&s->flash, OBJECT(dev_soc), "gd32c103.flash",
                           GD32_FLASH_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, GD32_FLASH_BASE, &s->flash);

    memory_region_init_alias(&s->flash_alias, OBJECT(dev_soc),
                             "gd32c103.flash.alias", &s->flash, 0,
                             GD32_FLASH_SIZE);
    memory_region_add_subregion(system_memory, 0, &s->flash_alias);

    /*
     * SRAM
     */
    memory_region_init_ram(&s->sram, NULL, "gd32c103.sram", GD32_SRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, GD32_SRAM_BASE, &s->sram);

    /*
     * ARM Cortex-M4 Core
     */
    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 68);  /* GD32C103 has 68 IRQs */
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 4);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    qdev_connect_clock_in(armv7m, "refclk", s->refclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    /*
     * USART/UART Peripherals
     * USART0, USART1, USART2, UART3, UART4
     */
    for (i = 0; i < GD32_NUM_USARTS; i++) {
        dev = DEVICE(&(s->usart[i]));
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usart[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, usart_addr[i]);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, usart_irq[i]));
    }

    /*
     * Unimplemented Peripherals
     * These stubs prevent guest crashes when accessing unmapped regions
     */
    create_unimplemented_device("gd32.rcu",   GD32_RCU_ADDR,   0x400);
    create_unimplemented_device("gd32.fmc",   GD32_FMC_ADDR,   0x400);
    create_unimplemented_device("gd32.afio",  GD32_AFIO_ADDR,  0x400);
    create_unimplemented_device("gd32.exti",  GD32_EXTI_ADDR,  0x400);
    create_unimplemented_device("gd32.gpioa", GD32_GPIOA_ADDR, 0x400);
    create_unimplemented_device("gd32.gpiob", GD32_GPIOB_ADDR, 0x400);
    create_unimplemented_device("gd32.gpioc", GD32_GPIOC_ADDR, 0x400);
    create_unimplemented_device("gd32.gpiod", GD32_GPIOD_ADDR, 0x400);
    create_unimplemented_device("gd32.gpioe", GD32_GPIOE_ADDR, 0x400);
}

static void gd32c103_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gd32c103_soc_realize;
    /* No vmstate or reset required: device has no internal state */
}

static const TypeInfo gd32c103_soc_info = {
    .name          = TYPE_GD32C103_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GD32C103State),
    .instance_init = gd32c103_soc_initfn,
    .class_init    = gd32c103_soc_class_init,
};

static void gd32c103_soc_types(void)
{
    type_register_static(&gd32c103_soc_info);
}

type_init(gd32c103_soc_types)

/*
 * GD32C103RBT6 Machine Definition
 */

/* Main SYSCLK frequency: 120MHz (max for GD32C103) */
#define GD32_SYSCLK_FRQ 120000000ULL

static void gd32c103rbt6_init(MachineState *machine)
{
    DeviceState *dev;
    Clock *sysclk;

    /* Create system clock (fixed frequency, no migration needed) */
    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, GD32_SYSCLK_FRQ);

    /* Create and configure SoC */
    dev = qdev_new(TYPE_GD32C103_SOC);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(dev));
    qdev_connect_clock_in(dev, "sysclk", sysclk);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Load kernel/firmware if provided */
    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0, GD32_FLASH_SIZE);
}

static void gd32c103rbt6_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };

    mc->desc = "GigaDevice GD32C103RBT6 (Cortex-M4)";
    mc->init = gd32c103rbt6_init;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 0;  /* SRAM is part of SoC, not machine RAM */
}

DEFINE_MACHINE("gd32c103rbt6", gd32c103rbt6_machine_init)
