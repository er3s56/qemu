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

/* Main SYSCLK frequency: 120MHz (max for GD32C103) */
#define GD32_SYSCLK_FRQ 120000000ULL

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

/* TIMER base addresses (from gd32c10x_timer.h) */
#define GD32_TIMER0_ADDR    0x40012C00  /* APB2 - Advanced */
#define GD32_TIMER1_ADDR    0x40000000  /* APB1 - General L0 */
#define GD32_TIMER2_ADDR    0x40000400  /* APB1 - General L0 */
#define GD32_TIMER3_ADDR    0x40000800  /* APB1 - General L0 */
#define GD32_TIMER4_ADDR    0x40000C00  /* APB1 - General L0 */
#define GD32_TIMER5_ADDR    0x40001000  /* APB1 - Basic */
#define GD32_TIMER6_ADDR    0x40001400  /* APB1 - Basic */
#define GD32_TIMER7_ADDR    0x40013400  /* APB2 - Advanced */
#define GD32_TIMER8_ADDR    0x40014C00  /* APB2 - General L1 */
#define GD32_TIMER9_ADDR    0x40015000  /* APB2 - General L2 */
#define GD32_TIMER10_ADDR   0x40015400  /* APB2 - General L2 */
#define GD32_TIMER11_ADDR   0x40001800  /* APB1 - General L1 */
#define GD32_TIMER12_ADDR   0x40001C00  /* APB1 - General L2 */
#define GD32_TIMER13_ADDR   0x40002000  /* APB1 - General L2 */

/* TIMER IRQ numbers (from gd32c10x.h) */
/* Note: Some timers share IRQ lines with TIMER0/TIMER7 */
#define GD32_TIMER0_BRK_IRQ     24  /* TIMER0 break, shared with TIMER8 */
#define GD32_TIMER0_UP_IRQ      25  /* TIMER0 update, shared with TIMER9 */
#define GD32_TIMER0_TRG_CMT_IRQ 26  /* TIMER0 trigger/commutation, shared with TIMER10 */
#define GD32_TIMER0_CH_IRQ      27  /* TIMER0 channel capture compare */
#define GD32_TIMER1_IRQ         28
#define GD32_TIMER2_IRQ         29
#define GD32_TIMER3_IRQ         30
#define GD32_TIMER4_IRQ         50
#define GD32_TIMER5_IRQ         54
#define GD32_TIMER6_IRQ         55
#define GD32_TIMER7_BRK_IRQ     43  /* TIMER7 break, shared with TIMER11 */
#define GD32_TIMER7_UP_IRQ      44  /* TIMER7 update, shared with TIMER12 */
#define GD32_TIMER7_TRG_CMT_IRQ 45  /* TIMER7 trigger/commutation, shared with TIMER13 */
#define GD32_TIMER7_CH_IRQ      46  /* TIMER7 channel capture compare */

/* CAN base addresses and IRQ numbers (from gd32c10x.h) */
#define GD32_CAN0_ADDR          0x40006400  /* APB1 */
#define GD32_CAN1_ADDR          0x40006800  /* APB1 */
/* CAN0 IRQs */
#define GD32_CAN0_TX_IRQ        19  /* CAN0 TX interrupt */
#define GD32_CAN0_RX0_IRQ       20  /* CAN0 RX0 interrupt */
#define GD32_CAN0_RX1_IRQ       21  /* CAN0 RX1 interrupt */
#define GD32_CAN0_EWMC_IRQ      22  /* CAN0 EWMC (error/wakeup/mode change) interrupt */
/* CAN1 IRQs */
#define GD32_CAN1_TX_IRQ        63  /* CAN1 TX interrupt */
#define GD32_CAN1_RX0_IRQ       64  /* CAN1 RX0 interrupt */
#define GD32_CAN1_RX1_IRQ       65  /* CAN1 RX1 interrupt */
#define GD32_CAN1_EWMC_IRQ      66  /* CAN1 EWMC interrupt */

