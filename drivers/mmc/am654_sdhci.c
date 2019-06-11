// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Texas Instruments' K3 SD Host Controller Interface
 */

#include <clk.h>
#include <common.h>
#include <dm.h>
#include <malloc.h>
#include <power-domain.h>
#include <sdhci.h>

#define AM654_SDHCI_MIN_FREQ	400000

struct am654_sdhci_plat {
	struct mmc_config cfg;
	struct mmc mmc;
	unsigned int f_max;
};

static int am654_sdhci_probe(struct udevice *dev)
{
	struct am654_sdhci_plat *plat = dev_get_platdata(dev);
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	struct power_domain sdhci_pwrdmn;
	struct clk clk;
	unsigned long clock;
	int ret;

	ret = power_domain_get_by_index(dev, &sdhci_pwrdmn, 0);
	if (ret) {
		dev_err(dev, "failed to get power domain\n");
		return ret;
	}

	ret = power_domain_on(&sdhci_pwrdmn);
	if (ret) {
		dev_err(dev, "Power domain on failed\n");
		return ret;
	}

	ret = clk_get_by_index(dev, 0, &clk);
	if (ret) {
		dev_err(dev, "failed to get clock\n");
		return ret;
	}

	clock = clk_get_rate(&clk);
	if (IS_ERR_VALUE(clock)) {
		dev_err(dev, "failed to get rate\n");
		return clock;
	}

	host->max_clk = clock;

	ret = sdhci_setup_cfg(&plat->cfg, host, plat->f_max,
			      AM654_SDHCI_MIN_FREQ);
	host->mmc = &plat->mmc;
	if (ret)
		return ret;
	host->mmc->priv = host;
	host->mmc->dev = dev;
	upriv->mmc = host->mmc;

	return sdhci_probe(dev);
}

static int am654_sdhci_ofdata_to_platdata(struct udevice *dev)
{
	struct am654_sdhci_plat *plat = dev_get_platdata(dev);
	struct sdhci_host *host = dev_get_priv(dev);

	host->name = dev->name;
	host->ioaddr = (void *)dev_read_addr(dev);
	host->bus_width = dev_read_u32_default(dev, "bus-width", 4);
	plat->f_max = dev_read_u32_default(dev, "max-frequency", 0);

	return 0;
}

static int am654_sdhci_bind(struct udevice *dev)
{
	struct am654_sdhci_plat *plat = dev_get_platdata(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id am654_sdhci_ids[] = {
	{ .compatible = "ti,am654-sdhci-5.1" },
	{ }
};

U_BOOT_DRIVER(am654_sdhci_drv) = {
	.name		= "am654_sdhci",
	.id		= UCLASS_MMC,
	.of_match	= am654_sdhci_ids,
	.ofdata_to_platdata = am654_sdhci_ofdata_to_platdata,
	.ops		= &sdhci_ops,
	.bind		= am654_sdhci_bind,
	.probe		= am654_sdhci_probe,
	.priv_auto_alloc_size = sizeof(struct sdhci_host),
	.platdata_auto_alloc_size = sizeof(struct am654_sdhci_plat),
};
