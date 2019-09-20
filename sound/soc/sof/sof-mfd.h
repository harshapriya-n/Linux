/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
 */
#include <linux/platform_device.h>
#include "sof-priv.h"

#ifndef __SOUND_SOC_SOF_MFD_H
#define __SOUND_SOC_SOF_MFD_H

/* client register/unregister */
void sof_client_dev_register(struct snd_sof_dev *sdev, const char *name,
			     struct platform_device *pdev);
void sof_client_dev_unregister(struct platform_device *pdev);

#endif
