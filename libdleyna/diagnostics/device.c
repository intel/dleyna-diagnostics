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
#include <math.h>

#include <libsoup/soup.h>
#include <libgupnp/gupnp-control-point.h>

#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>
#include <libdleyna/core/service-task.h>

#include "async.h"
#include "device.h"
#include "prop-defs.h"
#include "server.h"
#include "xml-util.h"

typedef void (*dld_device_local_cb_t)(dld_async_task_t *cb_data);

typedef struct dld_device_data_t_ dld_device_data_t;
struct dld_device_data_t_ {
	dld_device_local_cb_t local_cb;
};

/* Private structure used in chain task */
typedef struct prv_new_device_ct_t_ prv_new_device_ct_t;
struct prv_new_device_ct_t_ {
	dld_device_t *dev;
	const dleyna_connector_dispatch_cb_t *dispatch_table;
};

typedef struct prv_download_info_t_ prv_download_info_t;
struct prv_download_info_t_ {
	SoupSession *session;
	SoupMessage *msg;
	dld_async_task_t *task;
};

typedef struct prv_nslookup_result_t_ prv_nslookup_result_t;
struct prv_nslookup_result_t_ {
	gchar *status;
	gchar *answer_type;
	gchar *hostname_returned;
	gchar *ip_addresses;
	gchar *dns_server_ip;
	gchar *response_time;
};

static void prv_bm_device_status_cb(GUPnPServiceProxy *proxy,
				    const char *variable,
				    GValue *value,
				    gpointer user_data);

static void prv_bm_test_ids_cb(GUPnPServiceProxy *proxy,
			       const char *variable,
			       GValue *value,
			       gpointer user_data);

static void prv_bm_active_test_ids_cb(GUPnPServiceProxy *proxy,
				      const char *variable,
				      GValue *value,
				      gpointer user_data);

static void prv_props_update(dld_device_t *device);


static void prv_unref_variant(gpointer variant)
{
	GVariant *var = variant;
	if (var)
		g_variant_unref(var);
}

static void prv_context_unsubscribe(dld_device_context_t *ctx)
{
	DLEYNA_LOG_DEBUG("Enter");

	if (ctx->bms.timeout_id) {
		(void) g_source_remove(ctx->bms.timeout_id);
		ctx->bms.timeout_id = 0;
	}

	if (ctx->bms.subscribed) {
		(void) gupnp_service_proxy_remove_notify(
			ctx->bms.proxy, "DeviceStatus",
			prv_bm_device_status_cb, ctx->device);

		(void) gupnp_service_proxy_remove_notify(
			ctx->bms.proxy, "TestIDs",
			prv_bm_test_ids_cb, ctx->device);

		(void) gupnp_service_proxy_remove_notify(
			ctx->bms.proxy, "ActiveTestIDs",
			prv_bm_active_test_ids_cb, ctx->device);

		gupnp_service_proxy_set_subscribed(ctx->bms.proxy, FALSE);

		ctx->bms.subscribed = FALSE;
	}

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_dld_context_delete(gpointer context)
{
	dld_device_context_t *ctx = context;

	if (ctx) {
		prv_context_unsubscribe(ctx);

		g_free(ctx->ip_address);
		if (ctx->device_proxy)
			g_object_unref(ctx->device_proxy);
		if (ctx->bms.proxy)
			g_object_unref(ctx->bms.proxy);
		g_free(ctx);
	}
}

static void prv_change_props(GHashTable *props,
			     const gchar *key,
			     GVariant *value,
			     GVariantBuilder *changed_props_vb)
{
	g_hash_table_insert(props, (gpointer) key, value);
	if (changed_props_vb)
		g_variant_builder_add(changed_props_vb, "{sv}", key, value);
}

static void prv_emit_signal_properties_changed(dld_device_t *device,
					       const char *interface,
					       GVariant *changed_props)
{
#if DLD_LOG_LEVEL & DLD_LOG_LEVEL_DEBUG
	gchar *params;
#endif
	GVariant *val = g_variant_ref_sink(g_variant_new("(s@a{sv}as)",
					   interface,
					   changed_props,
					   NULL));

	DLEYNA_LOG_DEBUG("Emitted Signal: %s.%s - ObjectPath: %s",
			 DLD_INTERFACE_PROPERTIES,
			 DLD_INTERFACE_PROPERTIES_CHANGED,
			 device->path);

#if DLD_LOG_LEVEL & DLD_LOG_LEVEL_DEBUG
	params = g_variant_print(val, FALSE);
	DLEYNA_LOG_DEBUG("Params: %s", params);
	g_free(params);
#endif

	dld_diagnostics_get_connector()->notify(device->connection,
					       device->path,
					       DLD_INTERFACE_PROPERTIES,
					       DLD_INTERFACE_PROPERTIES_CHANGED,
					       val,
					       NULL);

	g_variant_unref(val);
}

static void prv_context_new(const gchar *ip_address,
			    GUPnPDeviceProxy *proxy,
			    GUPnPServiceProxy *bms_proxy,
			    dld_device_t *device,
			    dld_device_context_t **context)
{
	dld_device_context_t *ctx = g_new(dld_device_context_t, 1);

	ctx->ip_address = g_strdup(ip_address);
	ctx->device_proxy = proxy;
	ctx->device = device;
	ctx->bms.subscribed = FALSE;
	ctx->bms.timeout_id = 0;
	ctx->bms.proxy = bms_proxy;

	g_object_ref(proxy);

	*context = ctx;
}

static dld_device_context_t *prv_device_get_subscribed_context(
						const dld_device_t *device)
{
	dld_device_context_t *context;
	unsigned int i;

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (context->bms.subscribed)
			goto on_found;
	}

	return NULL;

on_found:

	return context;
}

static void prv_device_append_new_context(dld_device_t *device,
					  const gchar *ip_address,
					  GUPnPDeviceProxy *proxy,
					  GUPnPServiceProxy *bms_proxy)
{
	dld_device_context_t *new_context;

	prv_context_new(ip_address, proxy, bms_proxy, device, &new_context);
	g_ptr_array_add(device->contexts, new_context);
}

