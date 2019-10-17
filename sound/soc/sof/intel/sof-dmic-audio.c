// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <sound/sof.h>
#include "../sof-priv.h"
#include "../sof-mfd.h"
#include "../sof-audio.h"
#include "../audio-ops.h"
#include "../ops.h"
#include "hda.h"
#include "intel-audio.h"

/*
 * SOF Driver enumeration.
 */
static int sof_audio_select_machine(struct platform_device *pdev,
				    const struct sof_dev_desc *desc)
{
	struct sof_mfd_client *audio_client = dev_get_platdata(&pdev->dev);
	struct sof_audio_dev *sof_audio = audio_client->client_data;
	struct snd_soc_acpi_mach *mach;
	int ret;

	mach = devm_kzalloc(&pdev->dev, sizeof(*mach), GFP_KERNEL);
	if (!mach)
		return -ENOMEM;

	/* use generic dmic machine driver */
	ret = sof_dmic_setup(&pdev->dev, sof_audio, mach, desc->chip_info);
	if (ret < 0) {
		dev_warn(&pdev->dev,
			 "error: sof dmic set up failed %d\n", ret);
		return ret;
	}

	sof_audio->machine = mach;
	sof_audio->tplg_filename_prefix = desc->default_tplg_path;

	return ret;
}

static int sof_dmic_audio_probe(struct platform_device *pdev)
{
	struct sof_mfd_client *audio_client = dev_get_platdata(&pdev->dev);
	struct snd_sof_dev *sdev = dev_get_drvdata(pdev->dev.parent);
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const struct sof_intel_dsp_desc *chip = get_chip_info(plat_data);
	const struct sof_dev_desc *desc = plat_data->desc;
	struct snd_soc_dai_driver *dai_drv;
	struct sof_audio_dev *sof_audio;
	const char *drv_name;
	const void *machine;
	int dai_offset;
	int num_drv;
	int size;
	int ret;

	/* set IPC RX and TX reply callbacks */
	audio_client->sof_client_rx_message = sof_audio_rx_message;
	audio_client->sof_ipc_reply_cb = NULL;

	/* create SOF audio device */
	sof_audio = devm_kzalloc(&pdev->dev, sizeof(*sof_audio), GFP_KERNEL);
	if (!sof_audio)
		return -ENOMEM;

	INIT_LIST_HEAD(&sof_audio->pcm_list);
	INIT_LIST_HEAD(&sof_audio->kcontrol_list);
	INIT_LIST_HEAD(&sof_audio->widget_list);
	INIT_LIST_HEAD(&sof_audio->dai_list);
	INIT_LIST_HEAD(&sof_audio->route_list);

	sof_audio->audio_ops = desc->audio_ops;
	sof_audio->platform = dev_name(&pdev->dev);

	/* check for mandatory audio ops */
	if (!sof_audio || !sof_audio->audio_ops->ipc_pcm_params)
		return -EINVAL;

	audio_client->client_data = sof_audio;

	/* select machine driver */
	ret = sof_audio_select_machine(pdev, desc);
	if (ret < 0)
		return ret;

	/* set up platform component driver */
	snd_sof_new_platform_drv(sof_audio);

	/* now register audio DSP platform driver and dai */
	dai_offset = chip->num_ssp_drv;
	dai_drv = &sof_audio->audio_ops->drv[dai_offset];
	num_drv = chip->num_dmic_drv;
	ret = devm_snd_soc_register_component(&pdev->dev, &sof_audio->plat_drv,
					      dai_drv, num_drv);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"error: failed to register DSP DMIC DAI driver %d\n",
			ret);
		return ret;
	}

	machine = (const void *)sof_audio->machine;
	drv_name = sof_audio->machine->drv_name;
	size = sizeof(*sof_audio->machine);

	/* register machine driver, pass machine info as pdata */
	sof_audio->pdev_mach =
		platform_device_register_data(&pdev->dev, drv_name,
					      PLATFORM_DEVID_NONE,
					      machine, size);

	if (IS_ERR(sof_audio->pdev_mach)) {
		ret = PTR_ERR(sof_audio->pdev_mach);
		return ret;
	}

	dev_dbg(&pdev->dev, "created machine %s\n",
		dev_name(&sof_audio->pdev_mach->dev));

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(&pdev->dev, SND_SOF_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

static int sof_dmic_audio_remove(struct platform_device *pdev)
{
	struct sof_audio_dev *sof_audio = sof_mfd_get_client_data(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	if (!IS_ERR_OR_NULL(sof_audio->pdev_mach))
		platform_device_unregister(sof_audio->pdev_mach);

	return 0;
}

const struct dev_pm_ops sof_audio_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(sof_audio_suspend, sof_audio_resume)
	SET_RUNTIME_PM_OPS(sof_audio_runtime_suspend, sof_audio_resume, NULL)
};

static struct platform_driver sof_dmic_audio_driver = {
	.driver = {
		.name = "sof-dmic-audio",
		.pm = &sof_audio_pm,
	},

	.probe = sof_dmic_audio_probe,
	.remove = sof_dmic_audio_remove,
};

module_platform_driver(sof_dmic_audio_driver);

MODULE_DESCRIPTION("SOF DMIC Audio Client Platform Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:sof-dmic-audio");
