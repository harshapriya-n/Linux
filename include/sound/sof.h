/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __INCLUDE_SOUND_SOF_H
#define __INCLUDE_SOUND_SOF_H

#include <linux/pci.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-of.h>

struct snd_sof_dsp_ops;

enum {
	SND_SOC_SOF_MACH_TYPE_ACPI = 0,
	SND_SOC_SOF_MACH_TYPE_OF,
};

struct snd_soc_sof_mach {
	int type;
	char *tplg_filename;
	const char *tplg_filename_prefix;
	struct platform_device *pdev_mach;
	union {
		const struct snd_soc_acpi_mach *acpi;
		const struct snd_soc_of_mach *of;
	};
};

static inline
const char *sof_mach_get_drv_name(const struct snd_soc_sof_mach *mach)
{
	switch (mach->type) {
	case SND_SOC_SOF_MACH_TYPE_ACPI:
		return mach->acpi->drv_name;
	case SND_SOC_SOF_MACH_TYPE_OF:
		return mach->of->drv_name;
	default:
		return NULL;
	}
}

static inline
const void *sof_mach_get_machine(const struct snd_soc_sof_mach *mach)
{
	switch (mach->type) {
	case SND_SOC_SOF_MACH_TYPE_ACPI:
		return mach->acpi;
	case SND_SOC_SOF_MACH_TYPE_OF:
		return mach->of;
	default:
		return NULL;
	}
}

static inline void sof_mach_set_machine(struct snd_soc_sof_mach *mach,
					const void *machine)
{
	switch (mach->type) {
	case SND_SOC_SOF_MACH_TYPE_ACPI:
		mach->acpi = machine;
		break;
	case SND_SOC_SOF_MACH_TYPE_OF:
		mach->of = machine;
		break;
	default:
		return;
	}
}

static inline int sof_mach_get_mach_size(const struct snd_soc_sof_mach *mach)
{
	switch (mach->type) {
	case SND_SOC_SOF_MACH_TYPE_ACPI:
		return sizeof(*mach->acpi);
	case SND_SOC_SOF_MACH_TYPE_OF:
		return sizeof(*mach->of);
	default:
		return -EINVAL;
	}
}

/*
 * SOF Platform data.
 */
struct snd_sof_pdata {
	const struct firmware *fw;
	const char *name;

	struct device *dev;

	/*
	 * notification callback used if the hardware initialization
	 * can take time or is handled in a workqueue. This callback
	 * can be used by the caller to e.g. enable runtime_pm
	 * or limit functionality until all low-level inits are
	 * complete.
	 */
	void (*sof_probe_complete)(struct device *dev);

	/* descriptor */
	const struct sof_dev_desc *desc;

	/* firmware filename */
	const char *fw_filename_prefix;
	const char *fw_filename;

	void *hw_pdata;
};

/*
 * Descriptor used for setting up SOF platform data. This is used when
 * ACPI/PCI data is missing or mapped differently.
 */
struct sof_dev_desc {
	/* list of machines using this configuration */
	struct snd_soc_acpi_mach *machines;

	/* alternate list of machines using this configuration */
	struct snd_soc_acpi_mach *alt_machines;

	/* Platform resource indexes in BAR / ACPI resources. */
	/* Must set to -1 if not used - add new items to end */
	int resindex_lpe_base;
	int resindex_pcicfg_base;
	int resindex_imr_base;
	int irqindex_host_ipc;
	int resindex_dma_base;

	/* DMA only valid when resindex_dma_base != -1*/
	int dma_engine;
	int dma_size;

	/* IPC timeouts in ms */
	int ipc_timeout;
	int boot_timeout;

	/* chip information for dsp */
	const void *chip_info;

	/* defaults for no codec mode */
	const char *nocodec_tplg_filename;

	/* defaults paths for firmware and topology files */
	const char *default_fw_path;
	const char *default_tplg_path;

	/* default firmware name */
	const char *default_fw_filename;

	const struct snd_sof_dsp_ops *ops;
	const struct sof_arch_ops *arch_ops;
};

static inline
void sof_set_mach_type(struct snd_soc_sof_mach *mach,
		       const struct sof_dev_desc *desc)
{
	if (desc->machines)
		mach->type = SND_SOC_SOF_MACH_TYPE_ACPI;
	else
		mach->type = SND_SOC_SOF_MACH_TYPE_OF;
}
#endif
