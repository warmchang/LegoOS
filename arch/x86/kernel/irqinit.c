/*
 * Copyright (c) 2016 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <asm/desc.h>
#include <asm/apic.h>
#include <asm/i8259.h>
#include <asm/hw_irq.h>
#include <asm/io_apic.h>
#include <asm/irq_vectors.h>

#include <lego/irq.h>
#include <lego/acpi.h>
#include <lego/bitops.h>
#include <lego/irqdesc.h>
#include <lego/irqchip.h>

/* IRQ2 is cascade interrupt to second interrupt controller */
static struct irqaction irq2 = {
	.handler = no_action,
	.name = "cascade",
	.flags = IRQF_NO_THREAD,
};

/* TODO: per-cpu */
struct irq_desc *vector_irqs[NR_CPUS][NR_VECTORS];

struct irq_desc **per_cpu_vector_irq(int cpu)
{
	BUG_ON(!cpu_online(cpu));
	return vector_irqs[cpu];
}

/*
 * Dealing with legacy i8259 chip
 */
static void __init pre_vector_init(void)
{
	struct irq_chip *chip = legacy_pic->chip;
	int i;

	/* init i8259 itself */
	legacy_pic->init(0);

	/* The chip data was set by x86_apic_ioapic_init() */
	for (i = 0; i < nr_legacy_irqs(); i++)
		irq_set_chip_and_handler(i, chip, handle_level_irq);
}

void __init arch_irq_init(void)
{
	int i;

	x86_apic_ioapic_init();

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_LOCAL_APIC)
	/*
	 * An initial setup of the Virtual Wire Mode
	 * BSP LVT0's delivery mode is ExtINT
	 * BSP LVT1's delivery mode is NMI
	 */
	init_bsp_APIC();
#endif

	/*
	 * Set IRQ 0..nr_legacy_irqs()
	 */
	pre_vector_init();

	/*
	 * On cpu 0, Assign ISA_IRQ_VECTOR(irq) to IRQ 0..15.
	 *
	 * If these IRQ's are handled by legacy interrupt-controllers like PIC,
	 * then this configuration will likely be static after the boot. If
	 * these IRQ's are handled by more mordern controllers like IO-APIC,
	 * then this vector space can be freed and re-used dynamically as the
	 * irq's migrate etc.
	 */
	for (i = 0; i < nr_legacy_irqs(); i++) {
		struct irq_desc **vector_irq;

		vector_irq = per_cpu_vector_irq(0);
		vector_irq[ISA_IRQ_VECTOR(i)] = irq_to_desc(i);
	}

	/*
	 * SMP
	 */

#ifdef CONFIG_SMP
	/* IPI for generic function call */
	alloc_intr_gate(CALL_FUNCTION_VECTOR, call_function_interrupt);

	/* IPI for generic single function call */
	alloc_intr_gate(CALL_FUNCTION_SINGLE_VECTOR,
			call_function_single_interrupt);

	/* IPI used for rebooting/stopping */
	alloc_intr_gate(REBOOT_VECTOR, reboot_interrupt);
#endif

	/*
	 * APIC
	 */

#ifdef CONFIG_X86_LOCAL_APIC
	/* self generated IPI for local APIC timer */
	alloc_intr_gate(LOCAL_TIMER_VECTOR, apic_timer_interrupt);

	/* IPI for X86 platform specific use */
	alloc_intr_gate(X86_PLATFORM_IPI_VECTOR, x86_platform_ipi);

	/* IPI vectors for APIC spurious and error interrupts */
	alloc_intr_gate(SPURIOUS_APIC_VECTOR, spurious_interrupt);
	alloc_intr_gate(ERROR_APIC_VECTOR, error_interrupt);
#endif

	/*
	 * Cover the whole vector space, no vector can escape
	 * us. (some of these will be overridden and become
	 * 'special' SMP interrupts)
	 */
	i = FIRST_EXTERNAL_VECTOR;
#ifndef CONFIG_X86_LOCAL_APIC
#define first_system_vector NR_VECTORS
#endif
	for_each_clear_bit_from(i, used_vectors, first_system_vector) {
		set_intr_gate(i, irq_entries_start +
				8 * (i - FIRST_EXTERNAL_VECTOR));
	}

#ifdef CONFIG_X86_LOCAL_APIC
	for_each_clear_bit_from(i, used_vectors, NR_VECTORS) {
		set_intr_gate(i, spurious_interrupt);
	}
#endif

	if (!acpi_ioapic && nr_legacy_irqs())
		setup_irq(2, &irq2);
}