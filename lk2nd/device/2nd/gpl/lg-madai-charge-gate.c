/* SPDX-License-Identifier: GPL-2.0-only */
/* Opt-in charge-only inhibition before native sensor dependencies can defer. */
#include <boot.h>
#include <debug.h>
#include <libfdt.h>
#include <malloc.h>
#include <pm8x41.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MADAI_CHARGE_CONTROL "qcom,software-charge-control"
#define MADAI_CHARGE_HANDOFF "qcom,boot-charge-inhibited"
#define MADAI_CHG_CTRL 0x1049
#define MADAI_CHG_ENABLE 0x80

static int madai_charge_write_off(void)
{
	uint8_t value, verify;

	if (spmi_reg_read(0, MADAI_CHG_CTRL, &value, 0))
		return -FDT_ERR_INTERNAL;
	value &= ~MADAI_CHG_ENABLE;
	if (spmi_reg_write(0, MADAI_CHG_CTRL, &value, 0))
		return -FDT_ERR_INTERNAL;
	if (spmi_reg_read(0, MADAI_CHG_CTRL, &verify, 0))
		return -FDT_ERR_INTERNAL;
	return verify == value ? 0 : -FDT_ERR_INTERNAL;
}

static int madai_charge_inhibit(void)
{
	uint8_t type, subtype;
	int ret, recovery;

	/* Match the actual PM8226 on SID0 before changing its charger control. */
	if (spmi_reg_read(0, 0x104, &type, 0) ||
	    spmi_reg_read(0, 0x105, &subtype, 0))
		return -FDT_ERR_INTERNAL;
	if (type != 0x51 || subtype != 0x04)
		return -FDT_ERR_BADVALUE;

	ret = madai_charge_write_off();
	if (ret) {
		/* A failed write may have reached hardware. Keep charging off. */
		recovery = madai_charge_write_off();
		dprintf(CRITICAL, "Fx0 charge gate failed; inhibit retry: %d\n",
			recovery);
	}
	return ret;
}

static int madai_charge_node(const void *dtb)
{
	const fdt32_t *reg;
	const void *property;
	int node = -1, selected = 0, count = 0, parent, length, ret;

	while ((node = fdt_node_offset_by_compatible(dtb, node,
						  "qcom,pm8226-charger")) >= 0) {
		count++;
		property = fdt_getprop(dtb, node, MADAI_CHARGE_CONTROL, &length);
		if (!property) {
			if (length != -FDT_ERR_NOTFOUND)
				return length;
			continue;
		}
		if (!node || length || selected)
			return -FDT_ERR_BADVALUE;
		selected = node;
	}
	if (node != -FDT_ERR_NOTFOUND)
		return node;
	if (!selected)
		return 0;
	if (count != 1)
		return -FDT_ERR_BADVALUE;
	node = selected;
	if (fdt_getprop(dtb, node, MADAI_CHARGE_HANDOFF, NULL))
		return -FDT_ERR_EXISTS;
	reg = fdt_getprop(dtb, node, "reg", &length);
	if (!reg || length != 4 || fdt32_to_cpu(reg[0]) != 0x1000)
		return -FDT_ERR_BADVALUE;
	parent = fdt_parent_offset(dtb, node);
	if (parent < 0)
		return parent;
	ret = fdt_node_check_compatible(dtb, parent, "qcom,pm8226");
	if (ret)
		return ret < 0 ? ret : -FDT_ERR_BADVALUE;
	reg = fdt_getprop(dtb, parent, "reg", &length);
	if (!reg || length != 8 || fdt32_to_cpu(reg[0]) || fdt32_to_cpu(reg[1]))
		return -FDT_ERR_BADVALUE;
	return node;
}

static int madai_charge_gate_update_dt(void *dtb, const char *cmdline,
				     enum boot_type boot_type)
{
	void *copy;
	int node, ret, size;

	(void)cmdline;
	if (boot_type & (BOOT_DOWNSTREAM | BOOT_LK2ND))
		return 0;
	ret = fdt_check_header(dtb);
	if (ret)
		return ret;
	ret = fdt_node_check_compatible(dtb, 0, "lg,madai");
	if (ret)
		return ret < 0 ? ret : 0;
	node = madai_charge_node(dtb);
	if (node == -FDT_ERR_NOTFOUND || !node)
		return 0;
	if (node < 0)
		return node;
	size = fdt_totalsize(dtb);
	if (size <= 0)
		return -FDT_ERR_BADVALUE;
	copy = memalign(8, size);
	if (!copy)
		return -FDT_ERR_NOSPACE;
	ret = fdt_open_into(dtb, copy, size);
	if (!ret) {
		node = madai_charge_node(copy);
		ret = node > 0 ? fdt_setprop(copy, node, MADAI_CHARGE_HANDOFF, NULL, 0) :
			node < 0 ? node : -FDT_ERR_BADVALUE;
	}
	/* Finish every fallible FDT operation before touching charger hardware. */
	if (!ret)
		ret = madai_charge_inhibit();
	if (!ret) {
		memcpy(dtb, copy, size);
		dprintf(INFO, "Fx0 charge gate: verified inhibit handed to Linux\n");
	}
	free(copy);
	return ret;
}

DEV_TREE_UPDATE(madai_charge_gate_update_dt);
