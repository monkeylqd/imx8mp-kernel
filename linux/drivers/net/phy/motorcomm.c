// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Motorcomm PHYs
 *
 * Author: Peter Geis <pgwipeout@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/of.h>

#define PHY_ID_YT8511		0x0000010a
#define PHY_ID_YT8512H		0x00000128
#define PHY_ID_YT8521SH 	0x0000011a

#define YT8511_PAGE_SELECT	0x1e
#define YT8511_PAGE		0x1f
#define YT8511_EXT_CLK_GATE	0x0c
#define YT8511_EXT_DELAY_DRIVE	0x0d
#define YT8511_EXT_SLEEP_CTRL	0x27

#define YT8512H_EXT_REG_ADDR_OFFSET_REG 	0x1E
#define YT8512H_EXT_REG_DATA_REG		0x1F
#define YT8512H_EXT_LED1_CFG			0x40C3
#define YT8512H_EXT_LED0_CFG			0x40C0

#define YT8521SH_EXT_REG_ADDR_OFFSET_REG	0x1E
#define YT8521SH_EXT_REG_DATA_REG		0x1F
#define YT8521SH_EXT_LED2_CFG			0xA00E
#define YT8521SH_EXT_LED1_CFG			0xA00D
#define YT8521SH_EXT_COMMON_RGMII_CFG1		0xA003
#define YT8521SH_EXT_REG_TX_OFFSET		0
#define YT8521SH_EXT_REG_RX_OFFSET		10

/* 2b00 25m from pll
 * 2b01 25m from xtl *default*
 * 2b10 62.m from pll
 * 2b11 125m from pll
 */
#define YT8511_CLK_125M		(BIT(2) | BIT(1))
#define YT8511_PLLON_SLP	BIT(14)

/* RX Delay enabled = 1.8ns 1000T, 8ns 10/100T */
#define YT8511_DELAY_RX		BIT(0)

/* TX Gig-E Delay is bits 7:4, default 0x5
 * TX Fast-E Delay is bits 15:12, default 0xf
 * Delay = 150ps * N - 250ps
 * On = 2000ps, off = 50ps
 */
#define YT8511_DELAY_GE_TX_EN	(0xf << 4)
#define YT8511_DELAY_GE_TX_DIS	(0x2 << 4)
#define YT8511_DELAY_FE_TX_EN	(0xf << 12)
#define YT8511_DELAY_FE_TX_DIS	(0x2 << 12)

static int yt8511_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, YT8511_PAGE_SELECT);
};

static int yt8511_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, YT8511_PAGE_SELECT, page);
};

static int yt8511_config_init(struct phy_device *phydev)
{
	int oldpage, ret = 0;
	unsigned int ge, fe;

	oldpage = phy_select_page(phydev, YT8511_EXT_CLK_GATE);
	if (oldpage < 0)
		goto err_restore_page;

	/* set rgmii delay mode */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		ge = YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_DIS;
		fe = YT8511_DELAY_FE_TX_DIS;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		ge = YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		ge = YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN;
		fe = YT8511_DELAY_FE_TX_EN;
		break;
	default: /* do not support other modes */
		ret = -EOPNOTSUPP;
		goto err_restore_page;
	}

	ret = __phy_modify(phydev, YT8511_PAGE, (YT8511_DELAY_RX | YT8511_DELAY_GE_TX_EN), ge);
	if (ret < 0)
		goto err_restore_page;

	/* set clock mode to 125mhz */
	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_CLK_125M);
	if (ret < 0)
		goto err_restore_page;

	/* fast ethernet delay is in a separate page */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_DELAY_DRIVE);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, YT8511_DELAY_FE_TX_EN, fe);
	if (ret < 0)
		goto err_restore_page;

	/* leave pll enabled in sleep */
	ret = __phy_write(phydev, YT8511_PAGE_SELECT, YT8511_EXT_SLEEP_CTRL);
	if (ret < 0)
		goto err_restore_page;

	ret = __phy_modify(phydev, YT8511_PAGE, 0, YT8511_PLLON_SLP);
	if (ret < 0)
		goto err_restore_page;

err_restore_page:
	return phy_restore_page(phydev, oldpage, ret);
}

