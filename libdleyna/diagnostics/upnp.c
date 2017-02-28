/*
 * dLeyna
 *
 * Copyright (C) 2012-2017 Intel Corporation. All rights reserved.
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

#include <string.h>

#include <libgssdp/gssdp-resource-browser.h>
#include <libgupnp/gupnp-context-manager.h>
#include <libgupnp/gupnp-error.h>

#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>
#include <libdleyna/core/service-task.h>

#include "async.h"
#include "device.h"
#include "prop-defs.h"
#include "upnp.h"

#define DLD_BASIC_MANAGEMENT_SERVICE_TYPE \
				"urn:schemas-upnp-org:service:BasicManagement"

struct dld_upnp_t_ {
	dleyna_connector_id_t connection;
	const dleyna_connector_dispatch_cb_t *interface_info;
	dld_upnp_callback_t found_device;
	dld_upnp_callback_t lost_device;
	GUPnPContextManager *context_manager;
	void *user_data;
	GHashTable *device_udn_map;
	GHashTable *device_uc_map;
	guint counter;
};

/* Private structure used in service task */
typedef struct prv_device_new_ct_t_ prv_device_new_ct_t;
struct prv_device_new_ct_t_ {
	dld_upnp_t *upnp;
	char *udn;
	gchar *ip_address;
	dld_device_t *device;
	const dleyna_task_queue_key_t *queue_id;
};

static void prv_device_new_free(prv_device_new_ct_t *priv_t)
{
	if (priv_t) {
		g_free(priv_t->udn);
		g_free(priv_t->ip_address);
		g_free(priv_t);
	}
}

static void prv_device_chain_end(gboolean cancelled, gpointer data)
{
	dld_device_t *device;
	prv_device_new_ct_t *priv_t = (prv_device_new_ct_t *)data;

	DLEYNA_LOG_DEBUG("Enter");

	device = priv_t->device;

	if (cancelled)
		goto on_clear;

	DLEYNA_LOG_DEBUG("Notify new device available: %s", device->path);
	g_hash_table_insert(priv_t->upnp->device_udn_map, g_strdup(priv_t->udn),
			    device);
	priv_t->upnp->found_device(device->path);

on_clear:

	g_hash_table_remove(priv_t->upnp->device_uc_map, priv_t->udn);
	prv_device_new_free(priv_t);

	if (cancelled)
		dld_device_delete(device);

	DLEYNA_LOG_DEBUG("Exit");
	DLEYNA_LOG_DEBUG_NL();
}

static void prv_device_context_switch_end(gboolean cancelled, gpointer data)
{
	prv_device_new_ct_t *priv_t = (prv_device_new_ct_t *)data;

	DLEYNA_LOG_DEBUG("Enter");

	prv_device_new_free(priv_t);

	DLEYNA_LOG_DEBUG("Exit");
}

static const dleyna_task_queue_key_t *prv_create_device_queue(
						prv_device_new_ct_t **priv_t)
{
	const dleyna_task_queue_key_t *queue_id;

	*priv_t = g_new0(prv_device_new_ct_t, 1);

	queue_id = dleyna_task_processor_add_queue(
				dld_diagnostics_service_get_task_processor(),
				dleyna_service_task_create_source(),
				DLD_DIAGNOSTICS_SINK,
				DLEYNA_TASK_QUEUE_FLAG_AUTO_REMOVE,
				dleyna_service_task_process_cb,
				dleyna_service_task_cancel_cb,
				dleyna_service_task_delete_cb);
	dleyna_task_queue_set_finally(queue_id, prv_device_chain_end);
	dleyna_task_queue_set_user_data(queue_id, *priv_t);


	return queue_id;
}

static void prv_update_device_context(prv_device_new_ct_t *priv_t,
				      dld_upnp_t *upnp, const char *udn,
				      dld_device_t *device,
				      const gchar *ip_address,
				      const dleyna_task_queue_key_t *queue_id)
{
	priv_t->upnp = upnp;
	priv_t->udn = g_strdup(udn);
	priv_t->ip_address = g_strdup(ip_address);
	priv_t->queue_id = queue_id;
	priv_t->device = device;

	g_hash_table_insert(upnp->device_uc_map, g_strdup(udn), priv_t);
}