/* I2C base addresses and IRQ numbers (from gd32c10x.h) */
#define GD32_I2C0_ADDR          0x40005400  /* APB1 */
#define GD32_I2C1_ADDR          0x40005800  /* APB1 */
#define GD32_I2C0_EV_IRQ        31  /* I2C0 event interrupt */
#define GD32_I2C0_ER_IRQ        32  /* I2C0 error interrupt */
#define GD32_I2C1_EV_IRQ        33  /* I2C1 event interrupt */
#define GD32_I2C1_ER_IRQ        34  /* I2C1 error interrupt */

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

static const uint32_t gpio_addr[] = {
    GD32_GPIOA_ADDR,
    GD32_GPIOB_ADDR,
    GD32_GPIOC_ADDR,
    GD32_GPIOD_ADDR,
    GD32_GPIOE_ADDR
};

static const char gpio_name[][8] = {
    "GPIOA", "GPIOB", "GPIOC", "GPIOD", "GPIOE"
};

static const uint32_t timer_addr[] = {
    GD32_TIMER0_ADDR,
    GD32_TIMER1_ADDR,
    GD32_TIMER2_ADDR,
    GD32_TIMER3_ADDR,
    GD32_TIMER4_ADDR,
    GD32_TIMER5_ADDR,
    GD32_TIMER6_ADDR,
    GD32_TIMER7_ADDR,
    GD32_TIMER8_ADDR,
    GD32_TIMER9_ADDR,
    GD32_TIMER10_ADDR,
    GD32_TIMER11_ADDR,
    GD32_TIMER12_ADDR,
    GD32_TIMER13_ADDR
};

/*
 * TIMER IRQ mapping:
 * - TIMER0: Uses 4 separate IRQs (break, update, trigger/commutation, channel)
 *           For simplicity, we use the update IRQ (25) as the main interrupt
 * - TIMER7: Uses 4 separate IRQs (break, update, trigger/commutation, channel)
 *           For simplicity, we use the update IRQ (44) as the main interrupt
 * - TIMER8-10: Share IRQs with TIMER0 (24, 25, 26)
 * - TIMER11-13: Share IRQs with TIMER7 (43, 44, 45)
 * - TIMER1-6: Have dedicated IRQs
 */
static const int timer_irq[] = {
    GD32_TIMER0_UP_IRQ,     /* TIMER0 - use update IRQ */
    GD32_TIMER1_IRQ,        /* TIMER1 */
    GD32_TIMER2_IRQ,        /* TIMER2 */
    GD32_TIMER3_IRQ,        /* TIMER3 */
    GD32_TIMER4_IRQ,        /* TIMER4 */
    GD32_TIMER5_IRQ,        /* TIMER5 */
    GD32_TIMER6_IRQ,        /* TIMER6 */
    GD32_TIMER7_UP_IRQ,     /* TIMER7 - use update IRQ */
    GD32_TIMER0_BRK_IRQ,    /* TIMER8 - shared with TIMER0 break */
    GD32_TIMER0_UP_IRQ,     /* TIMER9 - shared with TIMER0 update */
    GD32_TIMER0_TRG_CMT_IRQ,/* TIMER10 - shared with TIMER0 trigger */
    GD32_TIMER7_BRK_IRQ,    /* TIMER11 - shared with TIMER7 break */
    GD32_TIMER7_UP_IRQ,     /* TIMER12 - shared with TIMER7 update */
    GD32_TIMER7_TRG_CMT_IRQ /* TIMER13 - shared with TIMER7 trigger */
};

static const char *timer_name[] = {
    "TIMER0", "TIMER1", "TIMER2", "TIMER3", "TIMER4",
    "TIMER5", "TIMER6", "TIMER7", "TIMER8", "TIMER9",
    "TIMER10", "TIMER11", "TIMER12", "TIMER13"
};

static const uint32_t can_addr[] = {
    GD32_CAN0_ADDR,
    GD32_CAN1_ADDR
};

/* CAN IRQs: TX, RX0, RX1, SCE (4 IRQs per controller) */
static const int can_irq[][4] = {
    { GD32_CAN0_TX_IRQ, GD32_CAN0_RX0_IRQ, GD32_CAN0_RX1_IRQ, GD32_CAN0_EWMC_IRQ },
    { GD32_CAN1_TX_IRQ, GD32_CAN1_RX0_IRQ, GD32_CAN1_RX1_IRQ, GD32_CAN1_EWMC_IRQ }
};

