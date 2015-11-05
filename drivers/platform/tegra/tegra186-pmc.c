/*
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/export.h>
#include <linux/tegra-pmc.h>
#include <linux/tegra_prod.h>
#include <iomap.h>

#define PMC_CTRL			0x0
#define PMC_PWRGATE_TOGGLE		0x30
#define PMC_PWRGATE_TOGGLE_START	(1 << 8)

#define PMC_SLCG_CTRL			0x4
#define PMC_DPD_PADS_ORIDE		0x8
#define PMC_SC7_CONFIG			0x14
#define PMC_SC7_STATUS			0x18
#define PMC_IMPL_PWRGOOD_TIMER	0x2C
#define PMC_BLINK_TIMER			0x30
#define PMC_NO_IOPOWER			0x34
#define PMC_DDR_PWR				0x38
#define PMC_E_18V_PWR			0x3C

#define PMC_E_33V_PWR			0x40
#define PMC_E_33V_SDMMC1HV_MASK		BIT(4)
#define PMC_E_33V_SDMMC2HV_MASK		BIT(5)
#define PMC_E_33V_SDMMC3HV_MASK		BIT(6)

#define PMC_SENSOR_CTRL			0x6C
#define PMC_SCRATCH_WRITE_MASK			BIT(2)
#define PMC_ENABLE_RST_MASK		BIT(1)

#define PMC_SATA_PWRGT_0		0x68

#define PMC_RST_STATUS			0x70
#define PMC_RST_LEVEL_MASK		0x3
#define PMC_RST_SOURCE			0x3C

#define PMC_IO_DPD_REQ			0x74
#define PMC_IO_DPD_CSIA_MASK	BIT(0)
#define PMC_IO_DPD_CSIB_MASK	BIT(1)

#define PMC_IO_DPD_STATUS		0x78
#define PMC_IO_DPD2_REQ			0x7C
#define PMC_IO_DPD2_CSIC_MASK	BIT(11)
#define PMC_IO_DPD2_CSID_MASK	BIT(12)
#define PMC_IO_DPD2_CSIE_MASK	BIT(13)
#define PMC_IO_DPD2_CSIF_MASK	BIT(14)

#define PMC_IO_DPD2_STATUS		0x80
#define PMC_IO_DPD3_REQ			0x84
#define PMC_IO_DPD3_STATUS		0x88
#define PMC_IO_DPD4_REQ			0x8C
#define PMC_IO_DPD4_STATUS		0x90
#define PMC_IO_DPD5_REQ			0x94
#define PMC_IO_DPD5_STATUS		0x98
#define PMC_IO_DPD6_REQ			0x9C
#define PMC_IO_DPD6_STATUS		0xA0
#define PMC_IO_DPD7_REQ			0xA4
#define PMC_IO_DPD7_STATUS		0xA8
#define PMC_IO_DPD8_REQ			0xAC
#define PMC_IO_DPD8_STATUS		0xB0
#define PMC_IO_DPD7_OFF_MASK	0xb4
#define PMC_IO_DPD8_OFF_MASK	0xb8

#define PMC_IO_SEL_DPD_TIM		0xBC
#define PMC_DSI_SEL_DPD			0xD0

#define PMC_TSC_MULT0			0xD4
#define PMC_UFSHC_PWR_CNTRL_0		0xF4

#define PMC_FUSE_CTRL			0x100
#define PMC_FUSE_CTRL_ENABLE_REDIRECTION	(1 << 0)
#define PMC_FUSE_CTRL_DISABLE_REDIRECTION	(1 << 1)
#define PMC_FUSE_CTRL_PS18_LATCH_SET	(1 << 8)
#define PMC_FUSE_CTRL_PS18_LATCH_CLEAR	(1 << 9)

#define PMC_THERMTRIP_CFG		0x104
#define PMC_THERMTRIP_CFG_LOCK_MASK		BIT(5)

#define PMC_IMPL_RAMDUMP_CTL_STATUS	0x10c
#define PMC_IMPL_HALT_IN_FIQ_MASK	BIT(28)

#define PMC_DDR_CNTRL			0x11C

static DEFINE_SPINLOCK(tegra186_pmc_access_lock);

void __iomem *tegra186_pmc_base;

static inline u32 tegra186_pmc_readl(u32 reg)
{
	return readl(tegra186_pmc_base + reg);
}

static inline void tegra186_pmc_writel(u32 val, u32 reg)
{
	writel(val, tegra186_pmc_base + reg);
}

static void _tegra186_pmc_register_update(int offset,
		unsigned long mask, unsigned long val)
{
	u32 pmc_reg;

	pmc_reg = tegra186_pmc_readl(offset);
	pmc_reg = (pmc_reg & ~mask) | (val & mask);
	tegra186_pmc_writel(pmc_reg, offset);
}

void tegra186_pmc_register_update(int offset,
		unsigned long mask, unsigned long val)
{
	unsigned long flags;

	spin_lock_irqsave(&tegra186_pmc_access_lock, flags);
	_tegra186_pmc_register_update(offset, mask, val);
	spin_unlock_irqrestore(&tegra186_pmc_access_lock, flags);
}
EXPORT_SYMBOL(tegra186_pmc_register_update);

unsigned long tegra_pmc_register_get(u32 offset)
{
	return tegra186_pmc_readl(offset);
}
EXPORT_SYMBOL(tegra_pmc_register_get);

int tegra186_pmc_io_dpd_enable(int reg, int bit_pos)
{
	unsigned int enable_mask;
	unsigned int dpd_status;

	spin_lock(&tegra186_pmc_access_lock);
	tegra186_pmc_writel(0x10, PMC_IO_SEL_DPD_TIM);
	enable_mask = (1 << bit_pos);

	tegra186_pmc_writel(enable_mask, (PMC_IO_DPD_REQ + reg * 8));
	udelay(7);

	dpd_status = tegra186_pmc_readl(PMC_IO_DPD_STATUS + reg * 8);
	if (!(dpd_status & (1 << bit_pos))) {
		pr_info("Error: dpd%d enable failed, status=%#x\n",
				(reg + 1), dpd_status);
	}
	spin_unlock(&tegra186_pmc_access_lock);
	return 0;
}
EXPORT_SYMBOL(tegra186_pmc_io_dpd_enable);

int tegra186_pmc_io_dpd_disable(int reg, int bit_pos)
{
	unsigned int enable_mask;
	unsigned int dpd_status;

	spin_lock(&tegra186_pmc_access_lock);
	enable_mask = (1 << bit_pos);

	tegra186_pmc_writel(enable_mask, PMC_IO_DPD_REQ + reg * 8);
	udelay(7);

	dpd_status = tegra186_pmc_readl(PMC_IO_DPD_STATUS + reg * 8);
	if (dpd_status & (1 << bit_pos)) {
		pr_info("Error: dpd%d disable failed, status=%#x\n",
				(reg + 1), dpd_status);
	}
	spin_unlock(&tegra186_pmc_access_lock);
	return 0;
}
EXPORT_SYMBOL(tegra186_pmc_io_dpd_disable);

int tegra186_pmc_io_dpd_get_status(int reg, int bit_pos)
{
	unsigned int dpd_status;

	dpd_status = tegra186_pmc_readl(PMC_IO_DPD_STATUS + reg * 8);
	if (dpd_status & BIT(bit_pos))
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(tegra186_pmc_io_dpd_get_status);

void tegra_pmc_pad_voltage_update(unsigned int reg,
		unsigned long mask, unsigned long val)
{
	unsigned long flags;

	spin_lock_irqsave(&tegra186_pmc_access_lock, flags);
	_tegra186_pmc_register_update(reg, mask, val);
	spin_unlock_irqrestore(&tegra186_pmc_access_lock, flags);
}
EXPORT_SYMBOL(tegra_pmc_pad_voltage_update);

unsigned long tegra_pmc_pad_voltage_get(unsigned int reg)
{
	return tegra186_pmc_readl(reg);
}
EXPORT_SYMBOL(tegra_pmc_pad_voltage_get);

void tegra_pmc_nvcsi_ab_brick_update(unsigned long mask, unsigned long val)
{
	unsigned long flags;

	spin_lock_irqsave(&tegra186_pmc_access_lock, flags);
	_tegra186_pmc_register_update(PMC_IO_DPD_REQ, mask, val);
	spin_unlock_irqrestore(&tegra186_pmc_access_lock, flags);
}
EXPORT_SYMBOL(tegra_pmc_nvcsi_ab_brick_update);

unsigned long tegra_pmc_nvcsi_ab_brick_getstatus(void)
{
	return tegra186_pmc_readl(PMC_IO_DPD_REQ);
}
EXPORT_SYMBOL(tegra_pmc_nvcsi_ab_brick_getstatus);

void tegra_pmc_nvcsi_cdef_brick_update(unsigned long mask, unsigned long val)
{
	unsigned long flags;

	spin_lock_irqsave(&tegra186_pmc_access_lock, flags);
	_tegra186_pmc_register_update(PMC_IO_DPD2_REQ, mask, val);
	spin_unlock_irqrestore(&tegra186_pmc_access_lock, flags);
}
EXPORT_SYMBOL(tegra_pmc_nvcsi_cdef_brick_update);

unsigned long tegra_pmc_nvcsi_cdef_brick_getstatus(void)
{
	return tegra186_pmc_readl(PMC_IO_DPD2_REQ);
}
EXPORT_SYMBOL(tegra_pmc_nvcsi_cdef_brick_getstatus);

void tegra186_pmc_enable_nvcsi_brick_dpd(void)
{
	u32 val;

	val = tegra186_pmc_readl(PMC_IO_DPD_REQ);
	val |= PMC_IO_DPD_CSIA_MASK;
	val |= PMC_IO_DPD_CSIB_MASK;
	tegra186_pmc_writel(val, PMC_IO_DPD_REQ);

	val = tegra186_pmc_readl(PMC_IO_DPD2_REQ);
	val |= (PMC_IO_DPD2_CSIC_MASK | PMC_IO_DPD2_CSID_MASK |
				PMC_IO_DPD2_CSIE_MASK | PMC_IO_DPD2_CSIF_MASK);
	tegra186_pmc_writel(val, PMC_IO_DPD2_REQ);
}
EXPORT_SYMBOL(tegra186_pmc_enable_nvcsi_brick_dpd);

void tegra186_pmc_disable_nvcsi_brick_dpd(void)
{
	u32 val;

	val = tegra186_pmc_readl(PMC_IO_DPD_REQ);
	val &= ~(PMC_IO_DPD_CSIA_MASK | PMC_IO_DPD_CSIB_MASK);
	tegra186_pmc_writel(val, PMC_IO_DPD_REQ);

	val = tegra186_pmc_readl(PMC_IO_DPD2_REQ);
	val &= ~(PMC_IO_DPD2_CSIC_MASK | PMC_IO_DPD2_CSID_MASK |
				PMC_IO_DPD2_CSIE_MASK | PMC_IO_DPD2_CSIF_MASK);
	tegra186_pmc_writel(val, PMC_IO_DPD2_REQ);
}
EXPORT_SYMBOL(tegra186_pmc_disable_nvcsi_brick_dpd);

void tegra_pmc_ufs_pwrcntrl_update(unsigned long mask, unsigned long val)
{
	unsigned long flags;

	spin_lock_irqsave(&tegra186_pmc_access_lock, flags);
	_tegra186_pmc_register_update(PMC_UFSHC_PWR_CNTRL_0, mask, val);
	spin_unlock_irqrestore(&tegra186_pmc_access_lock, flags);
}
EXPORT_SYMBOL(tegra_pmc_ufs_pwrcntrl_update);

unsigned long tegra_pmc_ufs_pwrcntrl_get(void)
{
	return tegra186_pmc_readl(PMC_UFSHC_PWR_CNTRL_0);
}
EXPORT_SYMBOL(tegra_pmc_ufs_pwrcntrl_get);

void tegra_pmc_iopower_enable(int reg, u32 bit_mask)
{
	tegra186_pmc_register_update(reg, bit_mask, 0);
}
EXPORT_SYMBOL(tegra_pmc_iopower_enable);

void  tegra_pmc_iopower_disable(int reg, u32 bit_mask)
{
	tegra186_pmc_register_update(reg, bit_mask, bit_mask);
}
EXPORT_SYMBOL(tegra_pmc_iopower_disable);

int tegra_pmc_iopower_get_status(int reg, u32 bit_mask)
{
	unsigned int no_iopower;

	no_iopower = tegra186_pmc_readl(reg);
	if (no_iopower & bit_mask)
		return 0;
	else
		return 1;
}
EXPORT_SYMBOL(tegra_pmc_iopower_get_status);

void tegra_pmc_sata_pwrgt_update(unsigned long mask, unsigned long val)
{
	unsigned long flags;

	spin_lock_irqsave(&tegra186_pmc_access_lock, flags);
	_tegra186_pmc_register_update(PMC_SATA_PWRGT_0, mask, val);
	spin_unlock_irqrestore(&tegra186_pmc_access_lock, flags);
}
EXPORT_SYMBOL(tegra_pmc_sata_pwrgt_update);

unsigned long tegra_pmc_sata_pwrgt_get(void)
{
	return tegra186_pmc_readl(PMC_SATA_PWRGT_0);
}
EXPORT_SYMBOL(tegra_pmc_sata_pwrgt_get);

void tegra_pmc_fuse_control_ps18_latch_set(void)
{
	u32 val;

	val = tegra186_pmc_readl(PMC_FUSE_CTRL);
	val &= ~(PMC_FUSE_CTRL_PS18_LATCH_CLEAR);
	tegra186_pmc_writel(val, PMC_FUSE_CTRL);
	mdelay(1);
	val |= PMC_FUSE_CTRL_PS18_LATCH_SET;
	tegra186_pmc_writel(val, PMC_FUSE_CTRL);
	mdelay(1);
}
EXPORT_SYMBOL(tegra_pmc_fuse_control_ps18_latch_set);

void tegra_pmc_fuse_control_ps18_latch_clear(void)
{
	u32 val;

	val = tegra186_pmc_readl(PMC_FUSE_CTRL);
	val &= ~(PMC_FUSE_CTRL_PS18_LATCH_SET);
	tegra186_pmc_writel(val, PMC_FUSE_CTRL);
	mdelay(1);
	val |= PMC_FUSE_CTRL_PS18_LATCH_CLEAR;
	tegra186_pmc_writel(val, PMC_FUSE_CTRL);
	mdelay(1);
}
EXPORT_SYMBOL(tegra_pmc_fuse_control_ps18_latch_clear);

void tegra_pmc_fuse_disable_mirroring(void)
{
	u32 val;

	val = tegra186_pmc_readl(PMC_FUSE_CTRL);
	if (val & PMC_FUSE_CTRL_ENABLE_REDIRECTION)
		tegra186_pmc_writel(PMC_FUSE_CTRL_DISABLE_REDIRECTION,
				PMC_FUSE_CTRL);
}
EXPORT_SYMBOL(tegra_pmc_fuse_disable_mirroring);

void tegra_pmc_fuse_enable_mirroring(void)
{
	u32 val;

	val = tegra186_pmc_readl(PMC_FUSE_CTRL);
	if (!(val & PMC_FUSE_CTRL_ENABLE_REDIRECTION))
		tegra186_pmc_writel(PMC_FUSE_CTRL_ENABLE_REDIRECTION,
				PMC_FUSE_CTRL);
}
EXPORT_SYMBOL(tegra_pmc_fuse_enable_mirroring);

bool tegra_pmc_is_halt_in_fiq(void)
{
	return !!(PMC_IMPL_HALT_IN_FIQ_MASK &
		tegra186_pmc_readl(PMC_IMPL_RAMDUMP_CTL_STATUS));
}
EXPORT_SYMBOL(tegra_pmc_is_halt_in_fiq);

static const struct of_device_id tegra186_pmc[] __initconst = {
	{ .compatible = "nvidia,tegra186-pmc" },
	{ }
};

static int tegra186_pmc_parse_dt(struct device_node *np)
{
	if (!np)
		return -EINVAL;

	tegra186_pmc_base = of_iomap(np, 0);

	if (of_property_read_bool(np, "nvidia,enable-halt-in-fiq"))
		tegra186_pmc_register_update(PMC_IMPL_RAMDUMP_CTL_STATUS,
			PMC_IMPL_HALT_IN_FIQ_MASK, PMC_IMPL_HALT_IN_FIQ_MASK);

	return 0;
}

static void tegra186_pmc_dev_release(struct device *dev)
{
}
static struct device tegra186_pmc_dev = { };
static struct tegra_prod_list *prod_list;

static int __init tegra186_pmc_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, tegra186_pmc);
	if (!np) {
		pr_info("Failed to find t186pmc node\n");
		return -ENODEV;
	}

	if (!of_device_is_available(np)) {
		pr_info("Node %s is not enabled\n", np->name);
		return -ENODEV;
	}

	ret = tegra186_pmc_parse_dt(np);
	if (ret < 0) {
		pr_info("Failed to parse t186pmc DT node:%d\n", ret);
		return ret;
	}

	tegra186_pmc_dev.release = tegra186_pmc_dev_release;
	tegra186_pmc_dev.of_node = np;
	tegra186_pmc_dev.parent = NULL;
	dev_set_name(&tegra186_pmc_dev, "tegra186-pmc");
	ret = device_register(&tegra186_pmc_dev);
	if (ret) {
		put_device(&tegra186_pmc_dev);
		pr_err("ERROR: tegra186-pmc device create failed: %d\n",
			ret);
	} else {
		pr_info("tegra186-pmc device create success\n");
	}

	/* Prod setting like platform specific rails */
	prod_list = tegra_prod_get(&tegra186_pmc_dev, NULL);
	if (IS_ERR(prod_list)) {
		ret = PTR_ERR(prod_list);
		dev_info(&tegra186_pmc_dev, "prod list not found: %d\n",
			ret);
		prod_list = NULL;
	} else {
		ret = tegra_prod_set_by_name(&tegra186_pmc_base,
				"prod_c_platform_pad_rail", prod_list);
		if (ret < 0) {
			dev_info(&tegra186_pmc_dev,
				"prod setting for rail not found\n");
		}
	}

	/* Register as pad controller */
	ret = tegra_pmc_padctrl_init(&tegra186_pmc_dev, np);
	if (ret) {
		pr_err("ERROR: Pad control driver init failed: %d\n",
				ret);
	}

	return 0;
}
postcore_initcall_sync(tegra186_pmc_init);

