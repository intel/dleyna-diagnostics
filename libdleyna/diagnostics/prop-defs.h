/*
 * dLeyna
 *
 * Copyright (C) 2012-2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Mark Ryan <mark.d.ryan@intel.com>
 *
 */

#ifndef DLD_PROPS_DEFS_H__
#define DLD_PROPS_DEFS_H__

#define DLD_INTERFACE_PROPERTIES "org.freedesktop.DBus.Properties"

#define DLD_INTERFACE_PROPERTIES_CHANGED "PropertiesChanged"

/* Manager Properties */
#define DLD_INTERFACE_PROP_NEVER_QUIT "NeverQuit"
#define DLD_INTERFACE_PROP_WHITE_LIST_ENTRIES "WhiteListEntries"
#define DLD_INTERFACE_PROP_WHITE_LIST_ENABLED "WhiteListEnabled"

#define DLD_INTERFACE_PROP_DEVICE_TYPE "DeviceType"
#define DLD_INTERFACE_PROP_UDN "UDN"
#define DLD_INTERFACE_PROP_FRIENDLY_NAME "FriendlyName"
#define DLD_INTERFACE_PROP_ICON_URL "IconURL"
#define DLD_INTERFACE_PROP_MANUFACTURER "Manufacturer"
#define DLD_INTERFACE_PROP_MANUFACTURER_URL "ManufacturerUrl"
#define DLD_INTERFACE_PROP_MODEL_DESCRIPTION "ModelDescription"
#define DLD_INTERFACE_PROP_MODEL_NAME "ModelName"
#define DLD_INTERFACE_PROP_MODEL_NUMBER "ModelNumber"
#define DLD_INTERFACE_PROP_SERIAL_NUMBER "SerialNumber"
#define DLD_INTERFACE_PROP_PRESENTATION_URL "PresentationURL"
#define DLD_INTERFACE_PROP_STATUS_INFO "StatusInfo"
#define DLD_INTERFACE_PROP_TEST_IDS "TestIDs"
#define DLD_INTERFACE_PROP_ACTIVE_TEST_IDS "ActiveTestIDs"

#endif /* DLD_PROPS_DEFS_H__ */