static void prv_add_device(dld_upnp_t *upnp, GUPnPDeviceProxy *dev_proxy,
			   GUPnPServiceProxy *bms_proxy,
			   const gchar *ip_address, const char *udn)
{
	dld_device_t *device;
	dld_device_context_t *context;
	const dleyna_task_queue_key_t *queue_id;
	unsigned int i;
	prv_device_new_ct_t *priv_t;

	DLEYNA_LOG_DEBUG("Enter");

	device = g_hash_table_lookup(upnp->device_udn_map, udn);

	if (!device) {
		priv_t = g_hash_table_lookup(upnp->device_uc_map, udn);

		if (priv_t)
			device = priv_t->device;
	}

	if (!device) {
		DLEYNA_LOG_DEBUG("Device not found. Adding");

		queue_id = prv_create_device_queue(&priv_t);

		device = dld_device_new(upnp->connection, dev_proxy, bms_proxy,
					ip_address,
					upnp->counter,
					upnp->interface_info,
					queue_id);

		prv_update_device_context(priv_t, upnp, udn, device, ip_address,
					  queue_id);

		upnp->counter++;
	} else {
		DLEYNA_LOG_DEBUG("Device Found");

		for (i = 0; i < device->contexts->len; ++i) {
			context = g_ptr_array_index(device->contexts, i);
			if (!strcmp(context->ip_address, ip_address))
				break;
		}

		if (i == device->contexts->len) {
			DLEYNA_LOG_DEBUG("Adding Context");
			dld_device_append_new_context(device, ip_address,
						      dev_proxy, bms_proxy);
		}
	}

	return;
}

static void prv_add_sub_device(dld_upnp_t *upnp, GUPnPDeviceProxy *sub_proxy,
			       GUPnPServiceProxy *bms_proxy,
			       const gchar *ip_address)
{
	const char *udn;

	DLEYNA_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *)sub_proxy);

	if (!udn)
		goto on_error;

	DLEYNA_LOG_DEBUG("UDN %s", udn);
	DLEYNA_LOG_DEBUG("IP Address %s", ip_address);

	prv_add_device(upnp, sub_proxy, bms_proxy, ip_address, udn);

on_error:

	DLEYNA_LOG_DEBUG("Exit");
	DLEYNA_LOG_DEBUG_NL();

	return;
}

static GUPnPServiceInfo *prv_add_bm_service_sub_devices(
						GUPnPDeviceInfo *device_info,
						dld_upnp_t *upnp,
						const gchar *ip_address)
{
	GList *child_devices;
	GList *next;
	GUPnPDeviceInfo *child_info = NULL;
	GUPnPServiceInfo *service_info = NULL;

	DLEYNA_LOG_DEBUG("Enter");

	child_devices = gupnp_device_info_list_devices(device_info);

	next = child_devices;
	while (next != NULL) {
		child_info = (GUPnPDeviceInfo *)next->data;

		service_info = gupnp_device_info_get_service(child_info,
					  DLD_BASIC_MANAGEMENT_SERVICE_TYPE);

		if (service_info != NULL)
			prv_add_sub_device(upnp,
					   (GUPnPDeviceProxy *)child_info,
					   (GUPnPServiceProxy *)service_info,
					   ip_address);

		service_info = prv_add_bm_service_sub_devices(child_info,
							      upnp,
							      ip_address);

		if (service_info != NULL)
			prv_add_sub_device(upnp,
					   (GUPnPDeviceProxy *)child_info,
					   (GUPnPServiceProxy *)service_info,
					   ip_address);

		next = g_list_next(next);
	}

	g_list_free_full(child_devices, g_object_unref);

	DLEYNA_LOG_DEBUG("Exit");

	return service_info;
}

