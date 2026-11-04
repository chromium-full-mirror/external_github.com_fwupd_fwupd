/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-dell-psb-device.h"
#include "fu-scsi-device.h"
#include "fu-scsi-plugin.h"

struct _FuScsiPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuScsiPlugin, fu_scsi_plugin, FU_TYPE_PLUGIN)

static void
fu_scsi_plugin_init(FuScsiPlugin *self)
{
}

/* create a device of the given type from a backend device and add it */
static gboolean
fu_scsi_plugin_add_device(FuPlugin *plugin, FuDevice *device, GType gtype, GError **error)
{
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	dev = g_object_new(gtype, "context", fu_plugin_get_context(plugin), NULL);
	fu_device_incorporate(dev, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
	locker = fu_device_locker_new(dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_add_device(plugin, dev);

	/* success */
	return TRUE;
}

static gboolean
fu_scsi_plugin_backend_device_added(FuPlugin *plugin,
				    FuDevice *device,
				    FuProgress *progress,
				    GError **error)
{
	GType gtype = fu_device_get_specialized_gtype(device);
	g_autoptr(FuDevice) pci = NULL;

	/* a quirk already chose the type */
	if (gtype != G_TYPE_INVALID)
		return fu_scsi_plugin_add_device(plugin, device, gtype, error);

	/* a block device is a plain SCSI disk */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "scsi_generic") != 0)
		return fu_scsi_plugin_add_device(plugin, device, FU_TYPE_SCSI_DEVICE, error);

	/* only the Dell PSB switch is interesting on /dev/sg*: it is the PCI
	 * function 1000:00B2, and nothing else on this subsystem is ours */
	pci = fu_device_get_backend_parent_with_subsystem(device, "pci", NULL);
	if (pci == NULL)
		return TRUE;
	if (!fu_device_probe(pci, error))
		return FALSE;
	if (fu_device_get_vid(pci) != 0x1000 || fu_device_get_pid(pci) != 0x00B2)
		return TRUE;

	/* success */
	return fu_scsi_plugin_add_device(plugin, device, FU_TYPE_DELL_PSB_DEVICE, error);
}

static void
fu_scsi_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	FuContext *ctx = fu_plugin_get_context(plugin);

	fu_context_add_quirk_key(ctx, "ScsiWriteBufferSize");
	fu_plugin_add_device_udev_subsystem(plugin, "block:disk");
	fu_plugin_add_device_udev_subsystem(plugin, "scsi_generic");
	fu_plugin_add_device_gtype(plugin, FU_TYPE_SCSI_DEVICE);
	fu_plugin_add_device_gtype(plugin, FU_TYPE_DELL_PSB_DEVICE);

	/* chain up to parent */
	G_OBJECT_CLASS(fu_scsi_plugin_parent_class)->constructed(obj);
}

static void
fu_scsi_plugin_class_init(FuScsiPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_scsi_plugin_constructed;
	plugin_class->backend_device_added = fu_scsi_plugin_backend_device_added;
}
