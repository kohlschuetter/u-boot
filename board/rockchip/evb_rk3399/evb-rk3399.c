// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <misc.h>
#include <env.h>
#include <u-boot/crc.h>
#include <hash.h>
#include <init.h>
#include <log.h>
#include <asm/arch-rockchip/periph.h>
#include <asm/setup.h>
#include <u-boot/sha256.h>
#include <power/regulator.h>

#define RK3399_CPUID_OFF  0x7
#define RK3399_CPUID_LEN  0x10

#ifndef CONFIG_SPL_BUILD
int board_early_init_f(void)
{
	struct udevice *regulator;
	int ret;

	ret = regulator_get_by_platname("vcc5v0_host", &regulator);
	if (ret) {
		debug("%s vcc5v0_host init fail! ret %d\n", __func__, ret);
		goto out;
	}

	ret = regulator_set_enable(regulator, true);
	if (ret)
		debug("%s vcc5v0-host-en set fail! ret %d\n", __func__, ret);

out:
	return 0;
}
#endif
static void setup_macaddr(void)
{
	if (!IS_ENABLED(CONFIG_CMD_NET))
		return;

	int ret;
	const char *cpuid = env_get("cpuid#");
	u8 hash[SHA256_SUM_LEN];
	int size = sizeof(hash);
	u8 mac_addr[6];

	/* Only generate a MAC address if none is set in the environment */
	if (env_get("ethaddr"))
		return;

	if (!cpuid) {
		debug("%s: could not retrieve 'cpuid#'\n", __func__);
		return;
	}

	ret = hash_block("sha256", (void *)cpuid, strlen(cpuid), hash, &size);
	if (ret) {
		debug("%s: failed to calculate SHA256\n", __func__);
		return;
	}

	/* Copy 6 bytes of the hash to base the MAC address on */
	memcpy(mac_addr, hash, 6);

	/* Make this a valid MAC address and set it */
	mac_addr[0] &= 0xfe;  /* clear multicast bit */
	mac_addr[0] |= 0x02;  /* set local assignment bit (IEEE802) */
	eth_env_set_enetaddr("ethaddr", mac_addr);
}

static void setup_serial(void)
{
	if (!IS_ENABLED(CONFIG_ROCKCHIP_EFUSE))
		return;

	struct udevice *dev;
	int ret, i;
	u8 cpuid[RK3399_CPUID_LEN];
	u8 low[RK3399_CPUID_LEN / 2], high[RK3399_CPUID_LEN / 2];
	char cpuid_str[RK3399_CPUID_LEN * 2 + 1];
	u64 serialno;
	char serialno_str[16];

	/* retrieve the device */
	ret = uclass_get_device_by_driver(UCLASS_MISC,
					  DM_DRIVER_GET(rockchip_efuse), &dev);
	if (ret) {
		debug("%s: could not find efuse device\n", __func__);
		return;
	}

	/* read the cpu_id range from the efuses */
	ret = misc_read(dev, RK3399_CPUID_OFF, &cpuid, sizeof(cpuid));
	if (ret) {
		debug("%s: reading cpuid from the efuses failed\n",
		      __func__);
		return;
	}

	memset(cpuid_str, 0, sizeof(cpuid_str));
	for (i = 0; i < 16; i++)
		sprintf(&cpuid_str[i * 2], "%02x", cpuid[i]);

	debug("cpuid: %s\n", cpuid_str);

	/*
	 * Mix the cpuid bytes using the same rules as in
	 *   ${linux}/drivers/soc/rockchip/rockchip-cpuinfo.c
	 */
	for (i = 0; i < 8; i++) {
		low[i] = cpuid[1 + (i << 1)];
		high[i] = cpuid[i << 1];
	}

	serialno = crc32_no_comp(0, low, 8);
	serialno |= (u64)crc32_no_comp(serialno, high, 8) << 32;
	snprintf(serialno_str, sizeof(serialno_str), "%llx", serialno);

	env_set("cpuid#", cpuid_str);
	env_set("serial#", serialno_str);
}

int misc_init_r(void)
{
	setup_serial();
	setup_macaddr();

	return 0;
}

void get_board_serial(struct tag_serialnr *serialnr)
{
	u64 serial = 0;

	if (IS_ENABLED(CONFIG_SERIAL_TAG)) {
		char *serial_string;

		serial_string = env_get("serial#");

		if (serial_string)
			serial = simple_strtoull(serial_string, NULL, 16);
	}

	serialnr->high = (u32)(serial >> 32);
	serialnr->low = (u32)(serial & 0xffffffff);
}