static void prv_device_subscribe_context(dld_device_t *device)
{
	dld_device_context_t *subscribed_context;
	dld_device_context_t *preferred_context;

	subscribed_context = prv_device_get_subscribed_context(device);
	preferred_context = dld_device_get_context(device);

	if (subscribed_context != preferred_context) {
		if (subscribed_context) {
			DLEYNA_LOG_DEBUG(
					"Subscription switch from <%s> to <%s>",
					subscribed_context->ip_address,
					preferred_context->ip_address);
			prv_context_unsubscribe(subscribed_context);
		}
		dld_device_subscribe_to_service_changes(device);
	}
}

void dld_device_append_new_context(dld_device_t *device,
				   const gchar *ip_address,
				   GUPnPDeviceProxy *proxy,
				   GUPnPServiceProxy *bms_proxy)
{
	prv_device_append_new_context(device, ip_address, proxy, bms_proxy);
	prv_device_subscribe_context(device);
}

void dld_device_delete(void *device)
{
	unsigned int i;
	dld_device_t *dev = device;

	if (dev) {
		if (dev->timeout_id)
			(void) g_source_remove(dev->timeout_id);

		for (i = 0; i < DLD_INTERFACE_INFO_MAX && dev->ids[i]; ++i)
			(void) dld_diagnostics_get_connector()->unpublish_object(
								dev->connection,
								dev->ids[i]);
		g_ptr_array_unref(dev->contexts);
		g_free(dev->path);

		g_hash_table_unref(dev->props);

		g_free(dev->icon.mime_type);
		g_free(dev->icon.bytes);

		g_free(dev);
	}
}

void dld_device_unsubscribe(void *device)
{
	unsigned int i;
	dld_device_t *dev = device;
	dld_device_context_t *context;

	if (dev) {
		for (i = 0; i < dev->contexts->len; ++i) {
			context = g_ptr_array_index(dev->contexts, i);
			prv_context_unsubscribe(context);
		}
	}
}

static gboolean prv_re_enable_bms_subscription(gpointer user_data)
{
	dld_device_context_t *context = user_data;

	context->bms.timeout_id = 0;

	return FALSE;
}

static void prv_bms_subscription_lost_cb(GUPnPServiceProxy *proxy,
					 const GError *reason,
					 gpointer user_data)
{
	dld_device_context_t *context = user_data;

	if (!context->bms.timeout_id) {
		gupnp_service_proxy_set_subscribed(context->bms.proxy, TRUE);
		context->bms.timeout_id = g_timeout_add_seconds(10,
						prv_re_enable_bms_subscription,
						context);
	} else {
		g_source_remove(context->bms.timeout_id);
		(void) gupnp_service_proxy_remove_notify(
				context->bms.proxy, "DeviceStatus",
				prv_bm_device_status_cb, context->device);
		(void) gupnp_service_proxy_remove_notify(
				context->bms.proxy, "TestIDs",
				prv_bm_test_ids_cb, context->device);
		(void) gupnp_service_proxy_remove_notify(
				context->bms.proxy, "ActiveTestIDs",
				prv_bm_active_test_ids_cb, context->device);

		context->bms.timeout_id = 0;
		context->bms.subscribed = FALSE;
	}
}

void dld_device_subscribe_to_service_changes(dld_device_t *device)
{
	dld_device_context_t *context;

	context = dld_device_get_context(device);

	DLEYNA_LOG_DEBUG("Subscribing through context <%s>",
			 context->ip_address);

	if (context->bms.proxy) {
		gupnp_service_proxy_set_subscribed(context->bms.proxy, TRUE);
		(void) gupnp_service_proxy_add_notify(context->bms.proxy,
						      "DeviceStatus",
						      G_TYPE_STRING,
						      prv_bm_device_status_cb,
						      device);
		(void) gupnp_service_proxy_add_notify(context->bms.proxy,
						      "TestIDs",
						      G_TYPE_STRING,
						      prv_bm_test_ids_cb,
						      device);
		(void) gupnp_service_proxy_add_notify(context->bms.proxy,
						      "ActiveTestIDs",
						      G_TYPE_STRING,
						      prv_bm_active_test_ids_cb,
						      device);
		context->bms.subscribed = TRUE;

		g_signal_connect(context->bms.proxy,
				 "subscription-lost",
				 G_CALLBACK(prv_bms_subscription_lost_cb),
				 context);
	}
}

static GUPnPServiceProxyAction *prv_subscribe(dleyna_service_task_t *task,
					      GUPnPServiceProxy *proxy,
					      gboolean *failed)
{
	dld_device_t *device;

	DLEYNA_LOG_DEBUG("Enter");

	device = (dld_device_t *)dleyna_service_task_get_user_data(task);

	device->construct_step++;
	prv_device_subscribe_context(device);

	*failed = FALSE;

	DLEYNA_LOG_DEBUG("Exit");

	return NULL;
}

static GUPnPServiceProxyAction *prv_declare(dleyna_service_task_t *task,
					    GUPnPServiceProxy *proxy,
					    gboolean *failed)
{
	unsigned int i;
	dld_device_t *device;
	prv_new_device_ct_t *priv_t;
	const dleyna_connector_dispatch_cb_t *table;

	DLEYNA_LOG_DEBUG("Enter");

	*failed = FALSE;

	priv_t = (prv_new_device_ct_t *)dleyna_service_task_get_user_data(task);
	device = priv_t->dev;
	device->construct_step++;

	table = priv_t->dispatch_table;

	for (i = 0; i < DLD_INTERFACE_INFO_MAX; ++i) {
		device->ids[i] = dld_diagnostics_get_connector()->publish_object(
					device->connection,
					device->path,
					FALSE,
					dld_diagnostics_get_interface_name(i),
					table + i);

		if (!device->ids[i]) {
			*failed = TRUE;
			goto on_error;
		}
	}

on_error:

	DLEYNA_LOG_DEBUG("Exit");

	return NULL;
}