static const char *can_name[] = { "CAN0", "CAN1" };

static const uint32_t i2c_addr[] = {
    GD32_I2C0_ADDR,
    GD32_I2C1_ADDR
};

/* I2C IRQs: event, error (2 IRQs per controller) */
static const int i2c_irq[][2] = {
    { GD32_I2C0_EV_IRQ, GD32_I2C0_ER_IRQ },
    { GD32_I2C1_EV_IRQ, GD32_I2C1_ER_IRQ }
};

static const char *i2c_name[] = { "I2C0", "I2C1" };

static void gd32c103_soc_initfn(Object *obj)
{
    GD32C103State *s = GD32C103_SOC(obj);
    int i;

    /* Initialize ARM Cortex-M4 core */
    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    /* Initialize RCU (Reset and Clock Unit) */
    object_initialize_child(obj, "rcu", &s->rcu, TYPE_GD32_RCU);

    /* Initialize FMC (Flash Memory Controller) */
    object_initialize_child(obj, "fmc", &s->fmc, TYPE_GD32_FMC);

    /* Initialize AFIO (Alternate Function I/O) */
    object_initialize_child(obj, "afio", &s->afio, TYPE_GD32_AFIO);

    /* Initialize GPIO ports */
    for (i = 0; i < GD32_NUM_GPIOS; i++) {
        object_initialize_child(obj, "gpio[*]", &s->gpio[i], TYPE_GD32_GPIO);
    }

    /* Initialize TIMER peripherals */
    for (i = 0; i < GD32_NUM_TIMERS; i++) {
        object_initialize_child(obj, "timer[*]", &s->timer[i], TYPE_GD32_TIMER);
    }

    /* Initialize CAN peripherals */
    for (i = 0; i < GD32_NUM_CANS; i++) {
        object_initialize_child(obj, "can[*]", &s->can[i], TYPE_GD32_CAN);
    }

    /* Initialize I2C peripherals */
    for (i = 0; i < GD32_NUM_I2CS; i++) {
        object_initialize_child(obj, "i2c[*]", &s->i2c[i], TYPE_GD32_I2C);
    }

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
     * RCU (Reset and Clock Unit)
     */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rcu), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rcu), 0, GD32_RCU_ADDR);

    /*
     * FMC (Flash Memory Controller)
     * Link to flash memory region for programming/erasing
     */
    object_property_set_link(OBJECT(&s->fmc), "flash",
                             OBJECT(&s->flash), &error_abort);
    qdev_prop_set_uint32(DEVICE(&s->fmc), "flash-base", GD32_FLASH_BASE);
    qdev_prop_set_uint32(DEVICE(&s->fmc), "flash-size", GD32_FLASH_SIZE);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fmc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fmc), 0, GD32_FMC_ADDR);

    /*
     * AFIO (Alternate Function I/O)
     */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->afio), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->afio), 0, GD32_AFIO_ADDR);

    /*
     * GPIO Ports (GPIOA - GPIOE)
     */
    for (i = 0; i < GD32_NUM_GPIOS; i++) {
        dev = DEVICE(&s->gpio[i]);
        qdev_prop_set_string(dev, "name", gpio_name[i]);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[i]), 0, gpio_addr[i]);
    }

    /*
     * TIMER Peripherals (TIMER0 - TIMER13)
     */
    for (i = 0; i < GD32_NUM_TIMERS; i++) {
        dev = DEVICE(&s->timer[i]);
        qdev_prop_set_string(dev, "name", timer_name[i]);
        qdev_prop_set_uint64(dev, "clock-frequency", GD32_SYSCLK_FRQ);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, timer_addr[i]);
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, timer_irq[i]));
    }

    /*
     * CAN Peripherals (CAN0, CAN1)
     * Each CAN has 4 IRQ lines: TX, RX0, RX1, SCE
     *
     * In GD32, all 28 filters are shared between CAN0 and CAN1, and all filter
     * registers are accessed through CAN0's address space. CAN1 uses CAN0's
     * filter configuration for filter matching.
     */
    for (i = 0; i < GD32_NUM_CANS; i++) {
        dev = DEVICE(&s->can[i]);
        qdev_prop_set_string(dev, "name", can_name[i]);
        qdev_prop_set_uint8(dev, "can-index", i);
        /* CAN1 uses CAN0's filter configuration */
        if (i == 1) {
            object_property_set_link(OBJECT(&s->can[i]), "filter-owner",
                                     OBJECT(&s->can[0]), &error_abort);
        }
        /* Connect to CAN bus if provided */
        if (s->canbus[i]) {
            object_property_set_link(OBJECT(&s->can[i]), "canbus",
                                     OBJECT(s->canbus[i]), &error_abort);
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->can[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, can_addr[i]);
        /* Connect 4 IRQ lines: TX, RX0, RX1, SCE */
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, can_irq[i][0]));
        sysbus_connect_irq(busdev, 1, qdev_get_gpio_in(armv7m, can_irq[i][1]));
        sysbus_connect_irq(busdev, 2, qdev_get_gpio_in(armv7m, can_irq[i][2]));
        sysbus_connect_irq(busdev, 3, qdev_get_gpio_in(armv7m, can_irq[i][3]));
    }

    /*
     * I2C Peripherals (I2C0, I2C1)
     * Each I2C has 2 IRQ lines: event and error
     */
    for (i = 0; i < GD32_NUM_I2CS; i++) {
        dev = DEVICE(&s->i2c[i]);
        qdev_prop_set_string(dev, "name", i2c_name[i]);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, i2c_addr[i]);
        /* Connect 2 IRQ lines: event and error */
        sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, i2c_irq[i][0]));
        sysbus_connect_irq(busdev, 1, qdev_get_gpio_in(armv7m, i2c_irq[i][1]));
    }

    /*
     * Unimplemented Peripherals
     * These stubs prevent guest crashes when accessing unmapped regions
     */
    create_unimplemented_device("gd32.exti",  GD32_EXTI_ADDR,  0x400);
}

