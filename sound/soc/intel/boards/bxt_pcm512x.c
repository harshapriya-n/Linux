// SPDX-License-Identifier: GPL-2.0
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
//

/*
 * bxt-pcm512x.c - ASoc Machine driver for Intel platforms
 * with TI PCM512x codec, e.g. Up or Up2 with Hifiberry DAC+ (PRO) HAT
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/platform_sst_audio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/jack.h>
#include "../../codecs/hdac_hdmi.h"
#include "../../codecs/pcm512x.h"
#include "../atom/sst-atom-controls.h"

struct bxt_card_private {
	struct list_head hdmi_pcm_list;
};

static const struct snd_soc_dapm_widget dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Speaker */
	{"Ext Spk", NULL, "OUTR"},
	{"Ext Spk", NULL, "OUTL"},
};

static int codec_fixup(struct snd_soc_pcm_runtime *rtd,
		       struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will covert the FE rate to 48k, stereo */
	rate->min = 48000;
	rate->max = 48000;
	channels->min = 2;
	channels->max = 2;

	/* set SSP5 to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int aif1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x08);

	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static void aif1_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x00);
}

static int init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_EN, 0x08, 0x08);
	snd_soc_component_update_bits(codec, PCM512x_GPIO_OUTPUT_4, 0x0f, 0x02);
	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x08);

	return 0;
}

static const struct snd_soc_ops aif1_ops = {
	.startup = aif1_startup,
	.shutdown = aif1_shutdown,
};

SND_SOC_DAILINK_DEF(ssp5_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP5 Pin")));

SND_SOC_DAILINK_DEF(ssp5_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-104C5122:00", "pcm512x-hifi")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:0e.0")));

static struct snd_soc_dai_link dailink[] = {
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP5-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.init = init,
		.be_hw_params_fixup = codec_fixup,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp5_pin, ssp5_codec, platform),
	},
};

/* SoC card */
static struct snd_soc_card bxt_pcm512x_card = {
	.name = "bxt-pcm512x",
	.owner = THIS_MODULE,
	.dai_link = dailink,
	.num_links = ARRAY_SIZE(dailink),
	.dapm_widgets = dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

 /* i2c-<HID>:00 with HID being 8 chars */
static char codec_name[SND_ACPI_I2C_ID_LEN];

static int bxt_pcm512x_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach;
	struct bxt_card_private *ctx;
	struct acpi_device *adev;
	int dai_index = 0;
	int ret_val = 0;
	int i;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI))
		INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	mach = (&pdev->dev)->platform_data;
	card = &bxt_pcm512x_card;
	card->dev = &pdev->dev;

	/* set platform name for each dailink */
	ret_val = snd_soc_fixup_dai_links_platform_name(card,
							mach->mach_params.platform);
	if (ret_val)
		return ret_val;

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(dailink); i++) {
		if (!strcmp(dailink[i].codecs->name, "i2c-104C5122:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(codec_name, sizeof(codec_name),
			 "%s%s", "i2c-", acpi_dev_name(adev));
		put_device(&adev->dev);
		dailink[dai_index].codecs->name = codec_name;
	}

	snd_soc_card_set_drvdata(card, ctx);

	ret_val = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, card);
	return ret_val;
}

static struct platform_driver bxt_pcm521x_driver = {
	.driver = {
		.name = "bxt-pcm512x",
		.pm = &snd_soc_pm_ops,
	},
	.probe = bxt_pcm512x_probe,
};
module_platform_driver(bxt_pcm521x_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Broxton + PCM512x Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bxt-pcm512x");