void dld_device_construct(
			dld_device_t *dev,
			dld_device_context_t *context,
			dleyna_connector_id_t connection,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id)
{
	prv_new_device_ct_t *priv_t;

	DLEYNA_LOG_DEBUG("Current step: %d", dev->construct_step);

	priv_t = g_new0(prv_new_device_ct_t, 1);

	priv_t->dev = dev;
	priv_t->dispatch_table = dispatch_table;

	if (dev->construct_step < 1)
		dleyna_service_task_add(queue_id, prv_subscribe,
					context->bms.proxy,
					NULL, NULL, dev);

	if (dev->construct_step < 2)
		dleyna_service_task_add(queue_id, prv_declare,
					context->bms.proxy,
					NULL, g_free, priv_t);

	dleyna_task_queue_start(queue_id);

	DLEYNA_LOG_DEBUG("Exit");
}

dld_device_t *dld_device_new(
			dleyna_connector_id_t connection,
			GUPnPDeviceProxy *proxy,
			GUPnPServiceProxy *bms_proxy,
			const gchar *ip_address,
			guint counter,
			const dleyna_connector_dispatch_cb_t *dispatch_table,
			const dleyna_task_queue_key_t *queue_id)
{
	dld_device_t *dev;
	gchar *new_path;
	dld_device_context_t *context;

	DLEYNA_LOG_DEBUG("New Diagnostics Device on %s", ip_address);

	new_path = g_strdup_printf("%s/%u", DLEYNA_DIAGNOSTICS_PATH, counter);
	DLEYNA_LOG_DEBUG("Diagnostics Device Path %s", new_path);

	dev = g_new0(dld_device_t, 1);

	dev->connection = connection;
	dev->contexts = g_ptr_array_new_with_free_func(prv_dld_context_delete);
	dev->path = new_path;
	dev->props = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
					   prv_unref_variant);

	prv_device_append_new_context(dev, ip_address, proxy, bms_proxy);

	prv_props_update(dev);

	context = dld_device_get_context(dev);

	dld_device_construct(dev, context, connection,
			     dispatch_table, queue_id);

	DLEYNA_LOG_DEBUG("Exit");

	return dev;
}

dld_device_t *dld_device_from_path(const gchar *path, GHashTable *device_list)
{
	GHashTableIter iter;
	gpointer value;
	dld_device_t *device;
	dld_device_t *retval = NULL;

	g_hash_table_iter_init(&iter, device_list);
	while (g_hash_table_iter_next(&iter, NULL, &value)) {
		device = value;
		if (!strcmp(device->path, path)) {
			retval = device;
			break;
		}
	}

	return retval;
}

dld_device_context_t *dld_device_get_context(dld_device_t *device)
{
	dld_device_context_t *context;
	unsigned int i;
	const char ip4_local_prefix[] = "127.0.0.";

	for (i = 0; i < device->contexts->len; ++i) {
		context = g_ptr_array_index(device->contexts, i);
		if (!strncmp(context->ip_address, ip4_local_prefix,
			     sizeof(ip4_local_prefix) - 1) ||
		    !strcmp(context->ip_address, "::1") ||
		    !strcmp(context->ip_address, "0:0:0:0:0:0:0:1"))
			break;
	}

	if (i == device->contexts->len)
		context = g_ptr_array_index(device->contexts, 0);

	return context;
}

static void prv_get_prop(dld_async_task_t *cb_data)
{
	dld_task_get_prop_t *get_prop = &cb_data->task.ut.get_prop;
	GVariant *res = NULL;

	DLEYNA_LOG_DEBUG("Enter");

	if (!strcmp(get_prop->interface_name,
		    DLEYNA_DIAGNOSTICS_INTERFACE_DEVICE) ||
	    !strcmp(get_prop->interface_name, "")) {
		res = g_hash_table_lookup(cb_data->device->props,
					  get_prop->prop_name);
	} else {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Unknown Interface");
	}

	if (!res) {
		if (!cb_data->error)
			cb_data->error =
				g_error_new(DLEYNA_SERVER_ERROR,
					    DLEYNA_ERROR_UNKNOWN_PROPERTY,
					    "Property not defined for object");
	} else {
		cb_data->task.result = g_variant_ref(res);
	}

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_add_props(GHashTable *props, GVariantBuilder *vb)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	g_hash_table_iter_init(&iter, props);

	while (g_hash_table_iter_next(&iter, &key, &value))
		g_variant_builder_add(vb, "{sv}", (gchar *)key,
				      (GVariant *)value);
}

static void prv_get_props(dld_async_task_t *cb_data)
{
	dld_task_get_props_t *get_props = &cb_data->task.ut.get_props;
	GVariantBuilder *vb;

	DLEYNA_LOG_DEBUG("Enter");

	vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	if (!strcmp(get_props->interface_name,
		    DLEYNA_DIAGNOSTICS_INTERFACE_DEVICE) ||
	    !strcmp(get_props->interface_name, "")) {
		prv_add_props(cb_data->device->props, vb);
	} else {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_UNKNOWN_INTERFACE,
					     "Unknown Interface");
		goto on_error;
	}

	cb_data->task.result = g_variant_ref_sink(g_variant_builder_end(vb));

on_error:

	g_variant_builder_unref(vb);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_bm_device_status_cb(GUPnPServiceProxy *proxy,
				    const char *variable,
				    GValue *value,
				    gpointer user_data)
{
	dld_device_t *device = user_data;
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;
	GVariantBuilder device_status_vb;
	gchar **parts;
	unsigned int i = 0;
	const gchar *device_status_str;
	GVariant *device_status;

	device_status_str = g_value_get_string(value);

	DLEYNA_LOG_DEBUG("prv_bm_device_status_cb: %s", device_status_str);

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	g_variant_builder_init(&device_status_vb, G_VARIANT_TYPE("as"));

	parts = g_strsplit(device_status_str, ",", 0);
	while (parts[i]) {
		g_strstrip(parts[i]);

		g_variant_builder_add(&device_status_vb, "s", parts[i]);
		++i;
	}

	device_status = g_variant_builder_end(&device_status_vb);

	prv_change_props(device->props, DLD_INTERFACE_PROP_STATUS_INFO,
			 g_variant_ref_sink(device_status),
			 changed_props_vb);

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));

	prv_emit_signal_properties_changed(device,
					   DLEYNA_DIAGNOSTICS_INTERFACE_DEVICE,
					   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);

	g_strfreev(parts);
}

