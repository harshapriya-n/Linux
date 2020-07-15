// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "sof-client.h"
#include "sof-priv.h"

static void sof_client_ancildev_release(struct device *dev)
{
	struct ancillary_device *ancildev = to_ancillary_dev(dev);
	struct sof_client_dev *cdev = ancillary_dev_to_sof_client_dev(ancildev);

	ida_simple_remove(cdev->client_ida, ancildev->id);
	kfree(cdev);
}

static struct sof_client_dev *sof_client_dev_alloc(struct snd_sof_dev *sdev, const char *name,
						   struct ida *client_ida)
{
	struct sof_client_dev *cdev;
	struct ancillary_device *ancildev;
	int ret;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return NULL;

	cdev->sdev = sdev;
	cdev->client_ida = client_ida;
	ancildev = &cdev->ancildev;
	ancildev->name = name;
	ancildev->dev.parent = sdev->dev;
	ancildev->dev.release = sof_client_ancildev_release;

	ancildev->id = ida_alloc(client_ida, GFP_KERNEL);
	if (ancildev->id < 0) {
		dev_err(sdev->dev, "error: get IDA idx for ancillary device %s failed\n", name);
		ret = ancildev->id;
		goto err_free;
	}

	ret = ancillary_device_initialize(ancildev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to initialize client dev %s\n", name);
		ida_simple_remove(client_ida, ancildev->id);
		goto err_free;
	}

	return cdev;

err_free:
	kfree(cdev);
	return NULL;
}

int sof_client_dev_register(struct snd_sof_dev *sdev, const char *name, struct ida *client_ida)
{
	struct sof_client_dev *cdev;
	int ret;

	cdev = sof_client_dev_alloc(sdev, name, client_ida);
	if (!cdev)
		return -ENODEV;

	ret = ancillary_device_add(&cdev->ancildev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to add client dev %s\n", name);
		put_device(&cdev->ancildev.dev);
	}

	/* add to list of SOF client devices */
	mutex_lock(&sdev->client_mutex);
	list_add(&cdev->list, &sdev->client_list);
	mutex_unlock(&sdev->client_mutex);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(sof_client_dev_register, SND_SOC_SOF_CLIENT);

void sof_client_dev_unregister(struct sof_client_dev *cdev)
{
	struct snd_sof_dev *sdev = cdev->sdev;

	/* remove from list of SOF client devices */
	mutex_lock(&sdev->client_mutex);
	list_del(&cdev->list);
	mutex_unlock(&sdev->client_mutex);

	ancillary_device_unregister(&cdev->ancildev);
}
EXPORT_SYMBOL_NS_GPL(sof_client_dev_unregister, SND_SOC_SOF_CLIENT);

int sof_client_ipc_tx_message(struct sof_client_dev *cdev, u32 header, void *msg_data,
			      size_t msg_bytes, void *reply_data, size_t reply_bytes)
{
	return sof_ipc_tx_message(cdev->sdev->ipc, header, msg_data, msg_bytes,
				  reply_data, reply_bytes);
}
EXPORT_SYMBOL_NS_GPL(sof_client_ipc_tx_message, SND_SOC_SOF_CLIENT);

struct dentry *sof_client_get_debugfs_root(struct sof_client_dev *cdev)
{
	return cdev->sdev->debugfs_root;
}
EXPORT_SYMBOL_NS_GPL(sof_client_get_debugfs_root, SND_SOC_SOF_CLIENT);
