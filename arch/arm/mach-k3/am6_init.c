// SPDX-License-Identifier: GPL-2.0+
/*
 * K3: Architecture initialization
 *
 * Copyright (C) 2017-2018 Texas Instruments Incorporated - http://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 */

#include <common.h>
#include <asm/io.h>
#include <spl.h>
#include <asm/arch/hardware.h>
#include <asm/arch/sysfw-loader.h>
#include "common.h"
#include <dm.h>
#include <dm/uclass-internal.h>
#include <dm/pinctrl.h>

#ifdef CONFIG_SPL_BUILD
static void mmr_unlock(u32 base, u32 partition)
{
	/* Translate the base address */
	phys_addr_t part_base = base + partition * CTRL_MMR0_PARTITION_SIZE;

	/* Unlock the requested partition if locked using two-step sequence */
	writel(CTRLMMR_LOCK_KICK0_UNLOCK_VAL, part_base + CTRLMMR_LOCK_KICK0);
	writel(CTRLMMR_LOCK_KICK1_UNLOCK_VAL, part_base + CTRLMMR_LOCK_KICK1);
}

static void ctrl_mmr_unlock(void)
{
	/* Unlock all WKUP_CTRL_MMR0 module registers */
	mmr_unlock(WKUP_CTRL_MMR0_BASE, 0);
	mmr_unlock(WKUP_CTRL_MMR0_BASE, 1);
	mmr_unlock(WKUP_CTRL_MMR0_BASE, 2);
	mmr_unlock(WKUP_CTRL_MMR0_BASE, 3);
	mmr_unlock(WKUP_CTRL_MMR0_BASE, 6);
	mmr_unlock(WKUP_CTRL_MMR0_BASE, 7);

	/* Unlock all MCU_CTRL_MMR0 module registers */
	mmr_unlock(MCU_CTRL_MMR0_BASE, 0);
	mmr_unlock(MCU_CTRL_MMR0_BASE, 1);
	mmr_unlock(MCU_CTRL_MMR0_BASE, 2);
	mmr_unlock(MCU_CTRL_MMR0_BASE, 6);

	/* Unlock all CTRL_MMR0 module registers */
	mmr_unlock(CTRL_MMR0_BASE, 0);
	mmr_unlock(CTRL_MMR0_BASE, 1);
	mmr_unlock(CTRL_MMR0_BASE, 2);
	mmr_unlock(CTRL_MMR0_BASE, 3);
	mmr_unlock(CTRL_MMR0_BASE, 6);
	mmr_unlock(CTRL_MMR0_BASE, 7);
}

static void store_boot_index_from_rom(void)
{
	u32 *boot_index = (u32 *)K3_BOOT_PARAM_TABLE_INDEX_VAL;

	*boot_index = *(u32 *)(CONFIG_SYS_K3_BOOT_PARAM_TABLE_INDEX);
}

void board_init_f(ulong dummy)
{
#if defined(CONFIG_K3_LOAD_SYSFW) || defined(CONFIG_K3_AM654_DDRSS)
	struct udevice *dev;
	int ret;
#endif
	/*
	 * Cannot delay this further as there is a chance that
	 * K3_BOOT_PARAM_TABLE_INDEX can be over written by SPL MALLOC section.
	 */
	store_boot_index_from_rom();

	/* Make all control module registers accessible */
	ctrl_mmr_unlock();

#ifdef CONFIG_CPU_V7R
	setup_k3_mpu_regions();

	/*
	 * When running SPL on R5 we are using SRAM for BSS to have global
	 * data etc. working prior to relocation. Since this means we need
	 * to self-manage BSS, clear that section now.
	 */
	memset(__bss_start, 0, __bss_end - __bss_start);
#endif

	/* Init DM early in-order to invoke system controller */
	spl_early_init();

#ifdef CONFIG_K3_EARLY_CONS
	/*
	 * Allow establishing an early console as required for example when
	 * doing a UART-based boot. Note that this console may not "survive"
	 * through a SYSFW PM-init step and will need a re-init in some way
	 * due to changing module clock frequencies.
	 */
	early_console_init();
#endif

#ifdef CONFIG_K3_LOAD_SYSFW
	/*
	 * Process pinctrl for the serial0 a.k.a. WKUP_UART0 module and continue
	 * regardless of the result of pinctrl. Do this without probing the
	 * device, but instead by searching the device that would request the
	 * given sequence number if probed. The UART will be used by the system
	 * firmware (SYSFW) image for various purposes and SYSFW depends on us
	 * to initialize its pin settings.
	 */
	ret = uclass_find_device_by_seq(UCLASS_SERIAL, 0, true, &dev);
	if (!ret) {
		pinctrl_select_state(dev, "default");
	}

	/*
	 * Load, start up, and configure system controller firmware. Provide
	 * the U-Boot console init function to the SYSFW post-PM configuration
	 * callback hook, effectively switching on (or over) the console
	 * output.
	 */
	k3_sysfw_loader(preloader_console_init);
#else
	/* Prepare console output */
	preloader_console_init();
#endif

#ifdef CONFIG_K3_AM654_DDRSS
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		printf("DRAM init failed: %d\n", ret);
		return;
	}