static void prv_bm_test_ids_prop_change(dld_device_t *device, const gchar *key,
					const gchar *ids_str)
{
	GVariantBuilder *changed_props_vb;
	GVariant *changed_props;
	GVariantBuilder ids_vb;
	gchar **parts;
	unsigned int i = 0;
	GVariant *ids;

	changed_props_vb = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	g_variant_builder_init(&ids_vb, G_VARIANT_TYPE("au"));

	parts = g_strsplit(ids_str, ",", 0);
	while (parts[i]) {
		g_strstrip(parts[i]);

		g_variant_builder_add(&ids_vb, "u", atoi(parts[i]));
		++i;
	}

	ids = g_variant_builder_end(&ids_vb);

	prv_change_props(device->props, key,
			 g_variant_ref_sink(ids),
			 changed_props_vb);

	changed_props = g_variant_ref_sink(
				g_variant_builder_end(changed_props_vb));

	prv_emit_signal_properties_changed(device,
					   DLEYNA_DIAGNOSTICS_INTERFACE_DEVICE,
					   changed_props);
	g_variant_unref(changed_props);
	g_variant_builder_unref(changed_props_vb);

	g_strfreev(parts);
}

static void prv_bm_test_ids_cb(GUPnPServiceProxy *proxy,
			       const char *variable,
			       GValue *value,
			       gpointer user_data)
{
	dld_device_t *device = user_data;
	const gchar *test_ids_str;

	test_ids_str = g_value_get_string(value);

	DLEYNA_LOG_DEBUG("prv_bm_test_ids_cb: %s", test_ids_str);

	prv_bm_test_ids_prop_change(device, DLD_INTERFACE_PROP_TEST_IDS,
				    test_ids_str);
}

static void prv_bm_active_test_ids_cb(GUPnPServiceProxy *proxy,
				      const char *variable,
				      GValue *value,
				      gpointer user_data)
{
	dld_device_t *device = user_data;
	const gchar *active_test_ids_str;

	active_test_ids_str = g_value_get_string(value);

	DLEYNA_LOG_DEBUG("prv_bm_active_test_ids_cb: %s", active_test_ids_str);

	prv_bm_test_ids_prop_change(device, DLD_INTERFACE_PROP_ACTIVE_TEST_IDS,
				    active_test_ids_str);
}

static void prv_props_update(dld_device_t *device)
{
	dld_device_context_t *context;
	GUPnPDeviceInfo *proxy;
	GVariant *val;
	gchar *str;
	const gchar *const_str;
	GHashTable *props = device->props;

	context = dld_device_get_context(device);

	proxy = (GUPnPDeviceInfo *)context->device_proxy;

	const_str = gupnp_device_info_get_device_type(proxy);
	val = g_variant_ref_sink(g_variant_new_string(const_str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_DEVICE_TYPE, val);

	const_str = gupnp_device_info_get_udn(proxy);
	val = g_variant_ref_sink(g_variant_new_string(const_str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_UDN, val);

	str = gupnp_device_info_get_friendly_name(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_FRIENDLY_NAME, val);
	g_free(str);

	str = gupnp_device_info_get_icon_url(proxy, NULL, -1, -1, -1, FALSE,
					     NULL, NULL, NULL, NULL);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_ICON_URL, val);
	g_free(str);

	str = gupnp_device_info_get_manufacturer(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_MANUFACTURER, val);
	g_free(str);

	str = gupnp_device_info_get_manufacturer_url(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_MANUFACTURER_URL, val);
	g_free(str);

	str = gupnp_device_info_get_model_description(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_MODEL_DESCRIPTION, val);
	g_free(str);

	str = gupnp_device_info_get_model_name(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_MODEL_NAME, val);
	g_free(str);

	str = gupnp_device_info_get_model_number(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_MODEL_NUMBER, val);
	g_free(str);

	str = gupnp_device_info_get_serial_number(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_SERIAL_NUMBER, val);
	g_free(str);

	str = gupnp_device_info_get_presentation_url(proxy);
	val = g_variant_ref_sink(g_variant_new_string(str));
	g_hash_table_insert(props, DLD_INTERFACE_PROP_PRESENTATION_URL, val);
	g_free(str);
}

void dld_device_get_prop(dld_device_t *device, dld_task_t *task,
			 dld_upnp_task_complete_t cb)
{
	dld_async_task_t *cb_data = (dld_async_task_t *)task;
	dld_task_get_prop_t *get_prop = &task->ut.get_prop;

	cb_data->cb = cb;
	cb_data->device = device;

	prv_get_prop(cb_data);

	(void) g_idle_add(dld_async_task_complete, cb_data);
}

void dld_device_get_all_props(dld_device_t *device, dld_task_t *task,
			      dld_upnp_task_complete_t cb)
{
	dld_async_task_t *cb_data = (dld_async_task_t *)task;
	dld_task_get_props_t *get_props = &task->ut.get_props;

	cb_data->cb = cb;
	cb_data->device = device;

	prv_get_props(cb_data);

	(void) g_idle_add(dld_async_task_complete, cb_data);
}

static void prv_build_icon_result(dld_device_t *device, dld_task_t *task)
{
	GVariant *out_p[2];

	out_p[0] = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
					     device->icon.bytes,
					     device->icon.size,
					     1);
	out_p[1] = g_variant_new_string(device->icon.mime_type);
	task->result = g_variant_ref_sink(g_variant_new_tuple(out_p, 2));
}

static void prv_get_icon_cancelled(GCancellable *cancellable,
				   gpointer user_data)
{
	prv_download_info_t *download = (prv_download_info_t *)user_data;

	dld_async_task_cancelled(cancellable, download->task);

	if (download->msg) {
		soup_session_cancel_message(download->session, download->msg,
					    SOUP_STATUS_CANCELLED);
		DLEYNA_LOG_DEBUG("Cancelling device icon download");
	}
}

static void prv_free_download_info(prv_download_info_t *download)
{
	if (download->msg)
		g_object_unref(download->msg);
	g_object_unref(download->session);
	g_free(download);
}

