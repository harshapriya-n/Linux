// SPDX-License-Identifier: GPL-2.0-only
/*
 * Software based bus for Auxiliary devices
 *
 * Copyright (c) 2019-2020 Intel Corporation
 *
 * Please see Documentation/driver-api/auxiliary_bus.rst for more information.
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/string.h>
#include <linux/auxiliary_bus.h>

static const struct auxiliary_device_id *auxiliary_match_id(const struct auxiliary_device_id *id,
							  const struct auxiliary_device *auxdev)
{
	while (id->name[0]) {
		const char *p = strrchr(dev_name(&auxdev->dev), '.');
		int match_size;

		if (!p) {
			id++;
			continue;
		}
		match_size = p - dev_name(&auxdev->dev);

		/* use dev_name(&auxdev->dev) prefix before last '.' char to match to */
		if (!strncmp(dev_name(&auxdev->dev), id->name, match_size) &&
		    strlen(id->name) == match_size)
			return id;
		id++;
	}
	return NULL;
}

static int auxiliary_match(struct device *dev, struct device_driver *drv)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct auxiliary_driver *auxdrv = to_auxiliary_drv(drv);

	return !!auxiliary_match_id(auxdrv->id_table, auxdev);
}

static int auxiliary_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	const char *name, *p;

	name = dev_name(dev);
	p = strrchr(name, '.');

	return add_uevent_var(env, "MODALIAS=%s%.*s", AUXILIARY_MODULE_PREFIX, (int)(p - name),
			      name);
}

static const struct dev_pm_ops auxiliary_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_generic_runtime_suspend, pm_generic_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_generic_suspend, pm_generic_resume)
};

static int auxiliary_bus_probe(struct device *dev)
{
	struct auxiliary_driver *auxdrv = to_auxiliary_drv(dev->driver);
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	int ret;

	ret = dev_pm_domain_attach(dev, true);
	if (ret) {
		dev_warn(dev, "Failed to attach to PM Domain : %d\n", ret);
		return ret;
	}

	ret = auxdrv->probe(auxdev, auxiliary_match_id(auxdrv->id_table, auxdev));
	if (ret)
		dev_pm_domain_detach(dev, true);

	return ret;
}

static int auxiliary_bus_remove(struct device *dev)
{
	struct auxiliary_driver *auxdrv = to_auxiliary_drv(dev->driver);
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	int ret = 0;

	if (auxdrv->remove)
		ret = auxdrv->remove(auxdev);
	dev_pm_domain_detach(dev, true);

	return ret;
}

static void auxiliary_bus_shutdown(struct device *dev)
{
	struct auxiliary_driver *auxdrv = to_auxiliary_drv(dev->driver);
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);

	if (auxdrv->shutdown)
		auxdrv->shutdown(auxdev);
}

struct bus_type auxiliary_bus_type = {
	.name = "auxiliary",
	.probe = auxiliary_bus_probe,
	.remove = auxiliary_bus_remove,
	.shutdown = auxiliary_bus_shutdown,
	.match = auxiliary_match,
	.uevent = auxiliary_uevent,
	.pm = &auxiliary_dev_pm_ops,
};

/**
 * auxiliary_device_initialize - check auxiliary_device and initialize
 * @auxdev: auxiliary device struct
 *
 * This is the first step in the two-step process to register an auxiliary_device.
 *
 * When this function returns an error code, then the device_initialize will *not* have
 * been performed, and the caller will be responsible to free any memory allocated for the
 * auxiliary_device in the error path directly.
 *
 * It returns 0 on success.  On success, the device_initialize has been performed.  After this
 * point any error unwinding will need to include a call to auxiliary_device_initialize().
 * In this post-initialize error scenario, a call to the device's .release callback will be
 * triggered by auxiliary_device_uninitialize(), and all memory clean-up is expected to be
 * handled there.
 */
int auxiliary_device_initialize(struct auxiliary_device *auxdev)
{
	struct device *dev = &auxdev->dev;

	dev->bus = &auxiliary_bus_type;

	if (!dev->parent) {
		pr_err("auxiliary_device has a NULL dev->parent\n");
		return -EINVAL;
	}

	if (!auxdev->name) {
		pr_err("acillary_device has a NULL name\n");
		return -EINVAL;
	}

	device_initialize(&auxdev->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(auxiliary_device_initialize);

/**
 * __auxiliary_device_add - add an auxiliary bus device
 * @auxdev: auxiliary bus device to add to the bus
 * @modname: name of the parent device's driver module
 *
 * This is the second step in the two-step process to register an auxiliary_device.
 *
 * This function must be called after a successful call to auxiliary_device_initialize(), which
 * will perform the device_initialize.  This means that if this returns an error code, then a
 * call to auxiliary_device_uninitialize() must be performed so that the .release callback will
 * be triggered to free the memory associated with the auxiliary_device.
 */
int __auxiliary_device_add(struct auxiliary_device *auxdev, const char *modname)
{
	struct device *dev = &auxdev->dev;
	int ret;

	if (!modname) {
		pr_err("auxiliary device modname is NULL\n");
		return -EINVAL;
	}

	ret = dev_set_name(dev, "%s.%s.%d", modname, auxdev->name, auxdev->id);
	if (ret) {
		pr_err("auxiliary device dev_set_name failed: %d\n", ret);
		return ret;
	}

	ret = device_add(dev);
	if (ret)
		dev_err(dev, "adding auxiliary device failed!: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(__auxiliary_device_add);

/**
 * __auxiliary_driver_register - register a driver for auxiliary bus devices
 * @auxdrv: auxiliary_driver structure
 * @owner: owning module/driver
 */
int __auxiliary_driver_register(struct auxiliary_driver *auxdrv, struct module *owner)
{
	if (WARN_ON(!auxdrv->probe))
		return -EINVAL;

	auxdrv->driver.owner = owner;
	auxdrv->driver.bus = &auxiliary_bus_type;

	return driver_register(&auxdrv->driver);
}
EXPORT_SYMBOL_GPL(__auxiliary_driver_register);

static int __init auxiliary_bus_init(void)
{
	return bus_register(&auxiliary_bus_type);
}

static void __exit auxiliary_bus_exit(void)
{
	bus_unregister(&auxiliary_bus_type);
}

module_init(auxiliary_bus_init);
module_exit(auxiliary_bus_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Auxiliary Bus");
MODULE_AUTHOR("David Ertman <david.m.ertman@intel.com>");
MODULE_AUTHOR("Kiran Patil <kiran.patil@intel.com>");
