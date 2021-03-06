/*
 *  linux/arch/arm/mach-sun6i/hotplug.c
 *
 *  Copyright (C) 2012-2016 Allwinner Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/hardware/gic.h>
#include <mach/platform.h>
#include <mach/hardware.h>



static cpumask_t dead_cpus;

#define IS_WFI_MODE(cpu)    (readl(IO_ADDRESS(AW_R_CPUCFG_BASE) + CPUX_STATUS(cpu)) & (1<<2))

int platform_cpu_kill(unsigned int cpu)
{
    int k;
    u32 pwr_reg;

    int tmp_cpu = get_cpu();
    put_cpu();
    pr_info("[hotplug]: cpu(%d) try to kill cpu(%d)\n", tmp_cpu, cpu);

    for (k = 0; k < 1000; k++) {
        if (cpumask_test_cpu(cpu, &dead_cpus) && IS_WFI_MODE(cpu)) {

            /* step9: set up power-off signal */
            pwr_reg = readl(IO_ADDRESS(AW_R_PRCM_BASE) + AW_CPU_PWROFF_REG);
            pwr_reg |= (1<<cpu);
            writel(pwr_reg, IO_ADDRESS(AW_R_PRCM_BASE) + AW_CPU_PWROFF_REG);
            usleep_range(1000, 1000);

            /* step10: active the power output clamp */
            writel(0xff, IO_ADDRESS(AW_R_PRCM_BASE) + AW_CPUX_PWR_CLAMP(cpu));
            pr_info("[hotplug]: cpu%d is killed! .\n", cpu);

            return 1;
        }

        msleep(1);
    }

    pr_err("[hotplug]: try to kill cpu:%d failed!\n", cpu);

    return 0;
}


void platform_cpu_die(unsigned int cpu)
{
    unsigned long actlr;

    gic_cpu_exit(0);

    /* notify platform_cpu_kill() that hardware shutdown is finished */
    cpumask_set_cpu(cpu, &dead_cpus);

    /* step1: disable cache */
    asm("mrc    p15, 0, %0, c1, c0, 0" : "=r" (actlr) );
    actlr &= ~(1<<2);
    asm("mcr    p15, 0, %0, c1, c0, 0\n" : : "r" (actlr));

    /* step2: clean and ivalidate L1 cache */
    flush_cache_all();

    /* step3: execute a CLREX instruction */
    asm("clrex" : : : "memory", "cc");

    /* step4: switch cpu from SMP mode to AMP mode, aim is to disable cache coherency */
    asm("mrc    p15, 0, %0, c1, c0, 1" : "=r" (actlr) );
    actlr &= ~(1<<6);
    asm("mcr    p15, 0, %0, c1, c0, 1\n" : : "r" (actlr));

    /* step5: execute an ISB instruction */
    isb();
    /* step6: execute a DSB instruction  */
    dsb();

    /* step7: execute a WFI instruction */
    while(1) {
        asm("wfi" : : : "memory", "cc");
    }
}

int platform_cpu_disable(unsigned int cpu)
{
    cpumask_clear_cpu(cpu, &dead_cpus);
    /*
     * we don't allow CPU 0 to be shutdown (it is still too special
     * e.g. clock tick interrupts)
     */
    return cpu == 0 ? -EPERM : 0;
}

