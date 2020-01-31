// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "sof-client.h"

#define MAX_IPC_FLOOD_DURATION_MS 1000
#define MAX_IPC_FLOOD_COUNT 10000
#define IPC_FLOOD_TEST_RESULT_LEN 512
#define SOF_IPC_CLIENT_SUSPEND_DELAY_MS 3000

struct sof_ipc_client_data {
	struct dentry *dfs_root;
	char *buf;
};

static int sof_debug_ipc_flood_test(struct device *dev,
				    bool flood_duration_test,
				    unsigned long ipc_duration_ms,
				    unsigned long ipc_count)
{
	struct sof_ipc_client_data *ipc_client_data = sof_get_client_data(dev);
	struct sof_ipc_cmd_hdr hdr;
	struct sof_ipc_reply reply;
	u64 min_response_time = U64_MAX;
	ktime_t start, end, test_end;
	u64 avg_response_time = 0;
	u64 max_response_time = 0;
	u64 ipc_response_time;
	int i = 0;
	int ret;

	/* configure test IPC */
	hdr.cmd = SOF_IPC_GLB_TEST_MSG | SOF_IPC_TEST_IPC_FLOOD;
	hdr.size = sizeof(hdr);

	/* set test end time for duration flood test */
	if (flood_duration_test)
		test_end = ktime_get_ns() + ipc_duration_ms * NSEC_PER_MSEC;

	/* send test IPC's */
	while (1) {
		start = ktime_get();
		ret = sof_client_ipc_tx_message(dev, hdr.cmd, &hdr, hdr.size,
						&reply, sizeof(reply));
		end = ktime_get();

		if (ret < 0)
			break;

		/* compute min and max response times */
		ipc_response_time = ktime_to_ns(ktime_sub(end, start));
		min_response_time = min(min_response_time, ipc_response_time);
		max_response_time = max(max_response_time, ipc_response_time);

		/* sum up response times */
		avg_response_time += ipc_response_time;
		i++;

		/* test complete? */
		if (flood_duration_test) {
			if (ktime_to_ns(end) >= test_end)
				break;
		} else {
			if (i == ipc_count)
				break;
		}
	}

	if (ret < 0)
		dev_err(dev, "error: ipc flood test failed at %d iterations\n",
			i);

	/* return if the first IPC fails */
	if (!i)
		return ret;

	/* compute average response time */
	do_div(avg_response_time, i);

	/* clear previous test output */
	memset(ipc_client_data->buf, 0, IPC_FLOOD_TEST_RESULT_LEN);

	if (flood_duration_test) {
		dev_dbg(dev, "IPC Flood test duration: %lums\n",
			ipc_duration_ms);
		snprintf(ipc_client_data->buf, IPC_FLOOD_TEST_RESULT_LEN,
			 "IPC Flood test duration: %lums\n", ipc_duration_ms);
	}

	dev_dbg(dev,
		"IPC Flood count: %d, Avg response time: %lluns\n",
		i, avg_response_time);
	dev_dbg(dev, "Max response time: %lluns\n",
		max_response_time);
	dev_dbg(dev, "Min response time: %lluns\n",
		min_response_time);

	/* format output string and save test results */
	snprintf(ipc_client_data->buf + strlen(ipc_client_data->buf),
		 IPC_FLOOD_TEST_RESULT_LEN - strlen(ipc_client_data->buf),
		 "IPC Flood count: %d\nAvg response time: %lluns\n",
		 i, avg_response_time);

	snprintf(ipc_client_data->buf + strlen(ipc_client_data->buf),
		 IPC_FLOOD_TEST_RESULT_LEN - strlen(ipc_client_data->buf),
		 "Max response time: %lluns\nMin response time: %lluns\n",
		 max_response_time, min_response_time);

	return ret;
}