static void prv_device_available_cb(GUPnPControlPoint *cp,
				    GUPnPDeviceProxy *proxy,
				    gpointer user_data)
{
	dld_upnp_t *upnp = user_data;
	const char *udn;
	const gchar *ip_address;
	GUPnPServiceProxy *bms_proxy;

	DLEYNA_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *)proxy);

	ip_address = gupnp_context_get_host_ip(
					gupnp_control_point_get_context(cp));

	if (!udn || !ip_address)
		goto on_error;

	DLEYNA_LOG_DEBUG("UDN %s", udn);
	DLEYNA_LOG_DEBUG("IP Address %s", ip_address);

	bms_proxy = (GUPnPServiceProxy *)
			gupnp_device_info_get_service(
					(GUPnPDeviceInfo *)proxy,
					DLD_BASIC_MANAGEMENT_SERVICE_TYPE);

	if (bms_proxy != NULL)
		prv_add_device(upnp, proxy, bms_proxy, ip_address, udn);

	(void) prv_add_bm_service_sub_devices((GUPnPDeviceInfo *)proxy,
					      upnp,
					      ip_address);

on_error:

	DLEYNA_LOG_DEBUG("Exit");
	DLEYNA_LOG_DEBUG_NL();

	return;
}

static gboolean prv_subscribe_to_service_changes(gpointer user_data)
{
	dld_device_t *device = user_data;

	device->timeout_id = 0;
	dld_device_subscribe_to_service_changes(device);

	return FALSE;
}

static void prv_remove_device(dld_upnp_t *upnp, const gchar *ip_address,
			      const char *udn)
{
	dld_device_t *device;
	unsigned int i;
	dld_device_context_t *context;
	gboolean subscribed;
	gboolean under_construction = FALSE;
	prv_device_new_ct_t *priv_t;
	gboolean construction_ctx = FALSE;
	const dleyna_task_queue_key_t *queue_id;

	DLEYNA_LOG_DEBUG("Enter");

	device = g_hash_table_lookup(upnp->device_udn_map, udn);

	if (!device) {
		priv_t = g_hash_table_lookup(upnp->device_uc_map, udn);

		if (priv_t) {
			device = priv_t->device;
			under_construction = TRUE;
		}
	}

	if (!device) {
		DLEYNA_LOG_WARNING("Device not found. Ignoring");
		goto on_error;
	}

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (!strcmp(context->ip_address, ip_address))
			break;
	}

	if (i < device->contexts->len) {
		subscribed = (context->bms.subscribed);

		if (under_construction)
			construction_ctx = !strcmp(context->ip_address,
						   priv_t->ip_address);

		(void) g_ptr_array_remove_index(device->contexts, i);

		if (device->contexts->len == 0) {
			if (!under_construction) {
				DLEYNA_LOG_DEBUG(
					"Last Context lost. Delete device");

				upnp->lost_device(device->path);
				g_hash_table_remove(upnp->device_udn_map, udn);
			} else {
				DLEYNA_LOG_WARNING(
				       "Device under construction. Cancelling");

				dleyna_task_processor_cancel_queue(
							priv_t->queue_id);
			}
		} else if (under_construction && construction_ctx) {
			DLEYNA_LOG_WARNING(
				"Device under construction. Switching context");

			/* Cancel previous contruction task chain */
			g_hash_table_remove(priv_t->upnp->device_uc_map,
					    priv_t->udn);
			dleyna_task_queue_set_finally(
						priv_t->queue_id,
						prv_device_context_switch_end);
			dleyna_task_processor_cancel_queue(priv_t->queue_id);

			/* Create a new construction task chain */
			context = dld_device_get_context(device);
			queue_id = prv_create_device_queue(&priv_t);
			prv_update_device_context(priv_t, upnp, udn, device,
						  context->ip_address,
						  queue_id);

			/* Start tasks from current construction step */
			dld_device_construct(device, context, upnp->connection,
					     upnp->interface_info, queue_id);
		} else if (subscribed && !device->timeout_id) {
			DLEYNA_LOG_DEBUG("Subscribe on new context");

			device->timeout_id = g_timeout_add_seconds(1,
					prv_subscribe_to_service_changes,
					device);
		}
	}

on_error:

	DLEYNA_LOG_DEBUG("Exit");

	return;
}