static int yt8512h_config_init(struct phy_device *phydev)
{
	int value;

	/* Reset phy */
	__phy_write(phydev, MII_BMCR, BMCR_RESET);
	while (BMCR_RESET & __phy_read(phydev, MII_BMCR))
		msleep(30);

	value = __phy_read(phydev, MII_BMCR);
	__phy_write(phydev, MII_BMCR, (value & ~BMCR_PDOWN));
	msleep(50);

	/* Config LED1 to ON when phy link up */
	__phy_write(phydev, YT8512H_EXT_REG_ADDR_OFFSET_REG, YT8512H_EXT_LED1_CFG);
	msleep(50);
	__phy_write(phydev, YT8512H_EXT_REG_DATA_REG, 0x0030);
	msleep(50);

	/* Config LED0 to BLINK when phy link up and tx rx active */
	__phy_write(phydev, YT8512H_EXT_REG_ADDR_OFFSET_REG, YT8512H_EXT_LED0_CFG);
	msleep(50);
	__phy_write(phydev, YT8512H_EXT_REG_DATA_REG, 0x0330);

	return 0;
}

static int yt8521sh_config_init(struct phy_device *phydev)
{
	struct device_node *of_node = phydev->mdio.dev.of_node;
	int value;
	u16 phy_tx_delay = 0;
	u16 phy_rx_delay = 0;

	/* Reset phy */
	__phy_write(phydev, MII_BMCR, BMCR_RESET);
	while (BMCR_RESET & __phy_read(phydev, MII_BMCR))
		msleep(30);

	value = __phy_read(phydev, MII_BMCR);
	__phy_write(phydev, MII_BMCR, (value & ~BMCR_PDOWN));
	msleep(50);

	/* Config LED2 to ON when phy link up */
	__phy_write(phydev, YT8521SH_EXT_REG_ADDR_OFFSET_REG, YT8521SH_EXT_LED2_CFG);
	msleep(50);
	__phy_write(phydev, YT8521SH_EXT_REG_DATA_REG, 0x0070);

	/* Config LED1 to BLINK when phy link up and tx rx active */
	__phy_write(phydev, YT8521SH_EXT_REG_ADDR_OFFSET_REG, YT8521SH_EXT_LED1_CFG);
	msleep(50);
	__phy_write(phydev, YT8521SH_EXT_REG_DATA_REG, 0x0670);

	/* Get phy tx/rx delay vlaues from devicetree */
	if(!of_property_read_u32(of_node, "phy-tx-delay", &value))
		phy_tx_delay = value;
	if(!of_property_read_u32(of_node, "phy-rx-delay", &value))
		phy_rx_delay = value;
	/* Read rgmii cfg1 */
	__phy_write(phydev, YT8521SH_EXT_REG_ADDR_OFFSET_REG, YT8521SH_EXT_COMMON_RGMII_CFG1);
	msleep(50);
	value = __phy_read(phydev, YT8521SH_EXT_REG_DATA_REG);
	msleep(50);
	value &= ~(0xf << YT8521SH_EXT_REG_TX_OFFSET);
	value |= ((phy_tx_delay & 0xf) << YT8521SH_EXT_REG_TX_OFFSET);
	value &= ~(0xf << YT8521SH_EXT_REG_RX_OFFSET);
	value |= ((phy_rx_delay & 0xf) << YT8521SH_EXT_REG_RX_OFFSET);
	/* Config phy tx/rx delay */
	__phy_write(phydev, YT8521SH_EXT_REG_ADDR_OFFSET_REG, YT8521SH_EXT_COMMON_RGMII_CFG1);
	msleep(50);
	__phy_write(phydev, YT8521SH_EXT_REG_DATA_REG, value);

	return 0;
}

static struct phy_driver motorcomm_phy_drvs[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8511),
		.name		= "YT8511 Gigabit Ethernet",
		.config_init	= yt8511_config_init,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= yt8511_read_page,
		.write_page	= yt8511_write_page,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8512H),
		.name			= "YT8512H Ethernet",
		.config_init		= &yt8512h_config_init,
		.config_aneg    	= &genphy_config_aneg,
		.read_status    	= &genphy_read_status,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_YT8521SH),
		.name			= "YT8521SH Gigabit Ethernet",
		.config_init		= &yt8521sh_config_init,
		.config_aneg		= &genphy_config_aneg,
		.read_status		= &genphy_read_status,
	},
};

module_phy_driver(motorcomm_phy_drvs);

MODULE_DESCRIPTION("Motorcomm PHY driver");
MODULE_AUTHOR("Peter Geis");
MODULE_LICENSE("GPL");

static const struct mdio_device_id __maybe_unused motorcomm_tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8511) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8512H) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_YT8521SH) },
	{ /* sentinal */ }
};

MODULE_DEVICE_TABLE(mdio, motorcomm_tbl);