static void prv_get_icon_session_cb(SoupSession *session,
				    SoupMessage *msg,
				    gpointer user_data)
{
	prv_download_info_t *download = (prv_download_info_t *)user_data;
	dld_async_task_t *cb_data = (dld_async_task_t *)download->task;
	dld_device_t *device = (dld_device_t *)cb_data->device;

	if (msg->status_code == SOUP_STATUS_CANCELLED)
		goto out;

	if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		device->icon.size = msg->response_body->length;
		device->icon.bytes = g_malloc(device->icon.size);
		memcpy(device->icon.bytes, msg->response_body->data,
		       device->icon.size);

		prv_build_icon_result(device, &cb_data->task);
	} else {
		DLEYNA_LOG_DEBUG("Failed to GET device icon: %s",
				 msg->reason_phrase);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "Failed to GET device icon");
	}

	(void) g_idle_add(dld_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

out:

	prv_free_download_info(download);
}

void dld_device_get_icon(dld_device_t *device, dld_task_t *task,
			 dld_upnp_task_complete_t cb)
{
	GUPnPDeviceInfo *info;
	dld_device_context_t *context;
	dld_async_task_t *cb_data = (dld_async_task_t *)task;
	gchar *url;
	prv_download_info_t *download;

	cb_data->cb = cb;
	cb_data->device = device;

	if (device->icon.size != 0) {
		prv_build_icon_result(device, task);
		goto end;
	}

	context = dld_device_get_context(device);
	info = (GUPnPDeviceInfo *)context->device_proxy;

	url = gupnp_device_info_get_icon_url(info, NULL, -1, -1, -1, FALSE,
					     &device->icon.mime_type, NULL,
					     NULL, NULL);
	if (url == NULL) {
		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_NOT_SUPPORTED,
					     "No icon available");
		goto end;
	}

	download = g_new0(prv_download_info_t, 1);
	download->session = soup_session_async_new();
	download->msg = soup_message_new(SOUP_METHOD_GET, url);
	download->task = cb_data;

	if (!download->msg) {
		DLEYNA_LOG_WARNING("Invalid URL %s", url);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_BAD_RESULT,
					     "Invalid URL %s", url);
		prv_free_download_info(download);
		g_free(url);

		goto end;
	}

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(prv_get_icon_cancelled),
				      download, NULL);

	g_object_ref(download->msg);
	soup_session_queue_message(download->session, download->msg,
				   prv_get_icon_session_cb, download);

	g_free(url);

	return;

end:

	(void) g_idle_add(dld_async_task_complete, cb_data);
}