#endif
}

u32 spl_boot_mode(const u32 boot_device)
{
#if defined(CONFIG_SUPPORT_EMMC_BOOT)
	u32 devstat = readl(CTRLMMR_MAIN_DEVSTAT);
	u32 bootindex = readl(K3_BOOT_PARAM_TABLE_INDEX_VAL);

	u32 bootmode = (devstat & CTRLMMR_MAIN_DEVSTAT_BOOTMODE_MASK) >>
			CTRLMMR_MAIN_DEVSTAT_BOOTMODE_SHIFT;

	/* eMMC boot0 mode is only supported for primary boot */
	if (bootindex == K3_PRIMARY_BOOTMODE &&
	    bootmode == BOOT_DEVICE_MMC1)
		return MMCSD_MODE_EMMCBOOT;
#endif

	/* Everything else use filesystem if available */
#if defined(CONFIG_SPL_FAT_SUPPORT) || defined(CONFIG_SPL_EXT_SUPPORT)
	return MMCSD_MODE_FS;
#else
	return MMCSD_MODE_RAW;
#endif
}

static u32 __get_backup_bootmedia(u32 devstat)
{
	u32 bkup_boot = (devstat & CTRLMMR_MAIN_DEVSTAT_BKUP_BOOTMODE_MASK) >>
			CTRLMMR_MAIN_DEVSTAT_BKUP_BOOTMODE_SHIFT;

	switch (bkup_boot) {
	case BACKUP_BOOT_DEVICE_USB:
		return BOOT_DEVICE_USB;
	case BACKUP_BOOT_DEVICE_UART:
		return BOOT_DEVICE_UART;
	case BACKUP_BOOT_DEVICE_ETHERNET:
		return BOOT_DEVICE_ETHERNET;
	case BACKUP_BOOT_DEVICE_MMC2:
	{
		u32 port = (devstat & CTRLMMR_MAIN_DEVSTAT_BKUP_MMC_PORT_MASK) >>
			    CTRLMMR_MAIN_DEVSTAT_BKUP_MMC_PORT_SHIFT;
		if (port == 0x0)
			return BOOT_DEVICE_MMC1;
		return BOOT_DEVICE_MMC2;
	}
	case BACKUP_BOOT_DEVICE_SPI:
		return BOOT_DEVICE_SPI;
	case BACKUP_BOOT_DEVICE_HYPERFLASH:
		return BOOT_DEVICE_HYPERFLASH;
	case BACKUP_BOOT_DEVICE_I2C:
		return BOOT_DEVICE_I2C;
	};

	return BOOT_DEVICE_RAM;
}

static u32 __get_primary_bootmedia(u32 devstat)
{
	u32 bootmode = (devstat & CTRLMMR_MAIN_DEVSTAT_BOOTMODE_MASK) >>
			CTRLMMR_MAIN_DEVSTAT_BOOTMODE_SHIFT;

	if (bootmode == BOOT_DEVICE_OSPI || bootmode ==	BOOT_DEVICE_QSPI)
		bootmode = BOOT_DEVICE_SPI;

	if (bootmode == BOOT_DEVICE_MMC2) {
		u32 port = (devstat & CTRLMMR_MAIN_DEVSTAT_MMC_PORT_MASK) >>
			    CTRLMMR_MAIN_DEVSTAT_MMC_PORT_SHIFT;
		if (port == 0x0)
			bootmode = BOOT_DEVICE_MMC1;
	} else if (bootmode == BOOT_DEVICE_MMC1) {
		u32 port = (devstat & CTRLMMR_MAIN_DEVSTAT_EMMC_PORT_MASK) >>
			    CTRLMMR_MAIN_DEVSTAT_EMMC_PORT_SHIFT;
		if (port == 0x1)
			bootmode = BOOT_DEVICE_MMC2;
	}

	return bootmode;
}

u32 spl_boot_device(void)
{
	u32 devstat = readl(CTRLMMR_MAIN_DEVSTAT);
	u32 bootindex = readl(K3_BOOT_PARAM_TABLE_INDEX_VAL);

	if (bootindex == K3_PRIMARY_BOOTMODE)
		return __get_primary_bootmedia(devstat);
	else
		return __get_backup_bootmedia(devstat);
}
#endif

#ifndef CONFIG_SYSRESET
void reset_cpu(ulong ignored)
{
}
#endif
