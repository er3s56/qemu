#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "target/arm/cpu.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"
#include "hw/char/serial.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/arm/primecell.h"

#define TYPE_LPC2119_MACHINE MACHINE_TYPE_NAME("lpc2119")

typedef struct {
    MachineState parent;
    ARMCPU *cpu;
    MemoryRegion flash;
    MemoryRegion sram;
    qemu_irq *pic;
} LPC2119State;

#define LPC2119_MACHINE(obj) \
    OBJECT_CHECK(LPC2119State, (obj), TYPE_LPC2119_MACHINE)

// Memory map
#define FLASH_BASE      0x00000000
#define SRAM_BASE       0x40000000
#define UART0_BASE      0xE000C000

// Memory sizes
#define FLASH_SIZE      (128 * KiB)
#define SRAM_SIZE       (16 * KiB)

// Simple test program (ARM assembly)
static const uint32_t test_program[] = {
    0xe3a00000,    // mov r0, #0
    0xe3a01001,    // mov r1, #1
    0xe5801000,    // str r1, [r0]
    0xeafffffe     // b .  (infinite loop)
};

static void pic_handler(void *opaque, int n, int level)
{
    ARMCPU *cpu = opaque;
    if (level) {
        cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
    }
}

static void lpc2119_init(MachineState *machine)
{
    LPC2119State *s = LPC2119_MACHINE(machine);
    Error *err = NULL;
    MemoryRegion *system_memory;

    system_memory = get_system_memory();
    if (!system_memory) {
        error_report("Failed to get system memory");
        exit(1);
    }

    printf("System memory obtained\n");

    printf("Initializing CPU...\n");
    s->cpu = ARM_CPU(cpu_create(machine->cpu_type));
    if (!s->cpu) {
        error_report("Failed to create CPU");
        exit(1);
    }

    printf("Initializing memory regions...\n");
    
    // Initialize Flash
    memory_region_init_ram_nomigrate(&s->flash, NULL, "lpc2119.flash",
                                   FLASH_SIZE, &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }
    
    // Write test program to Flash
    void *flash_ptr = memory_region_get_ram_ptr(&s->flash);
    memcpy(flash_ptr, test_program, sizeof(test_program));
    
    memory_region_set_readonly(&s->flash, true);
    memory_region_add_subregion(system_memory, FLASH_BASE, &s->flash);
    printf("Flash memory initialized at 0x%08x\n", FLASH_BASE);

    // Initialize SRAM
    memory_region_init_ram_nomigrate(&s->sram, NULL, "lpc2119.sram",
                                   SRAM_SIZE, &err);
    if (err) {
        error_report_err(err);
        exit(1);
    }
    memory_region_add_subregion(system_memory, SRAM_BASE, &s->sram);
    printf("SRAM initialized at 0x%08x\n", SRAM_BASE);

    // Initialize IRQs
    printf("Initializing IRQs...\n");
    s->pic = qemu_allocate_irqs(pic_handler, s->cpu, 32);
    if (!s->pic) {
        error_report("Failed to allocate IRQs");
        exit(1);
    }

    // Initialize UART
    printf("Initializing UART...\n");
    if (!serial_mm_init(system_memory, UART0_BASE, 2, s->pic[0],
                       115200, serial_hd(0), DEVICE_NATIVE_ENDIAN)) {
        error_report("Failed to initialize UART");
        exit(1);
    }

    // CPU initialization
    printf("Configuring CPU...\n");
    
    // Set initial CPU state
    CPUARMState *env = &s->cpu->env;
    env->regs[13] = SRAM_BASE + SRAM_SIZE; // Set stack pointer
    env->regs[15] = FLASH_BASE;            // Set PC
    env->uncached_cpsr = ARM_CPU_MODE_SVC | CPSR_F | CPSR_I; // Supervisor mode with interrupts disabled

    // Reset CPU after configuration
    cpu_reset(CPU(s->cpu));

    printf("LPC2119 initialization completed\n");
    printf("CPU PC: 0x%08x\n", env->regs[15]);
    printf("CPU SP: 0x%08x\n", env->regs[13]);
    printf("CPU CPSR: 0x%08x\n", env->uncached_cpsr);
}

static void lpc2119_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "NXP LPC2119";
    mc->init = lpc2119_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm946");
    mc->minimum_page_bits = 10;
    mc->default_ram_size = SRAM_SIZE;
    mc->default_ram_id = "lpc2119.sram";
}

static const TypeInfo lpc2119_machine_info = {
    .name = TYPE_LPC2119_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(LPC2119State),
    .class_init = lpc2119_machine_class_init,
};

static void lpc2119_machine_init(void)
{
    type_register_static(&lpc2119_machine_info);
}

type_init(lpc2119_machine_init)