static ssize_t sof_ipc_dfsentry_write(struct file *file,
				      const char __user *buffer,
				      size_t count, loff_t *ppos)
{
	struct dentry *dentry = file->f_path.dentry;
	struct device *dev = file->private_data;
	unsigned long ipc_duration_ms = 0;
	bool flood_duration_test = false;
	unsigned long ipc_count = 0;
	char *string;
	size_t size;
	int err;
	int ret;

	string = kzalloc(count, GFP_KERNEL);
	if (!string)
		return -ENOMEM;

	size = simple_write_to_buffer(string, count, ppos, buffer, count);
	ret = size;

	if (!strcmp(dentry->d_name.name, "ipc_flood_duration_ms"))
		flood_duration_test = true;

	/* test completion criterion */
	if (flood_duration_test)
		ret = kstrtoul(string, 0, &ipc_duration_ms);
	else
		ret = kstrtoul(string, 0, &ipc_count);
	if (ret < 0)
		goto out;

	/* limit max duration/ipc count for flood test */
	if (flood_duration_test) {
		if (!ipc_duration_ms) {
			ret = size;
			goto out;
		}

		/* find the minimum. min() is not used to avoid warnings */
		if (ipc_duration_ms > MAX_IPC_FLOOD_DURATION_MS)
			ipc_duration_ms = MAX_IPC_FLOOD_DURATION_MS;
	} else {
		if (!ipc_count) {
			ret = size;
			goto out;
		}

		/* find the minimum. min() is not used to avoid warnings */
		if (ipc_count > MAX_IPC_FLOOD_COUNT)
			ipc_count = MAX_IPC_FLOOD_COUNT;
	}

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err_ratelimited(dev,
				    "error: debugfs write failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(dev);
		goto out;
	}

	/* flood test */
	ret = sof_debug_ipc_flood_test(dev, flood_duration_test,
				       ipc_duration_ms, ipc_count);

	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev,
				    "error: debugfs write failed to idle %d\n",
				    err);

	/* return size if test is successful */
	if (ret >= 0)
		ret = size;
out:
	kfree(string);
	return ret;
}

static ssize_t sof_ipc_dfsentry_read(struct file *file, char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	struct sof_ipc_client_data *ipc_client_data = sof_get_client_data(dev);
	size_t size_ret;

	if (*ppos)
		return 0;

	/* return results of the last IPC test */
	count = strlen(ipc_client_data->buf);
	size_ret = copy_to_user(buffer, ipc_client_data->buf, count);
	if (size_ret)
		return -EFAULT;

	*ppos += count;
	return count;
}

static const struct file_operations sof_ipc_dfs_fops = {
	.open = simple_open,
	.read = sof_ipc_dfsentry_read,
	.llseek = default_llseek,
	.write = sof_ipc_dfsentry_write,
};

static int sof_ipc_test_probe(struct platform_device *pdev)
{
	struct snd_sof_client *ipc_client = dev_get_platdata(&pdev->dev);
	struct sof_ipc_client_data *ipc_client_data;

	ipc_client->pdev = pdev;

	/* allocate memory for client data */
	ipc_client_data = devm_kzalloc(&pdev->dev, sizeof(*ipc_client_data),
				       GFP_KERNEL);
	if (!ipc_client_data)
		return -ENOMEM;

	ipc_client_data->buf = devm_kzalloc(&pdev->dev,
					    IPC_FLOOD_TEST_RESULT_LEN,
					    GFP_KERNEL);
	if (!ipc_client_data->buf)
		return -ENOMEM;

	ipc_client->client_data = ipc_client_data;

	/* create ipc-test-client debugfs dir under parent SOF dir */
	ipc_client_data->dfs_root =
		debugfs_create_dir("ipc-flood-test",
				   sof_client_get_debugfs_root(&pdev->dev));

	/* create read-write ipc_flood_count debugfs entry */
	debugfs_create_file("ipc_flood_count", 0644,
			    ipc_client_data->dfs_root,
			    &pdev->dev, &sof_ipc_dfs_fops);

	/* create read-write ipc_flood_duration_ms debugfs entry */
	debugfs_create_file("ipc_flood_duration_ms", 0644,
			    ipc_client_data->dfs_root,
			    &pdev->dev, &sof_ipc_dfs_fops);

	/* probe complete, register with SOF core */
	sof_client_register(&pdev->dev);

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(&pdev->dev,
					 SOF_IPC_CLIENT_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;
}

static int sof_ipc_test_remove(struct platform_device *pdev)
{
	struct sof_ipc_client_data *ipc_client_data =
		sof_get_client_data(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	debugfs_remove_recursive(ipc_client_data->dfs_root);

	return 0;
}

static struct platform_driver sof_ipc_test_driver = {
	.driver = {
		.name = "sof-ipc-test",
	},

	.probe = sof_ipc_test_probe,
	.remove = sof_ipc_test_remove,
};

module_platform_driver(sof_ipc_test_driver);

MODULE_DESCRIPTION("SOF IPC Test Client Platform Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
MODULE_ALIAS("platform:sof-ipc-test");