static Property gd32c103_soc_properties[] = {
    DEFINE_PROP_LINK("canbus0", GD32C103State, canbus[0], TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_LINK("canbus1", GD32C103State, canbus[1], TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void gd32c103_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gd32c103_soc_realize;
    device_class_set_props(dc, gd32c103_soc_properties);
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

struct GD32C103RBT6MachineState {
    MachineState parent;

    GD32C103State soc;
    CanBusState *canbus[GD32_NUM_CANS];
};

#define TYPE_GD32C103RBT6_MACHINE MACHINE_TYPE_NAME("gd32c103rbt6")
OBJECT_DECLARE_SIMPLE_TYPE(GD32C103RBT6MachineState, GD32C103RBT6_MACHINE)

static void gd32c103rbt6_init(MachineState *machine)
{
    GD32C103RBT6MachineState *s = GD32C103RBT6_MACHINE(machine);
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

    /* Load kernel/firmware if provided */
    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0, GD32_FLASH_SIZE);
}

static void gd32c103rbt6_machine_instance_init(Object *obj)
{
    GD32C103RBT6MachineState *s = GD32C103RBT6_MACHINE(obj);

    /* Add CAN bus links for SocketCAN integration */
    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&s->canbus[0],
                             object_property_allow_set_link, 0);
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&s->canbus[1],
                             object_property_allow_set_link, 0);
}

static void gd32c103rbt6_machine_class_init(ObjectClass *oc, void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "GigaDevice GD32C103RBT6 (Cortex-M4)";
    mc->init = gd32c103rbt6_init;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 0;  /* SRAM is part of SoC, not machine RAM */
}

static const TypeInfo gd32c103rbt6_machine_type = {
    .name = TYPE_GD32C103RBT6_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(GD32C103RBT6MachineState),
    .instance_init = gd32c103rbt6_machine_instance_init,
    .class_init = gd32c103rbt6_machine_class_init,
};

static void gd32c103rbt6_machine_register_types(void)
{
    type_register_static(&gd32c103rbt6_machine_type);
}

type_init(gd32c103rbt6_machine_register_types)
