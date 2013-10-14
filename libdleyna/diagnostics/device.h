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

#ifndef DLD_DEVICE_H__
#define DLD_DEVICE_H__

#include <gio/gio.h>
#include <glib.h>

#include <libgupnp/gupnp-service-proxy.h>
#include <libgupnp/gupnp-device-proxy.h>

#include <libdleyna/core/connector.h>

#include "server.h"
#include "upnp.h"

typedef struct dls_service_t_ dls_service_t;
struct dls_service_t_ {
	GUPnPServiceProxy *proxy;
	gboolean subscribed;
	guint timeout_id;
};

typedef struct dld_device_context_t_ dld_device_context_t;
struct dld_device_context_t_ {
	gchar *ip_address;
	GUPnPDeviceProxy *device_proxy;
	dls_service_t bms;
	dld_device_t *device;
};

typedef struct dld_device_icon_t_ dld_device_icon_t;
struct dld_device_icon_t_ {
	gchar *mime_type;
	guchar *bytes;
	gsize size;
};

struct dld_device_t_ {
	dleyna_connector_id_t connection;
	guint ids[DLD_INTERFACE_INFO_MAX];
	gchar *path;
	GPtrArray *contexts;
	GHashTable *props;
	guint timeout_id;
	guint construct_step;
	dld_device_icon_t icon;
};

void dld_device_construct(
			dld_device_t *dev,
			dld_device_context_t *context,
			dleyna_connector_id_t connection,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id);

dld_device_t *dld_device_new(
			dleyna_connector_id_t connection,
			GUPnPDeviceProxy *proxy,
			GUPnPServiceProxy *bms_proxy,
			const gchar *ip_address,
			guint counter,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id);

void dld_device_delete(void *device);

void dld_device_unsubscribe(void *device);

void dld_device_append_new_context(dld_device_t *device,
				   const gchar *ip_address,
				   GUPnPDeviceProxy *proxy,
				   GUPnPServiceProxy *bms_proxy);

dld_device_t *dld_device_from_path(const gchar *path, GHashTable *device_list);

dld_device_context_t *dld_device_get_context(dld_device_t *device);

void dld_device_subscribe_to_service_changes(dld_device_t *device);


void dld_device_set_prop(dld_device_t *device, dld_task_t *task,
			 dld_upnp_task_complete_t cb);

void dld_device_get_prop(dld_device_t *device, dld_task_t *task,
			dld_upnp_task_complete_t cb);

void dld_device_get_all_props(dld_device_t *device, dld_task_t *task,
			      dld_upnp_task_complete_t cb);

void dld_device_get_icon(dld_device_t *device, dld_task_t *task,
			 dld_upnp_task_complete_t cb);

void dld_device_get_test_info(dld_device_t *device, dld_task_t *task,
			      dld_upnp_task_complete_t cb);

void dld_device_cancel_test(dld_device_t *device, dld_task_t *task,
			    dld_upnp_task_complete_t cb);

void dld_device_ping(dld_device_t *device, dld_task_t *task,
		     dld_upnp_task_complete_t cb);

void dld_device_get_ping_result(dld_device_t *device, dld_task_t *task,
				dld_upnp_task_complete_t cb);

void dld_device_nslookup(dld_device_t *device, dld_task_t *task,
			 dld_upnp_task_complete_t cb);

void dld_device_get_nslookup_result(dld_device_t *device, dld_task_t *task,
				    dld_upnp_task_complete_t cb);

void dld_device_traceroute(dld_device_t *device, dld_task_t *task,
			   dld_upnp_task_complete_t cb);

void dld_device_get_traceroute_result(dld_device_t *device, dld_task_t *task,
				      dld_upnp_task_complete_t cb);

#endif /* DLD_DEVICE_H__ */