static void prv_remove_sub_device(dld_upnp_t *upnp, GUPnPDeviceProxy *sub_proxy,
				  GUPnPServiceProxy *bms_proxy,
				  const gchar *ip_address)
{
	const char *udn;

	DLEYNA_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *)sub_proxy);

	if (!udn)
		goto on_error;

	DLEYNA_LOG_DEBUG("UDN %s", udn);
	DLEYNA_LOG_DEBUG("IP Address %s", ip_address);

	prv_remove_device(upnp, ip_address, udn);

on_error:

	DLEYNA_LOG_DEBUG("Exit");
	DLEYNA_LOG_DEBUG_NL();

	return;
}

static GUPnPServiceInfo *prv_remove_bm_service_sub_devices(
						GUPnPDeviceInfo *device_info,
						dld_upnp_t *upnp,
						const gchar *ip_address)
{
	GList *child_devices;
	GList *next;
	GUPnPDeviceInfo *child_info = NULL;
	GUPnPServiceInfo *service_info = NULL;

	DLEYNA_LOG_DEBUG("Enter");

	child_devices = gupnp_device_info_list_devices(device_info);

	next = child_devices;
	while (next != NULL) {
		child_info = (GUPnPDeviceInfo *)next->data;

		service_info = gupnp_device_info_get_service(child_info,
					  DLD_BASIC_MANAGEMENT_SERVICE_TYPE);

		if (service_info != NULL)
			prv_remove_sub_device(upnp,
					      (GUPnPDeviceProxy *)child_info,
					      (GUPnPServiceProxy *)service_info,
					      ip_address);

		service_info = prv_remove_bm_service_sub_devices(child_info,
								 upnp,
								 ip_address);

		if (service_info != NULL)
			prv_remove_sub_device(upnp,
					      (GUPnPDeviceProxy *)child_info,
					      (GUPnPServiceProxy *)service_info,
					      ip_address);

		next = g_list_next(next);
	}

	g_list_free_full(child_devices, g_object_unref);

	DLEYNA_LOG_DEBUG("Exit");

	return service_info;
}

static void prv_device_unavailable_cb(GUPnPControlPoint *cp,
				      GUPnPDeviceProxy *proxy,
				      gpointer user_data)
{
	dld_upnp_t *upnp = user_data;
	const char *udn;
	const gchar *ip_address;

	DLEYNA_LOG_DEBUG("Enter");

	udn = gupnp_device_info_get_udn((GUPnPDeviceInfo *)proxy);

	ip_address = gupnp_context_get_host_ip(
					gupnp_control_point_get_context(cp));

	if (!udn || !ip_address)
		goto on_error;

	DLEYNA_LOG_DEBUG("UDN %s", udn);
	DLEYNA_LOG_DEBUG("IP Address %s", ip_address);

	(void) prv_remove_bm_service_sub_devices((GUPnPDeviceInfo *)proxy,
						 upnp,
						 ip_address);

	prv_remove_device(upnp, ip_address, udn);

on_error:

	return;
}

static void prv_on_context_available(GUPnPContextManager *context_manager,
				     GUPnPContext *context,
				     gpointer user_data)
{
	dld_upnp_t *upnp = user_data;
	GUPnPControlPoint *cp;

	cp = gupnp_control_point_new(context, "upnp:rootdevice");

	g_signal_connect(cp, "device-proxy-available",
			 G_CALLBACK(prv_device_available_cb), upnp);

	g_signal_connect(cp, "device-proxy-unavailable",
			 G_CALLBACK(prv_device_unavailable_cb), upnp);

	gssdp_resource_browser_set_active(GSSDP_RESOURCE_BROWSER(cp), TRUE);
	gupnp_context_manager_manage_control_point(upnp->context_manager, cp);
	g_object_unref(cp);
}

dld_upnp_t *dld_upnp_new(dleyna_connector_id_t connection,
			 const dleyna_connector_dispatch_cb_t *dispatch_table,
			 dld_upnp_callback_t found_device,
			 dld_upnp_callback_t lost_device)
{
	dld_upnp_t *upnp = g_new0(dld_upnp_t, 1);

	upnp->connection = connection;
	upnp->interface_info = dispatch_table;
	upnp->found_device = found_device;
	upnp->lost_device = lost_device;

	upnp->device_udn_map = g_hash_table_new_full(g_str_hash, g_str_equal,
						     g_free,
						     dld_device_delete);

	upnp->device_uc_map = g_hash_table_new_full(g_str_hash, g_str_equal,
						    g_free, NULL);

	upnp->context_manager = gupnp_context_manager_create(0);

	g_signal_connect(upnp->context_manager, "context-available",
			 G_CALLBACK(prv_on_context_available),
			 upnp);

	return upnp;
}

