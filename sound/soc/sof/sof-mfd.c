// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//
#include <linux/device.h>
#include "sof-mfd.h"

void sof_client_dev_register(struct snd_sof_dev *sdev, const char *name,
			     struct platform_device *pdev)
{
	int ret;

	pdev = platform_device_alloc(name, -1);
	if (!pdev) {
		dev_err(sdev->dev, "error: Failed to allocate %s\n", name);
		return;
	}

	pdev->dev.parent = sdev->dev;
	dev_set_drvdata(&pdev->dev, sdev);
	ret = platform_device_add(pdev);
	if (ret) {
		dev_err(sdev->dev, "error: Failed to register %s: %d\n", name,
			ret);
		platform_device_put(pdev);
		pdev = NULL;
	}
	dev_dbg(sdev->dev, "%s client registered\n", name);
}
EXPORT_SYMBOL(sof_client_dev_register);

void sof_client_dev_unregister(struct platform_device *pdev)
{
	platform_device_del(pdev);
}
EXPORT_SYMBOL(sof_client_dev_unregister);
