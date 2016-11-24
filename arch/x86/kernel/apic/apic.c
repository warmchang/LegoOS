/*
 * Copyright (c) 2016 Wuklab, Purdue University. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <asm/asm.h>
#include <asm/apic.h>
#include <asm/processor.h>

#include <lego/irq.h>
#include <lego/kernel.h>
#include <lego/bitops.h>
#include <lego/cpumask.h>

unsigned int nr_cpus;

/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
u8 boot_cpu_apic_version;

/* The highest APIC ID seen during enumaration */
unsigned int max_physical_apicid;

unsigned long mp_lapic_addr;

unsigned long apic_phys;

#ifdef CONFIG_X86_X2APIC
int x2apic_mode;

void __init x2apic_enable(void)
{
	u64 msr;

	rdmsrl(MSR_IA32_APICBASE, msr);
	if (msr & X2APIC_ENABLE)
		return;
	wrmsrl(MSR_IA32_APICBASE, msr | X2APIC_ENABLE);
	printk(KERN_INFO "x2apic enabled\n");
}

void __init check_x2apic(void)
{
	if (x2apic_enabled())
		pr_info("x2apic: enabled by BIOS, switching to x2apic\n");
	else if (!x2apic_supported())
		pr_info("x2apic: not supported by your CPU\n");
	else
		pr_info("x2apic: disabled by your BIOS\n");
}
#endif

void __init setup_apic_driver(void)
{
	struct apic **drv;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		if ((*drv)->probe && (*drv)->probe()) {
			if (apic != *drv) {
				apic = *drv;
				pr_info("Switched APIC routing to %s.\n",
					apic->name);
			}
			return;
		}
	}
	panic("APIC: no driver found");
}

void native_apic_icr_write(u32 low, u32 id)
{
	unsigned long flags;

	local_irq_save(flags);
	apic_write(APIC_ICR2, SET_APIC_DEST_FIELD(id));
	apic_write(APIC_ICR, low);
	local_irq_restore(flags);
}

u64 native_apic_icr_read(void)
{
	u32 icr1, icr2;

	icr2 = apic_read(APIC_ICR2);
	icr1 = apic_read(APIC_ICR);

	return icr1 | ((u64)icr2 << 32);
}

void native_apic_wait_icr_idle(void)
{
	while (apic_read(APIC_ICR) & APIC_ICR_BUSY)
		cpu_relax();
}

u32 native_safe_apic_wait_icr_idle(void)
{
	u32 send_status;
	int timeout;

	timeout = 0;
	do {
		send_status = apic_read(APIC_ICR) & APIC_ICR_BUSY;
		if (!send_status)
			break;
		//udelay(100);
	} while (timeout++ < 1000);

	return send_status;
}

/*
 * The number of allocated logical CPU IDs.
 * Since logical CPU IDs are allocated contiguously, it equals to current
 * allocated max logical CPU ID plus 1. All allocated CPU ID should be
 * in [0, nr_logical_cpuidi), so the maximum of nr_logical_cpuids is nr_cpu_ids.
 *
 * NOTE: Reserve 0 for BSP.
 */
static int nr_logical_cpuids = 1;

/* Used to store mapping between logical CPU IDs and APIC IDs */
static int cpuid_to_apicid[] = {
	[0 ... NR_CPUS - 1] = -1,
};

/* Present physical APIC IDs */
static DECLARE_BITMAP(phys_apicid_present_map, MAX_LOCAL_APIC);

static int allocate_logical_cpuid(int apicid)
{
	int i;

	/*
	 * cpuid <-> apicid mapping is persistent, so when a cpu is up,
	 * check if the kernel has allocated a cpuid for it.
	 */
	for (i = 0; i < nr_logical_cpuids; i++) {
		if (cpuid_to_apicid[i] == apicid)
			return i;
	}

	/* Allocate a new cpuid. */
	if (nr_logical_cpuids >= nr_cpu_ids) {
		WARN_ONCE(1, "Only %d processors supported."
			     "Processor %d/0x%x and the rest are ignored.\n",
			     nr_cpu_ids - 1, nr_logical_cpuids, apicid);
		return -1;
	}

	cpuid_to_apicid[nr_logical_cpuids] = apicid;
	return nr_logical_cpuids++;
}

/**
 * apic_register_new_cpu - Register a new CPU with APIC ID
 * @apicid: APIC ID
 * @enabled: is this CPU enabled
 * return: the logical cpu number
 */
int apic_register_new_cpu(int apicid, int enabled)
{
	int cpu;
	bool boot_cpu_detected;

	boot_cpu_detected = test_bit(boot_cpu_physical_apicid, phys_apicid_present_map);

	/*
	 * If boot cpu has not been detected yet, then only allow upto
	 * nr_cpu_ids - 1 processors and keep one slot free for boot cpu
	 */
	if (!boot_cpu_detected && nr_cpus >= nr_cpu_ids - 1 &&
	    apicid != boot_cpu_physical_apicid) {
		pr_warn(
			"APIC: NR_CPUS/possible_cpus limit of %i almost"
			" reached. Keeping one slot for boot cpu."
			"  Processor 0x%x ignored.\n", nr_cpu_ids, apicid);
		return -ENODEV;
	}

	if (nr_cpus >= nr_cpu_ids) {
		if (enabled) {
			pr_warn("APIC: NR_CPUS/possible_cpus limit of %i "
				   "reached. Processor 0x%x ignored.\n",
				   nr_cpu_ids, apicid);
		}
		return -EINVAL;
	}

	if (apicid == boot_cpu_physical_apicid) {
		/*
		 * x86_bios_cpu_apicid is required to have processors listed
		 * in same order as logical cpu numbers. Hence the first
		 * entry is BSP, and so on.
		 * boot_cpu_init() already hold bit 0 in cpu_present_mask
		 * for BSP.
		 */
		cpu = 0;

		/* Logical cpuid 0 is reserved for BSP. */
		cpuid_to_apicid[0] = apicid;
	} else {
		cpu = allocate_logical_cpuid(apicid);
		if (cpu < 0) {
			pr_warn("APIC: fail to allocate logical cpuid\n");
			return -EINVAL;
		}
	}

	if (apicid > max_physical_apicid)
		max_physical_apicid = apicid;

	set_cpu_possible(cpu, true);
	if (enabled) {
		nr_cpus++;
		set_bit(apicid, phys_apicid_present_map);
		set_cpu_present(cpu, true);
	}

	return cpu;
}

/**
 * lapic_get_maxlvt
 * Get the maximum number of local vector table entries
 */
int lapic_get_maxlvt(void)
{
	unsigned int v;

	v = apic_read(APIC_LVR);
	return GET_APIC_MAXLVT(v);
}

void __init register_lapic_address(unsigned long address)
{
	mp_lapic_addr = address;

	if (!x2apic_mode) {
		set_fixmap(FIX_APIC_BASE, address);
		pr_info("Mapped APIC to %16lx (%16lx)\n", APIC_BASE, address);
	}
	if (boot_cpu_physical_apicid == -1U) {
		boot_cpu_physical_apicid  = read_apic_id();
		boot_cpu_apic_version = GET_APIC_VERSION(apic_read(APIC_LVR));
	}
}

void __init init_apic_mappings(void)
{

}