void dld_upnp_delete(dld_upnp_t *upnp)
{
	if (upnp) {
		g_object_unref(upnp->context_manager);
		g_hash_table_unref(upnp->device_udn_map);
		g_hash_table_unref(upnp->device_uc_map);

		g_free(upnp);
	}
}

GVariant *dld_upnp_get_device_ids(dld_upnp_t *upnp)
{
	GVariantBuilder vb;
	GHashTableIter iter;
	gpointer value;
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	g_variant_builder_init(&vb, G_VARIANT_TYPE("ao"));
	g_hash_table_iter_init(&iter, upnp->device_udn_map);

	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		device = value;
		g_variant_builder_add(&vb, "o", device->path);
	}

	DLEYNA_LOG_DEBUG("Exit");

	return g_variant_ref_sink(g_variant_builder_end(&vb));
}

GHashTable *dld_upnp_get_device_udn_map(dld_upnp_t *upnp)
{
	return upnp->device_udn_map;
}

static dld_device_t *prv_get_and_check_device(dld_upnp_t *upnp,
					      dld_task_t *task,
					      dld_upnp_task_complete_t cb)
{
	dld_device_t *device;
	dld_async_task_t *cb_data = (dld_async_task_t *)task;

	device = dld_device_from_path(task->path, upnp->device_udn_map);

	if (!device) {
		DLEYNA_LOG_WARNING("Cannot locate device");

		cb_data->cb = cb;
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OBJECT_NOT_FOUND,
					     "Cannot locate a device for the "
					     "specified object");

		(void) g_idle_add(dld_async_task_complete, cb_data);
	}

	return device;
}

void dld_upnp_get_prop(dld_upnp_t *upnp, dld_task_t *task,
		       dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);
	DLEYNA_LOG_DEBUG("Prop.%s", task->ut.get_prop.prop_name);

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_get_prop(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_get_all_props(dld_upnp_t *upnp, dld_task_t *task,
			    dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	DLEYNA_LOG_DEBUG("Path: %s", task->path);
	DLEYNA_LOG_DEBUG("Interface %s", task->ut.get_prop.interface_name);

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_get_all_props(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_get_icon(dld_upnp_t *upnp, dld_task_t *task,
		       dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_get_icon(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_get_test_info(dld_upnp_t *upnp, dld_task_t *task,
			    dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_get_test_info(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_cancel_test(dld_upnp_t *upnp, dld_task_t *task,
			  dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_cancel_test(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_ping(dld_upnp_t *upnp, dld_task_t *task,
		   dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_ping(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_get_ping_result(dld_upnp_t *upnp, dld_task_t *task,
			      dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_get_ping_result(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_nslookup(dld_upnp_t *upnp, dld_task_t *task,
		       dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_nslookup(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_get_nslookup_result(dld_upnp_t *upnp, dld_task_t *task,
				  dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_get_nslookup_result(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_traceroute(dld_upnp_t *upnp, dld_task_t *task,
			 dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_traceroute(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_get_traceroute_result(dld_upnp_t *upnp, dld_task_t *task,
				    dld_upnp_task_complete_t cb)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = prv_get_and_check_device(upnp, task, cb);
	if (device != NULL)
		dld_device_get_traceroute_result(device, task, cb);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_unsubscribe(dld_upnp_t *upnp)
{
	GHashTableIter iter;
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	g_hash_table_iter_init(&iter, upnp->device_udn_map);
	while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&device))
		dld_device_unsubscribe(device);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_upnp_rescan(dld_upnp_t *upnp)
{
	DLEYNA_LOG_DEBUG("re-scanning control points");

	gupnp_context_manager_rescan_control_points(upnp->context_manager);
}

GUPnPContextManager *dld_upnp_get_context_manager(dld_upnp_t *upnp)
{
	return upnp->context_manager;
}
