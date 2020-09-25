// SPDX-License-Identifier: GPL-2.0-only
/*
 * Software based bus for Ancillary devices
 *
 * Copyright (c) 2019-2020 Intel Corporation
 *
 * Please see Documentation/driver-api/ancillary_bus.rst for more information.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <linux/ancillary_bus.h>

static const struct ancillary_device_id *ancillary_match_id(const struct ancillary_device_id *id,
							    const struct ancillary_device *adev)
{
	const char dot = '.';

	while (id->name[0]) {
		const char *p = strrchr(dev_name(&adev->dev), dot);
		int match_size;

		if (!p)
			continue;
		match_size = p - dev_name(&adev->dev);

		/* use dev_name(&adev->dev) prefix before last '.' char to match to */
		if (!strncmp(dev_name(&adev->dev), id->name, match_size))
			return id;
		id++;
	}
	return NULL;
}

static int ancillary_match(struct device *dev, struct device_driver *drv)
{
	struct ancillary_driver *adrv = to_ancillary_drv(drv);
	struct ancillary_device *adev = to_ancillary_dev(dev);

	return !!ancillary_match_id(adrv->id_table, adev);
}

static int ancillary_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct ancillary_device *adev = to_ancillary_dev(dev);
	char *match_name, *p;
	const char dot  = '.';
	int ret = 0;

	match_name = kasprintf(GFP_KERNEL, "%s", dev_name(&adev->dev));
	if (!match_name)
		return -ENOMEM;

	p = strrchr(match_name, dot);
	if (!p) {
		ret = -EINVAL;
		goto out;
	}
	*p = 0;

	if (add_uevent_var(env, "MODALIAS=%s%s", ANCILLARY_MODULE_PREFIX, match_name))
		ret = -ENOMEM;
out:
	kfree(match_name);
	return ret;
}

static const struct dev_pm_ops ancillary_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_generic_runtime_suspend, pm_generic_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_generic_suspend, pm_generic_resume)
};

struct bus_type ancillary_bus_type = {
	.name = "ancillary",
	.match = ancillary_match,
	.uevent = ancillary_uevent,
	.pm = &ancillary_dev_pm_ops,
};

/**
 * ancillary_device_initialize - check ancillary_device and initialize
 * @adev: ancillary device struct
 */
int ancillary_device_initialize(struct ancillary_device *adev)
{
	struct device *dev = &adev->dev;

	dev->bus = &ancillary_bus_type;

	if (WARN_ON(!dev->parent) || WARN_ON(!adev->name) ||
	    WARN_ON(!(dev->type && dev->type->release) && !dev->release))
		return -EINVAL;

	device_initialize(&adev->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(ancillary_device_initialize);

/**
 * ancillary_device_add - add an ancillary bus device
 * @adev: ancillary bus device to add to the bus
 * @modname: name of the parent device's driver module
 */
int __ancillary_device_add(struct ancillary_device *adev, const char *modname)
{
	struct device *dev = &adev->dev;
	int ret;

	if (WARN_ON(!modname))
		return -EINVAL;

	ret = dev_set_name(dev, "%s.%s.%d", modname, adev->name, adev->id);
	if (ret) {
		dev_err(dev->parent, "dev_set_name failed for device: %d\n", ret);
		return ret;
	}

	ret = device_add(dev);
	if (ret)
		dev_err(dev, "adding device failed!: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(__ancillary_device_add);

static int ancillary_probe_driver(struct device *dev)
{
	struct ancillary_driver *adrv = to_ancillary_drv(dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(dev);
	int ret;

	ret = dev_pm_domain_attach(dev, true);
	if (ret) {
		dev_warn(&adev->dev, "Failed to attach to PM Domain : %d\n", ret);
		return ret;
	}

	ret = adrv->probe(adev, ancillary_match_id(adrv->id_table, adev));
	if (ret)
		dev_pm_domain_detach(dev, true);

	return ret;
}

static int ancillary_remove_driver(struct device *dev)
{
	struct ancillary_driver *adrv = to_ancillary_drv(dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(dev);
	int ret;

	ret = adrv->remove(adev);
	dev_pm_domain_detach(dev, true);

	return ret;
}

static void ancillary_shutdown_driver(struct device *dev)
{
	struct ancillary_driver *adrv = to_ancillary_drv(dev->driver);
	struct ancillary_device *adev = to_ancillary_dev(dev);

	adrv->shutdown(adev);
}

/**
 * __ancillary_driver_register - register a driver for ancillary bus devices
 * @adrv: ancillary_driver structure
 * @owner: owning module/driver
 */
int __ancillary_driver_register(struct ancillary_driver *adrv, struct module *owner)
{
	if (WARN_ON(!adrv->probe) || WARN_ON(!adrv->remove) || WARN_ON(!adrv->shutdown) ||
	    WARN_ON(!adrv->id_table))
		return -EINVAL;

	adrv->driver.owner = owner;
	adrv->driver.bus = &ancillary_bus_type;
	adrv->driver.probe = ancillary_probe_driver;
	adrv->driver.remove = ancillary_remove_driver;
	adrv->driver.shutdown = ancillary_shutdown_driver;

	return driver_register(&adrv->driver);
}
EXPORT_SYMBOL_GPL(__ancillary_driver_register);

static int __init ancillary_bus_init(void)
{
	return bus_register(&ancillary_bus_type);
}

static void __exit ancillary_bus_exit(void)
{
	bus_unregister(&ancillary_bus_type);
}

module_init(ancillary_bus_init);
module_exit(ancillary_bus_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Ancillary Bus");
MODULE_AUTHOR("David Ertman <david.m.ertman@intel.com>");
MODULE_AUTHOR("Kiran Patil <kiran.patil@intel.com>");