static void prv_generic_test_action(dld_device_t *device, dld_task_t *task,
				    dld_upnp_task_complete_t cb,
				    const gchar *action,
				    GUPnPServiceProxyActionCallback action_cb)
{
	dld_device_context_t *context;
	dld_async_task_t *cb_data = (dld_async_task_t *)task;
	dld_task_test_t *test = &task->ut.test;
	guint test_id = test->id;

	context = dld_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dld_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->bms.proxy;

	g_object_add_weak_pointer((G_OBJECT(context->bms.proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action =
		gupnp_service_proxy_begin_action(cb_data->proxy, action,
						 action_cb, cb_data,
						 "TestID", G_TYPE_UINT, test_id,
						 NULL);
}

static void prv_get_test_info_cb(GUPnPServiceProxy *proxy,
				 GUPnPServiceProxyAction *action,
				 gpointer user_data)
{
	GError *error = NULL;
	dld_async_task_t *cb_data = user_data;
	const gchar *message;
	gchar *type = NULL;
	gchar *state = NULL;
	gboolean end;
	GVariant *out_params[2];

	DLEYNA_LOG_DEBUG("Enter");

	end = gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					     &error,
					     "Type", G_TYPE_STRING, &type,
					     "State", G_TYPE_STRING, &state,
					     NULL);
	if (!end || (type == NULL) || (state == NULL)) {
		message = (error != NULL) ? error->message : "Invalid result";
		DLEYNA_LOG_WARNING("GetTestInfo operation failed: %s",
				   message);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "GetTestInfo operation "
					     "failed: %s",
					     message);

		goto on_error;
	}

	DLEYNA_LOG_DEBUG("Result: type = %s, state = %s", type, state);

	out_params[0] = g_variant_new_string(type);
	out_params[1] = g_variant_new_string(state);

	cb_data->task.result = g_variant_ref_sink(
					g_variant_new_tuple(out_params, 2));

on_error:

	(void) g_idle_add(dld_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	g_free(type);
	g_free(state);

	if (error != NULL)
		g_error_free(error);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_get_test_info(dld_device_t *device, dld_task_t *task,
			      dld_upnp_task_complete_t cb)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action(device, task, cb,
				"GetTestInfo", prv_get_test_info_cb);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_cancel_test_cb(GUPnPServiceProxy *proxy,
			       GUPnPServiceProxyAction *action,
			       gpointer user_data)
{
	GError *error = NULL;
	dld_async_task_t *cb_data = user_data;
	const gchar *message;

	DLEYNA_LOG_DEBUG("Enter");

	if (!gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					    &error,
					    NULL)) {
		message = (error != NULL) ? error->message : "Invalid result";
		DLEYNA_LOG_WARNING("CancelTest operation failed: %s",
				   message);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "CancelTest operation "
					     "failed: %s",
					     message);
	}

	(void) g_idle_add(dld_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	if (error != NULL)
		g_error_free(error);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_cancel_test(dld_device_t *device, dld_task_t *task,
			    dld_upnp_task_complete_t cb)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action(device, task, cb,
				"CancelTest", prv_cancel_test_cb);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_generic_test_action_cb(GUPnPServiceProxy *proxy,
				       GUPnPServiceProxyAction *action,
				       gpointer user_data,
				       const gchar *action_str)
{
	GError *error = NULL;
	dld_async_task_t *cb_data = user_data;
	const gchar *message;
	guint test_id = G_MAXUINT32;
	gboolean end;

	end = gupnp_service_proxy_end_action(cb_data->proxy, cb_data->action,
					     &error,
					     "TestID", G_TYPE_UINT, &test_id,
					     NULL);
	if (!end || (test_id == G_MAXUINT32)) {
		message = (error != NULL) ? error->message : "Invalid result";
		DLEYNA_LOG_WARNING("%s operation failed: %s", action_str,
				   message);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "%s operation failed: %s",
					     action_str, message);

		goto on_error;
	}

	DLEYNA_LOG_DEBUG("Result: test ID = %u", test_id);

	cb_data->task.result = g_variant_ref_sink(
					g_variant_new_uint32(test_id));

on_error:

	(void) g_idle_add(dld_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	if (error != NULL)
		g_error_free(error);
}

static void prv_ping_cb(GUPnPServiceProxy *proxy,
			GUPnPServiceProxyAction *action,
			gpointer user_data)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action_cb(proxy, action, user_data, "Ping");

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_ping(dld_device_t *device, dld_task_t *task,
		     dld_upnp_task_complete_t cb)
{
	dld_device_context_t *context;
	dld_async_task_t *cb_data = (dld_async_task_t *)task;
	dld_task_ping_t *ping = &task->ut.ping;
	gchar *host = ping->host;
	guint repeat = ping->repeat_count;
	guint interval = ping->interval;
	guint data_block_size = ping->data_block_size;
	guint dscp = ping->dscp;

	context = dld_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dld_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->bms.proxy;

	g_object_add_weak_pointer((G_OBJECT(context->bms.proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action = gupnp_service_proxy_begin_action(
				cb_data->proxy, "Ping",
				prv_ping_cb, cb_data,
				"Host", G_TYPE_STRING, host,
				"NumberOfRepetitions", G_TYPE_UINT, repeat,
				"Timeout", G_TYPE_UINT, interval,
				"DataBlockSize", G_TYPE_UINT, data_block_size,
				"DSCP", G_TYPE_UINT, dscp,
				NULL);
}

static void prv_get_ping_result_cb(GUPnPServiceProxy *proxy,
				   GUPnPServiceProxyAction *action,
				   gpointer user_data)
{
	GError *error = NULL;
	dld_async_task_t *cb_data = user_data;
	const gchar *message;
	gchar *status = NULL;
	gchar *info = NULL;
	guint success = G_MAXUINT32;
	guint failure = G_MAXUINT32;
	guint avg_rsp_time = G_MAXUINT32;
	guint min_rsp_time = G_MAXUINT32;
	guint max_rsp_time = G_MAXUINT32;
	gboolean end;
	GVariant *out_params[7];

	DLEYNA_LOG_DEBUG("Enter");

	end = gupnp_service_proxy_end_action(
			cb_data->proxy, cb_data->action,
			&error,
			"Status", G_TYPE_STRING, &status,
			"AdditionalInfo", G_TYPE_STRING, &info,
			"SuccessCount", G_TYPE_UINT, &success,
			"FailureCount", G_TYPE_UINT, &failure,
			"AverageResponseTime", G_TYPE_UINT, &avg_rsp_time,
			"MinimumResponseTime", G_TYPE_UINT, &min_rsp_time,
			"MaximumResponseTime", G_TYPE_UINT, &max_rsp_time,
			NULL);
	if (!end || (status == NULL) || (info == NULL) ||
	    (success == G_MAXUINT32) || (failure == G_MAXUINT32) ||
	    (avg_rsp_time == G_MAXUINT32) || (min_rsp_time == G_MAXUINT32) ||
	    (max_rsp_time == G_MAXUINT32)) {
		message = (error != NULL) ? error->message : "Invalid result";
		DLEYNA_LOG_WARNING("Ping operation failed: %s",
				   message);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "Ping operation "
					     "failed: %s",
					     message);

		goto on_error;
	}

	DLEYNA_LOG_DEBUG("Result: status = %s, additional info = %s",
			 status, info);
	DLEYNA_LOG_DEBUG("Result: success = %u, failure = %u",
			 success, failure);
	DLEYNA_LOG_DEBUG("Result: avg response time = %u", avg_rsp_time);
	DLEYNA_LOG_DEBUG("Result: min response time = %u", min_rsp_time);
	DLEYNA_LOG_DEBUG("Result: max response time = %u", max_rsp_time);

	out_params[0] = g_variant_new_string(status);
	out_params[1] = g_variant_new_string(info);
	out_params[2] = g_variant_new_uint32(success);
	out_params[3] = g_variant_new_uint32(failure);
	out_params[4] = g_variant_new_uint32(avg_rsp_time);
	out_params[5] = g_variant_new_uint32(min_rsp_time);
	out_params[6] = g_variant_new_uint32(max_rsp_time);

	cb_data->task.result = g_variant_ref_sink(
					g_variant_new_tuple(out_params, 7));

on_error:

	(void) g_idle_add(dld_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	g_free(status);
	g_free(info);

	if (error != NULL)
		g_error_free(error);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_get_ping_result(dld_device_t *device, dld_task_t *task,
				dld_upnp_task_complete_t cb)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action(device, task, cb,
				"GetPingResult",
				prv_get_ping_result_cb);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_nslookup_cb(GUPnPServiceProxy *proxy,
			    GUPnPServiceProxyAction *action,
			    gpointer user_data)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action_cb(proxy, action, user_data, "NSLookup");

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_nslookup(dld_device_t *device, dld_task_t *task,
			 dld_upnp_task_complete_t cb)
{
	dld_device_context_t *context;
	dld_async_task_t *cb_data = (dld_async_task_t *)task;
	dld_task_nslookup_t *nslookup = &task->ut.nslookup;
	gchar *hostname = nslookup->hostname;
	gchar *dns_server = nslookup->dns_server;
	guint repeat = nslookup->repeat_count;
	guint interval = nslookup->interval;

	context = dld_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dld_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->bms.proxy;

	g_object_add_weak_pointer((G_OBJECT(context->bms.proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action = gupnp_service_proxy_begin_action(
				cb_data->proxy, "NSLookup",
				prv_nslookup_cb, cb_data,
				"HostName", G_TYPE_STRING, hostname,
				"DNSServer", G_TYPE_STRING, dns_server,
				"NumberOfRepetitions", G_TYPE_UINT, repeat,
				"Timeout", G_TYPE_UINT, interval,
				NULL);
}

static void prv_free_nslookup_result(prv_nslookup_result_t *result)
{
	if (result != NULL) {
		g_free(result->status);
		g_free(result->answer_type);
		g_free(result->hostname_returned);
		g_free(result->ip_addresses);
		g_free(result->dns_server_ip);
		g_free(result->response_time);

		g_free(result);
	}
}

static prv_nslookup_result_t *prv_extract_nslookup_result(xmlNode *result_node)
{
	prv_nslookup_result_t *result = NULL;

	result = g_new0(prv_nslookup_result_t, 1);

	result->status = xml_util_get_child_string_content_by_name(
						result_node,
						"Status",
						NULL);

	result->answer_type = xml_util_get_child_string_content_by_name(
						result_node,
						"AnswerType",
						NULL);

	result->hostname_returned = xml_util_get_child_string_content_by_name(
						result_node,
						"HostNameReturned",
						NULL);

	result->ip_addresses = xml_util_get_child_string_content_by_name(
						result_node,
						"IPAddresses",
						NULL);

	result->dns_server_ip = xml_util_get_child_string_content_by_name(
						result_node,
						"DNSServerIP",
						NULL);

	result->response_time = xml_util_get_child_string_content_by_name(
						result_node,
						"ResponseTime",
						NULL);

	if ((result->hostname_returned == NULL ||
	     strlen(result->hostname_returned) > 256) ||
	    (result->ip_addresses == NULL ||
	     strlen(result->ip_addresses) > 256) ||
	    (result->status == NULL) || (result->answer_type == NULL) ||
	    (result->dns_server_ip == NULL) || (result->response_time == NULL))
		goto on_error;

	return result;

on_error:
	prv_free_nslookup_result(result);

	return NULL;
}

static GList *prv_nslookup_result_decode(const gchar *nslookup_xml)
{
	xmlDoc *doc;
	xmlNode *node;
	GList *result_list = NULL;
	prv_nslookup_result_t *nslookup_result;

	DLEYNA_LOG_DEBUG("Enter");

	DLEYNA_LOG_DEBUG_NL();
	DLEYNA_LOG_DEBUG("NSLookupResult XML: %s", nslookup_xml);
	DLEYNA_LOG_DEBUG_NL();

	doc = xmlParseMemory(nslookup_xml, strlen(nslookup_xml) + 1);
	if (doc == NULL) {
		DLEYNA_LOG_WARNING("XML: invalid document");

		goto on_exit;
	}

	node = xmlDocGetRootElement(doc);
	if (node == NULL) {
		DLEYNA_LOG_WARNING("XML: empty document");

		goto on_exit;
	}

	if (node->name == NULL) {
		DLEYNA_LOG_WARNING("XML: empty document name");

		goto on_exit;
	}

	if (strcmp((char *)node->name, "NSLookupResult")) {
		DLEYNA_LOG_WARNING("XML: invalid document name");

		goto on_exit;
	}

	for (node = node->children; node; node = node->next) {
		if (node->name != NULL &&
		    !strcmp((char *)node->name, "Result")) {
			nslookup_result = prv_extract_nslookup_result(node);

			if (nslookup_result != NULL)
				result_list = g_list_prepend(result_list,
							     nslookup_result);
		}
	}

on_exit:
	DLEYNA_LOG_DEBUG("Exit");

	if (doc != NULL)
		xmlFreeDoc(doc);

	return result_list;
}

static void prv_results_list_add_result(prv_nslookup_result_t *result,
					GVariantBuilder *results_vb)
{
	GVariant *results;
	GVariantBuilder ip_addresses_vb;
	GVariant *ip_addresses_array;
	gchar **ip_addresses;
	unsigned int i = 0;

	g_variant_builder_init(&ip_addresses_vb, G_VARIANT_TYPE("as"));

	DLEYNA_LOG_DEBUG("Result: NSLookupResult");
	DLEYNA_LOG_DEBUG("-> status: %s", result->status);
	DLEYNA_LOG_DEBUG("-> answer_type: %s", result->answer_type);
	DLEYNA_LOG_DEBUG("-> hostname_returned: %s", result->hostname_returned);
	DLEYNA_LOG_DEBUG("-> result->ip_addresses: %s", result->ip_addresses);
	DLEYNA_LOG_DEBUG("-> dns_server_ip: %s", result->dns_server_ip);
	DLEYNA_LOG_DEBUG("-> response_time: %s", result->response_time);

	ip_addresses = g_strsplit(result->ip_addresses, ",", 0);
	while (ip_addresses[i]) {
		g_strstrip(ip_addresses[i]);

		g_variant_builder_add(&ip_addresses_vb, "s", ip_addresses[i]);
		++i;
	}

	ip_addresses_array = g_variant_builder_end(&ip_addresses_vb);

	g_strfreev(ip_addresses);

	g_variant_builder_add(results_vb, "(sss@assu)", result->status,
			      result->answer_type, result->hostname_returned,
			      ip_addresses_array, result->dns_server_ip,
			      atoi(result->response_time));
}

GVariant *prv_results_list_build(const gchar *nslookup_result)
{
	GList *result_list;
	GList *next;
	prv_nslookup_result_t *result;
	GVariantBuilder results_vb;

	result_list = prv_nslookup_result_decode(nslookup_result);

	g_variant_builder_init(&results_vb, G_VARIANT_TYPE("a(sssassu)"));

	next = result_list;
	while (next != NULL) {
		result = (prv_nslookup_result_t *)next->data;

		prv_results_list_add_result(result, &results_vb);

		next = g_list_next(next);
	}

	g_list_free_full(result_list, (GDestroyNotify)prv_free_nslookup_result);

	return g_variant_builder_end(&results_vb);
}

static void prv_get_nslookup_result_cb(GUPnPServiceProxy *proxy,
				       GUPnPServiceProxyAction *action,
				       gpointer user_data)
{
	GError *error = NULL;
	dld_async_task_t *cb_data = user_data;
	const gchar *message;
	gchar *status = NULL;
	gchar *info = NULL;
	guint success = G_MAXUINT32;
	gchar *nslookup_result = NULL;
	gboolean end;
	GVariant *out_params[4];

	DLEYNA_LOG_DEBUG("Enter");

	end = gupnp_service_proxy_end_action(
			  cb_data->proxy, cb_data->action,
			  &error,
			  "Status", G_TYPE_STRING, &status,
			  "AdditionalInfo", G_TYPE_STRING, &info,
			  "SuccessCount", G_TYPE_UINT, &success,
			  "Result", G_TYPE_STRING, &nslookup_result,
			  NULL);
	if (!end || (status == NULL) || (info == NULL) ||
	    (success == G_MAXUINT32) || (nslookup_result == NULL)) {
		message = (error != NULL) ? error->message : "Invalid result";
		DLEYNA_LOG_WARNING("NSLookup operation failed: %s",
				   message);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "NSLookup operation "
					     "failed: %s",
					     message);

		goto on_error;
	}

	DLEYNA_LOG_DEBUG("Result: status = %s, additional info = %s",
			 status, info);
	DLEYNA_LOG_DEBUG("Result: success count = %u", success);

	out_params[0] = g_variant_new_string(status);
	out_params[1] = g_variant_new_string(info);
	out_params[2] = g_variant_new_uint32(success);
	out_params[3] = prv_results_list_build(nslookup_result);

	cb_data->task.result = g_variant_ref_sink(
					g_variant_new_tuple(out_params, 4));

on_error:

	(void) g_idle_add(dld_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	g_free(status);
	g_free(info);
	g_free(nslookup_result);

	if (error != NULL)
		g_error_free(error);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_get_nslookup_result(dld_device_t *device, dld_task_t *task,
				    dld_upnp_task_complete_t cb)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action(device, task, cb,
				"GetNSLookupResult",
				prv_get_nslookup_result_cb);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_traceroute_cb(GUPnPServiceProxy *proxy,
			      GUPnPServiceProxyAction *action,
			      gpointer user_data)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action_cb(proxy, action, user_data, "Traceroute");

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_traceroute(dld_device_t *device, dld_task_t *task,
			   dld_upnp_task_complete_t cb)
{
	dld_device_context_t *context;
	dld_async_task_t *cb_data = (dld_async_task_t *)task;
	dld_task_traceroute_t *traceroute = &task->ut.traceroute;
	gchar *host = traceroute->host;
	guint timeout = traceroute->timeout;
	guint data_block_size = traceroute->data_block_size;
	guint max_hop_count = traceroute->max_hop_count;
	guint dscp = traceroute->dscp;

	context = dld_device_get_context(device);
	cb_data->cb = cb;
	cb_data->device = device;

	cb_data->cancel_id =
		g_cancellable_connect(cb_data->cancellable,
				      G_CALLBACK(dld_async_task_cancelled),
				      cb_data, NULL);
	cb_data->proxy = context->bms.proxy;

	g_object_add_weak_pointer((G_OBJECT(context->bms.proxy)),
				  (gpointer *)&cb_data->proxy);

	cb_data->action = gupnp_service_proxy_begin_action(
				cb_data->proxy, "Traceroute",
				prv_traceroute_cb, cb_data,
				"Host", G_TYPE_STRING, host,
				"Timeout", G_TYPE_UINT, timeout,
				"DataBlockSize", G_TYPE_UINT, data_block_size,
				"MaxHopCount", G_TYPE_UINT, max_hop_count,
				"DSCP", G_TYPE_UINT, dscp,
				NULL);
}

static void prv_get_traceroute_result_cb(GUPnPServiceProxy *proxy,
					 GUPnPServiceProxyAction *action,
					 gpointer user_data)
{
	GError *error = NULL;
	dld_async_task_t *cb_data = user_data;
	const gchar *message;
	gchar *status = NULL;
	gchar *info = NULL;
	guint rsp_time = G_MAXUINT32;
	gchar *hop_hosts = NULL;
	gboolean end;
	GVariantBuilder vb;
	gchar **parts;
	unsigned int i = 0;
	GVariant *out_params[4];

	DLEYNA_LOG_DEBUG("Enter");

	end = gupnp_service_proxy_end_action(
					cb_data->proxy, cb_data->action,
					&error,
					"Status", G_TYPE_STRING, &status,
					"AdditionalInfo", G_TYPE_STRING, &info,
					"ResponseTime", G_TYPE_UINT, &rsp_time,
					"HopHosts", G_TYPE_STRING, &hop_hosts,
					NULL);
	if (!end || (status == NULL) || (info == NULL) ||
	    (rsp_time == G_MAXUINT32) || (hop_hosts == NULL)) {
		message = (error != NULL) ? error->message : "Invalid result";
		DLEYNA_LOG_WARNING("Traceroute operation failed: %s",
				   message);

		cb_data->error = g_error_new(DLEYNA_SERVER_ERROR,
					     DLEYNA_ERROR_OPERATION_FAILED,
					     "Traceroute operation "
					     "failed: %s",
					     message);

		goto on_error;
	}

	DLEYNA_LOG_DEBUG("Result: status = %s, additional info = %s",
			 status, info);
	DLEYNA_LOG_DEBUG("Result: response time = %u", rsp_time);
	DLEYNA_LOG_DEBUG("Result: hop hosts = %s", hop_hosts);

	g_variant_builder_init(&vb, G_VARIANT_TYPE("as"));

	parts = g_strsplit(hop_hosts, ",", 0);
	while (parts[i]) {
		g_strstrip(parts[i]);

		g_variant_builder_add(&vb, "s", parts[i]);
		++i;
	}

	out_params[0] = g_variant_new_string(status);
	out_params[1] = g_variant_new_string(info);
	out_params[2] = g_variant_new_uint32(rsp_time);
	out_params[3] = g_variant_builder_end(&vb);

	cb_data->task.result = g_variant_ref_sink(
					g_variant_new_tuple(out_params, 4));

	g_strfreev(parts);

on_error:

	(void) g_idle_add(dld_async_task_complete, cb_data);
	g_cancellable_disconnect(cb_data->cancellable, cb_data->cancel_id);

	g_free(status);
	g_free(info);
	g_free(hop_hosts);

	if (error != NULL)
		g_error_free(error);

	DLEYNA_LOG_DEBUG("Exit");
}

void dld_device_get_traceroute_result(dld_device_t *device, dld_task_t *task,
				      dld_upnp_task_complete_t cb)
{
	DLEYNA_LOG_DEBUG("Enter");

	prv_generic_test_action(device, task, cb,
				"GetTracerouteResult",
				prv_get_traceroute_result_cb);

	DLEYNA_LOG_DEBUG("Exit");
}
