// SPDX-License-Identifier: GPL-2.0-only
/*
 * LG Fx0 battery data selected by the first-stage bootloader.
 * OCV data: peremen/android-kernel-lg-madai, commit
 * 03aba4427a7666d57b5ae06d375e259ea337f351,
 * arch/arm/mach-msm/lge/bms-batterydata-LGE_BL_59UH_2440mAh_TOCAD_MADAI_Final.c.
 */

#include <boot.h>
#include <debug.h>
#include <libfdt.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../device.h"
#include "lg-madai-battery-data.h"

static bool madai_known_battery;

static bool madai_stock_battery(const char *cmdline)
{
	const char *key = "lge.battid=";
	const char *value = "ISL6296_L";
	const char *arg, *end;
	bool found = false, match = false;
	size_t len;

	if (!cmdline)
		return false;

	for (arg = cmdline; *arg; arg = end) {
		while (*arg == ' ')
			arg++;
		if (!*arg)
			break;
		end = strchr(arg, ' ');
		if (!end)
			end = arg + strlen(arg);
		len = end - arg;
		if (len < strlen(key) || strncmp(arg, key, strlen(key)))
			continue;
		/* Duplicate, unknown or truncated values must not select a pack. */
		if (found)
			return false;
		found = true;
		match = len == strlen(key) + strlen(value) &&
			!memcmp(arg + strlen(key), value, strlen(value));
	}
	return match;
}

static int madai_battery_detect(const void *dtb, int node)
{
	(void)dtb;
	(void)node;
	madai_known_battery = madai_stock_battery(lk2nd_dev.cmdline);
	if (madai_known_battery)
		lk2nd_dev.battery = "ISL6296_L (TOCAD Madai)";
	return 0;
}

LK2ND_DEVICE_INIT("lg,madai-battery", madai_battery_detect);

static int madai_find_consumer(const void *dtb, const char *compatible)
{
	int node, next;

	node = fdt_node_offset_by_compatible(dtb, -1, compatible);
	if (node < 0)
		return node;
	next = fdt_node_offset_by_compatible(dtb, node, compatible);
	if (next != -FDT_ERR_NOTFOUND)
		return next < 0 ? next : -FDT_ERR_BADVALUE;
	if (fdt_getprop(dtb, node, "monitored-battery", NULL))
		return -FDT_ERR_EXISTS;
	return node;
}

static int madai_battery_properties(void *dtb, int node, uint32_t phandle)
{
	fdt32_t temperatures[5], table[31 * 2];
	char name[32];
	int ret, column, row;

	ret = fdt_setprop_string(dtb, node, "compatible", "simple-battery");
	if (ret)
		return ret;
	ret = fdt_setprop_u32(dtb, node, "phandle", phandle);
	if (ret)
		return ret;
	ret = fdt_setprop_string(dtb, node, "device-chemistry", "lithium-ion");
	if (ret)
		return ret;
	/* KDDI's rated capacity, distinct from vendor nominal/measured FCC. */
	ret = fdt_setprop_u32(dtb, node, "charge-full-design-microamp-hours", 2370000);
	if (ret)
		return ret;
	ret = fdt_setprop_u32(dtb, node, "constant-charge-voltage-max-microvolt", 4350000);
	if (ret)
		return ret;
	ret = fdt_setprop_u32(dtb, node, "voltage-min-design-microvolt", 3400000);
	if (ret)
		return ret;
	for (column = 0; column < 5; column++)
		temperatures[column] = cpu_to_fdt32((uint32_t)madai_ocv_temperatures[column]);
	ret = fdt_setprop(dtb, node, "ocv-capacity-celsius", temperatures,
			  sizeof(temperatures));
	if (ret)
		return ret;
	for (column = 0; column < 5; column++) {
		for (row = 0; row < 31; row++) {
			table[2 * row] = cpu_to_fdt32(madai_ocv_mv[row][column] * 1000);
			table[2 * row + 1] = cpu_to_fdt32(madai_ocv_percent[row]);
		}
		snprintf(name, sizeof(name), "ocv-capacity-table-%d", column);
		ret = fdt_setprop(dtb, node, name, table, sizeof(table));
		if (ret)
			return ret;
	}
	return 0;
}

static int madai_battery_apply(void *dtb)
{
	static const char *const consumers[] = {
		"qcom,pm8226-charger", "qcom,pm8226-bms",
	};
	uint32_t phandle;
	int node, ret, i;

	/* A supplied profile is a conflict, not permission to overwrite it. */
	node = fdt_subnode_offset(dtb, 0, "battery");
	if (node != -FDT_ERR_NOTFOUND)
		return node < 0 ? node : -FDT_ERR_EXISTS;
	for (i = 0; i < 2; i++) {
		ret = madai_find_consumer(dtb, consumers[i]);
		if (ret < 0 && !(i && ret == -FDT_ERR_NOTFOUND))
			return ret;
	}
	node = fdt_add_subnode(dtb, 0, "battery");
	if (node < 0)
		return node;
	ret = fdt_generate_phandle(dtb, &phandle);
	if (ret)
		return ret;
	ret = madai_battery_properties(dtb, node, phandle);
	if (ret)
		return ret;
	for (i = 0; i < 2; i++) {
		/* Inserting the battery and its properties moves consumer offsets. */
		node = madai_find_consumer(dtb, consumers[i]);
		if (i && node == -FDT_ERR_NOTFOUND)
			continue;
		if (node < 0)
			return node;
		ret = fdt_setprop_u32(dtb, node, "monitored-battery", phandle);
		if (ret)
			return ret;
	}
	return 0;
}

static int madai_battery_update_dt(void *dtb, const char *cmdline,
				   enum boot_type boot_type)
{
	void *copy;
	int ret, size;

	(void)cmdline;
	if (!madai_known_battery || boot_type & (BOOT_DOWNSTREAM | BOOT_LK2ND))
		return 0;
	ret = fdt_check_header(dtb);
	if (ret)
		return ret;
	ret = fdt_node_check_compatible(dtb, 0, "lg,madai");
	if (ret)
		return ret < 0 ? ret : 0;
	size = fdt_totalsize(dtb);
	if (size <= 0)
		return -FDT_ERR_BADVALUE;
	copy = malloc(size);
	if (!copy)
		return -FDT_ERR_NOSPACE;
	ret = fdt_open_into(dtb, copy, size);
	if (!ret)
		ret = madai_battery_apply(copy);
	/* Publish the profile and all references together, or change nothing. */
	if (!ret)
		memcpy(dtb, copy, size);
	free(copy);
	return ret;
}

DEV_TREE_UPDATE(madai_battery_update_dt);
