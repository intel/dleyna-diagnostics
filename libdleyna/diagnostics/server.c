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


#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <libdleyna/core/connector.h>
#include <libdleyna/core/control-point.h>
#include <libdleyna/core/error.h>
#include <libdleyna/core/log.h>
#include <libdleyna/core/task-processor.h>
#include <libdleyna/core/white-list.h>

#include "async.h"
#include "control-point-diagnostics.h"
#include "device.h"
#include "manager.h"
#include "prop-defs.h"
#include "server.h"
#include "upnp.h"

#ifdef UA_PREFIX
	#define DLD_PRG_NAME UA_PREFIX " dLeyna/" VERSION
#else
	#define DLD_PRG_NAME "dLeyna/" VERSION
#endif

#define DLD_INTERFACE_GET_VERSION "GetVersion"
#define DLD_INTERFACE_GET_DEVICES "GetDevices"
#define DLD_INTERFACE_RESCAN "Rescan"
#define DLD_INTERFACE_RELEASE "Release"

#define DLD_INTERFACE_FOUND_DEVICE "FoundDevice"
#define DLD_INTERFACE_LOST_DEVICE "LostDevice"

#define DLD_INTERFACE_VERSION "Version"
#define DLD_INTERFACE_DEVICES "Devices"

#define DLD_INTERFACE_PATH "Path"

#define DLD_INTERFACE_CHANGED_PROPERTIES "changed_properties"
#define DLD_INTERFACE_INVALIDATED_PROPERTIES "invalidated_properties"
#define DLD_INTERFACE_GET "Get"
#define DLD_INTERFACE_GET_ALL "GetAll"
#define DLD_INTERFACE_SET "Set"
#define DLD_INTERFACE_INTERFACE_NAME "interface_name"
#define DLD_INTERFACE_PROPERTY_NAME "property_name"
#define DLD_INTERFACE_PROPERTIES_VALUE "properties"
#define DLD_INTERFACE_VALUE "value"

#define DLD_INTERFACE_CANCEL "Cancel"
#define DLD_INTERFACE_GET_ICON "GetIcon"
#define DLD_INTERFACE_RESOLUTION "Resolution"
#define DLD_INTERFACE_ICON_BYTES "Bytes"
#define DLD_INTERFACE_MIME_TYPE "MimeType"
#define DLD_INTERFACE_REQ_MIME_TYPE "RequestedMimeType"

#define DLD_INTERFACE_GET_TEST_INFO "GetTestInfo"
#define DLD_INTERFACE_CANCEL_TEST "CancelTest"
#define DLD_INTERFACE_PING "Ping"
#define DLD_INTERFACE_GET_PING_RESULT "GetPingResult"
#define DLD_INTERFACE_NSLOOKUP "NSLookup"
#define DLD_INTERFACE_GET_NSLOOKUP_RESULT "GetNSLookupResult"
#define DLD_INTERFACE_TRACEROUTE "Traceroute"
#define DLD_INTERFACE_GET_TRACEROUTE_RESULT "GetTracerouteResult"
#define DLD_INTERFACE_TEST_ID "TestId"
#define DLD_INTERFACE_TEST_TYPE "TestType"
#define DLD_INTERFACE_TEST_STATE "TestState"
#define DLD_INTERFACE_HOST "Host"
#define DLD_INTERFACE_REPEAT_COUNT "RepeatCount"
#define DLD_INTERFACE_INTERVAL "Interval"
#define DLD_INTERFACE_DATA_BLOCK_SIZE "DataBlockSize"
#define DLD_INTERFACE_DSCP "Dscp"
#define DLD_INTERFACE_HOSTNAME "HostName"
#define DLD_INTERFACE_DNS_SERVER "DNSServer"
#define DLD_INTERFACE_MAX_HOP_COUNT "MaxHopCount"
#define DLD_INTERFACE_TIMEOUT "TimeOut"
#define DLD_INTERFACE_TEST_STATUS "TestStatus"
#define DLD_INTERFACE_TEST_INFO "TestInfo"
#define DLD_INTERFACE_SUCCESS_COUNT "SuccessCount"
#define DLD_INTERFACE_FAILURE_COUNT "FailureCount"
#define DLD_INTERFACE_AVG_RESPONSE_TIME "AvgResponseTime"
#define DLD_INTERFACE_MIN_RESPONSE_TIME "MinResponseTime"
#define DLD_INTERFACE_MAX_RESPONSE_TIME "MaxResponseTime"
#define DLD_INTERFACE_NSLOOKUP_RESULT "NSLookupResult"
#define DLD_INTERFACE_RESPONSE_TIME "ResponseTime"
#define DLD_INTERFACE_HOP_HOSTS "HopHosts"

enum dld_manager_interface_type_ {
	DLD_MANAGER_INTERFACE_MANAGER,
	DLD_MANAGER_INTERFACE_INFO_PROPERTIES,
	DLD_MANAGER_INTERFACE_INFO_MAX
};

typedef struct dld_context_t_ dld_context_t;
struct dld_context_t_ {
	guint dld_id[DLD_MANAGER_INTERFACE_INFO_MAX];
	dleyna_connector_id_t connection;
	guint watchers;
	dleyna_task_processor_t *processor;
	const dleyna_connector_t *connector;
	dld_upnp_t *upnp;
	dleyna_settings_t *settings;
	dld_manager_t *manager;
};

static dld_context_t g_context;

static const gchar g_root_introspection[] =
	"<node>"
	"  <interface name='"DLEYNA_DIAGNOSTICS_INTERFACE_MANAGER"'>"
	"    <method name='"DLD_INTERFACE_GET_VERSION"'>"
	"      <arg type='s' name='"DLD_INTERFACE_VERSION"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_RELEASE"'>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_DEVICES"'>"
	"      <arg type='ao' name='"DLD_INTERFACE_DEVICES"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_RESCAN"'>"
	"    </method>"
	"    <signal name='"DLD_INTERFACE_FOUND_DEVICE"'>"
	"      <arg type='o' name='"DLD_INTERFACE_PATH"'/>"
	"    </signal>"
	"    <signal name='"DLD_INTERFACE_LOST_DEVICE"'>"
	"      <arg type='o' name='"DLD_INTERFACE_PATH"'/>"
	"    </signal>"
	"    <property type='as' name='"DLD_INTERFACE_PROP_NEVER_QUIT"'"
	"       access='readwrite'/>"
	"    <property type='as' name='"DLD_INTERFACE_PROP_WHITE_LIST_ENTRIES"'"
	"       access='readwrite'/>"
	"    <property type='b' name='"DLD_INTERFACE_PROP_WHITE_LIST_ENABLED"'"
	"       access='readwrite'/>"
	"  </interface>"
	"  <interface name='"DLD_INTERFACE_PROPERTIES"'>"
	"    <method name='"DLD_INTERFACE_GET"'>"
	"      <arg type='s' name='"DLD_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"DLD_INTERFACE_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_SET"'>"
	"      <arg type='s' name='"DLD_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"DLD_INTERFACE_VALUE"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_ALL"'>"
	"      <arg type='s' name='"DLD_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='a{sv}' name='"DLD_INTERFACE_PROPERTIES_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <signal name='"DLD_INTERFACE_PROPERTIES_CHANGED"'>"
	"      <arg type='s' name='"DLD_INTERFACE_INTERFACE_NAME"'/>"
	"      <arg type='a{sv}' name='"DLD_INTERFACE_CHANGED_PROPERTIES"'/>"
	"      <arg type='as' name='"DLD_INTERFACE_INVALIDATED_PROPERTIES"'/>"
	"    </signal>"
	"  </interface>"
	"</node>";

static const gchar g_diagnostics_device_introspection[] =
	"<node>"
	"  <interface name='"DLD_INTERFACE_PROPERTIES"'>"
	"    <method name='"DLD_INTERFACE_GET"'>"
	"      <arg type='s' name='"DLD_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_PROPERTY_NAME"'"
	"           direction='in'/>"
	"      <arg type='v' name='"DLD_INTERFACE_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_ALL"'>"
	"      <arg type='s' name='"DLD_INTERFACE_INTERFACE_NAME"'"
	"           direction='in'/>"
	"      <arg type='a{sv}' name='"DLD_INTERFACE_PROPERTIES_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <signal name='"DLD_INTERFACE_PROPERTIES_CHANGED"'>"
	"      <arg type='s' name='"DLD_INTERFACE_INTERFACE_NAME"'/>"
	"      <arg type='a{sv}' name='"DLD_INTERFACE_CHANGED_PROPERTIES"'/>"
	"      <arg type='as' name='"DLD_INTERFACE_INVALIDATED_PROPERTIES"'/>"
	"    </signal>"
	"  </interface>"
	"  <interface name='"DLEYNA_DIAGNOSTICS_INTERFACE_DEVICE"'>"
	"    <method name='"DLD_INTERFACE_CANCEL"'>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_ICON"'>"
	"      <arg type='s' name='"DLD_INTERFACE_REQ_MIME_TYPE"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_RESOLUTION"'"
	"           direction='in'/>"
	"      <arg type='ay' name='"DLD_INTERFACE_ICON_BYTES"'"
	"           direction='out'/>"
	"      <arg type='s' name='"DLD_INTERFACE_MIME_TYPE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_TEST_INFO"'>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_TYPE"'"
	"           direction='out'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_STATE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_CANCEL_TEST"'>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_PING"'>"
	"      <arg type='s' name='"DLD_INTERFACE_HOST"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_REPEAT_COUNT"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_INTERVAL"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_DATA_BLOCK_SIZE"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_DSCP"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_PING_RESULT"'>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_STATUS"'"
	"           direction='out'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_INFO"'"
	"           direction='out'/>"
	"      <arg type='u' name='"DLD_INTERFACE_SUCCESS_COUNT"'"
	"           direction='out'/>"
	"      <arg type='u' name='"DLD_INTERFACE_FAILURE_COUNT"'"
	"           direction='out'/>"
	"      <arg type='u' name='"DLD_INTERFACE_AVG_RESPONSE_TIME"'"
	"           direction='out'/>"
	"      <arg type='u' name='"DLD_INTERFACE_MIN_RESPONSE_TIME"'"
	"           direction='out'/>"
	"      <arg type='u' name='"DLD_INTERFACE_MAX_RESPONSE_TIME"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_NSLOOKUP"'>"
	"      <arg type='s' name='"DLD_INTERFACE_HOSTNAME"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_DNS_SERVER"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_REPEAT_COUNT"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_INTERVAL"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_NSLOOKUP_RESULT"'>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_STATUS"'"
	"           direction='out'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_INFO"'"
	"           direction='out'/>"
	"      <arg type='u' name='"DLD_INTERFACE_SUCCESS_COUNT"'"
	"           direction='out'/>"
	"      <arg type='a(sssassu)' name='"DLD_INTERFACE_NSLOOKUP_RESULT"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_TRACEROUTE"'>"
	"      <arg type='s' name='"DLD_INTERFACE_HOST"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_TIMEOUT"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_DATA_BLOCK_SIZE"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_MAX_HOP_COUNT"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_DSCP"'"
	"           direction='in'/>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"DLD_INTERFACE_GET_TRACEROUTE_RESULT"'>"
	"      <arg type='u' name='"DLD_INTERFACE_TEST_ID"'"
	"           direction='in'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_STATUS"'"
	"           direction='out'/>"
	"      <arg type='s' name='"DLD_INTERFACE_TEST_INFO"'"
	"           direction='out'/>"
	"      <arg type='u' name='"DLD_INTERFACE_RESPONSE_TIME"'"
	"           direction='out'/>"
	"      <arg type='as' name='"DLD_INTERFACE_HOP_HOSTS"'"
	"           direction='out'/>"
	"    </method>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_DEVICE_TYPE"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_UDN"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_FRIENDLY_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_ICON_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_MANUFACTURER"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_MANUFACTURER_URL"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_MODEL_DESCRIPTION"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_MODEL_NAME"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_MODEL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_SERIAL_NUMBER"'"
	"       access='read'/>"
	"    <property type='s' name='"DLD_INTERFACE_PROP_PRESENTATION_URL"'"
	"       access='read'/>"
	"    <property type='as' name='"DLD_INTERFACE_PROP_STATUS_INFO"'"
	"       access='read'/>"
	"    <property type='au' name='"DLD_INTERFACE_PROP_TEST_IDS"'"
	"       access='read'/>"
	"    <property type='au' name='"DLD_INTERFACE_PROP_ACTIVE_TEST_IDS"'"
	"       access='read'/>"
	"  </interface>"
	"</node>";

static const gchar *g_manager_interfaces[DLD_MANAGER_INTERFACE_INFO_MAX] = {
	/* MUST be in the exact same order as g_root_introspection */
	DLEYNA_DIAGNOSTICS_INTERFACE_MANAGER,
	DLD_INTERFACE_PROPERTIES
};

static void prv_process_task(dleyna_task_atom_t *task, gpointer user_data);

static void prv_manager_root_method_call(dleyna_connector_id_t conn,
					 const gchar *sender,
					 const gchar *object,
					 const gchar *interface,
					 const gchar *method,
					 GVariant *parameters,
					 dleyna_connector_msg_id_t invocation);

static void prv_manager_props_method_call(dleyna_connector_id_t conn,
					  const gchar *sender,
					  const gchar *object,
					  const gchar *interface,
					  const gchar *method,
					  GVariant *parameters,
					  dleyna_connector_msg_id_t invocation);

static void prv_props_method_call(dleyna_connector_id_t conn,
				  const gchar *sender,
				  const gchar *object,
				  const gchar *interface,
				  const gchar *method,
				  GVariant *parameters,
				  dleyna_connector_msg_id_t invocation);

static void prv_device_method_call(dleyna_connector_id_t conn,
				   const gchar *sender,
				   const gchar *object,
				   const gchar *interface,
				   const gchar *method,
				   GVariant *parameters,
				   dleyna_connector_msg_id_t invocation);

static const dleyna_connector_dispatch_cb_t
			g_root_vtables[DLD_MANAGER_INTERFACE_INFO_MAX] = {
	/* MUST be in the exact same order as g_root_introspection */
	prv_manager_root_method_call,
	prv_manager_props_method_call
};

static const dleyna_connector_dispatch_cb_t
				g_server_vtables[DLD_INTERFACE_INFO_MAX] = {
	/* MUST be in the same order as g_diagnostics_device_introspection */
	prv_props_method_call,
	prv_device_method_call
};

static const gchar *g_server_interfaces[DLD_INTERFACE_INFO_MAX] = {
	/* MUST be in the exact same order as g_server_introspection */
	DLD_INTERFACE_PROPERTIES,
	DLEYNA_DIAGNOSTICS_INTERFACE_DEVICE
};

const gchar *dld_diagnostics_get_interface_name(guint index)
{
	return g_server_interfaces[index];
}


const dleyna_connector_t *dld_diagnostics_get_connector(void)
{
	return g_context.connector;
}

dleyna_task_processor_t *dld_diagnostics_service_get_task_processor(void)
{
	return g_context.processor;
}

dld_upnp_t *dld_diagnostics_service_get_upnp(void)
{
	return g_context.upnp;
}

static void prv_process_sync_task(dld_task_t *task)
{
	GError *error;

	switch (task->type) {
	case DLD_TASK_GET_VERSION:
		task->result = g_variant_ref_sink(g_variant_new_string(
								VERSION));
		dld_task_complete(task);
		break;
	case DLD_TASK_GET_DEVICES:
		task->result = dld_upnp_get_device_ids(g_context.upnp);
		dld_task_complete(task);
		break;
	case DLD_TASK_RESCAN:
		dld_upnp_rescan(g_context.upnp);
		dld_task_complete(task);
		break;
	default:
		goto finished;
		break;
	}

	dleyna_task_queue_task_completed(task->atom.queue_id);

finished:
	return;
}

static void prv_async_task_complete(dld_task_t *task, GError *error)
{
	DLEYNA_LOG_DEBUG("Enter");

	if (error) {
		dld_task_fail(task, error);
		g_error_free(error);
	} else {
		dld_task_complete(task);
	}

	dleyna_task_queue_task_completed(task->atom.queue_id);

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_process_async_task(dld_task_t *task)
{
	dld_async_task_t *async_task = (dld_async_task_t *)task;

	DLEYNA_LOG_DEBUG("Enter");

	async_task->cancellable = g_cancellable_new();

	switch (task->type) {
	case DLD_TASK_GET_PROP:
		dld_upnp_get_prop(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLD_TASK_GET_ALL_PROPS:
		dld_upnp_get_all_props(g_context.upnp, task,
				       prv_async_task_complete);
		break;
	case DLD_TASK_GET_ICON:
		dld_upnp_get_icon(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLD_TASK_MANAGER_GET_PROP:
		dld_manager_get_prop(g_context.manager, g_context.settings,
				     task, prv_async_task_complete);
		break;
	case DLD_TASK_MANAGER_GET_ALL_PROPS:
		dld_manager_get_all_props(g_context.manager, g_context.settings,
					  task, prv_async_task_complete);
		break;
	case DLD_TASK_MANAGER_SET_PROP:
		dld_manager_set_prop(g_context.manager, g_context.settings,
				     task, prv_async_task_complete);
		break;
	case DLD_TASK_GET_TEST_INFO:
		dld_upnp_get_test_info(g_context.upnp, task,
				       prv_async_task_complete);
		break;
	case DLD_TASK_CANCEL_TEST:
		dld_upnp_cancel_test(g_context.upnp, task,
				     prv_async_task_complete);
		break;
	case DLD_TASK_PING:
		dld_upnp_ping(g_context.upnp, task,
			      prv_async_task_complete);
		break;
	case DLD_TASK_GET_PING_RESULT:
		dld_upnp_get_ping_result(g_context.upnp, task,
					 prv_async_task_complete);
		break;
	case DLD_TASK_NSLOOKUP:
		dld_upnp_nslookup(g_context.upnp, task,
				  prv_async_task_complete);
		break;
	case DLD_TASK_GET_NSLOOKUP_RESULT:
		dld_upnp_get_nslookup_result(g_context.upnp, task,
					     prv_async_task_complete);
		break;
	case DLD_TASK_TRACEROUTE:
		dld_upnp_traceroute(g_context.upnp, task,
				    prv_async_task_complete);
		break;
	case DLD_TASK_GET_TRACEROUTE_RESULT:
		dld_upnp_get_traceroute_result(g_context.upnp, task,
					       prv_async_task_complete);
		break;
	default:
		break;
	}

	DLEYNA_LOG_DEBUG("Exit");
}

static void prv_process_task(dleyna_task_atom_t *task, gpointer user_data)
{
	dld_task_t *client_task = (dld_task_t *)task;

	if (client_task->synchronous)
		prv_process_sync_task(client_task);
	else
		prv_process_async_task(client_task);
}

static void prv_cancel_task(dleyna_task_atom_t *task, gpointer user_data)
{
	dld_task_cancel((dld_task_t *)task);
}

static void prv_delete_task(dleyna_task_atom_t *task, gpointer user_data)
{
	dld_task_delete((dld_task_t *)task);
}

static void prv_remove_client(const gchar *name)
{
	dleyna_task_processor_remove_queues_for_source(g_context.processor,
						       name);

	g_context.watchers--;
	if (g_context.watchers == 0)
		if (!dleyna_settings_is_never_quit(g_context.settings))
			dleyna_task_processor_set_quitting(g_context.processor);
}

static void prv_lost_client(const gchar *name)
{
	DLEYNA_LOG_INFO("Client %s lost", name);
	prv_remove_client(name);
}

static void prv_control_point_initialize(const dleyna_connector_t *connector,
					 dleyna_task_processor_t *processor,
					 dleyna_settings_t *settings)
{
	memset(&g_context, 0, sizeof(g_context));

	g_context.processor = processor;
	g_context.settings = settings;
	g_context.connector = connector;
	g_context.connector->set_client_lost_cb(prv_lost_client);

	g_set_prgname(DLD_PRG_NAME);
}

static void prv_control_point_stop_service(void)
{
	uint i;

	if (g_context.upnp) {
		dld_upnp_unsubscribe(g_context.upnp);
		dld_upnp_delete(g_context.upnp);
	}

	if (g_context.connection) {
		for (i = 0; i < DLD_MANAGER_INTERFACE_INFO_MAX; i++)
			if (g_context.dld_id[i])
				g_context.connector->unpublish_object(
							g_context.connection,
							g_context.dld_id[i]);
	}
}

static void prv_control_point_free(void)
{
}

static void prv_add_task(dld_task_t *task, const gchar *source,
			 const gchar *sink)
{
	const dleyna_task_queue_key_t *queue_id;

	if (g_context.connector->watch_client(source))
		g_context.watchers++;

	queue_id = dleyna_task_processor_lookup_queue(g_context.processor,
						      source, sink);
	if (!queue_id)
		queue_id = dleyna_task_processor_add_queue(
					g_context.processor,
					source,
					sink,
					DLEYNA_TASK_QUEUE_FLAG_AUTO_START,
					prv_process_task,
					prv_cancel_task,
					prv_delete_task);

	dleyna_task_queue_add_task(queue_id, &task->atom);
}

static void prv_manager_root_method_call(dleyna_connector_id_t conn,
				const gchar *sender, const gchar *object,
				const gchar *interface,
				const gchar *method, GVariant *parameters,
				dleyna_connector_msg_id_t invocation)
{
	dld_task_t *task;

	DLEYNA_LOG_INFO("Calling %s method", method);

	if (!strcmp(method, DLD_INTERFACE_RELEASE)) {
		g_context.connector->unwatch_client(sender);
		prv_remove_client(sender);
		g_context.connector->return_response(invocation, NULL);

		goto finished;
	} else  {
		if (!strcmp(method, DLD_INTERFACE_GET_VERSION))
			task = dld_task_get_version_new(invocation);
		else if (!strcmp(method, DLD_INTERFACE_GET_DEVICES))
			task = dld_task_get_devices_new(invocation);
		else if (!strcmp(method, DLD_INTERFACE_RESCAN))
			task = dld_task_rescan_new(invocation);
		else
			goto finished;
	}

	prv_add_task(task, sender, DLD_DIAGNOSTICS_SINK);

finished:
	return;
}

static void prv_manager_props_method_call(dleyna_connector_id_t conn,
					  const gchar *sender,
					  const gchar *object,
					  const gchar *interface,
					  const gchar *method,
					  GVariant *parameters,
					  dleyna_connector_msg_id_t invocation)
{
	dld_task_t *task;
	GError *error = NULL;

	if (!strcmp(method, DLD_INTERFACE_GET_ALL))
		task = dld_task_manager_get_props_new(invocation, object,
						      parameters, &error);
	else if (!strcmp(method, DLD_INTERFACE_GET))
		task = dld_task_manager_get_prop_new(invocation, object,
						     parameters, &error);
	else if (!strcmp(method, DLD_INTERFACE_SET))
		task = dld_task_manager_set_prop_new(invocation, object,
						     parameters, &error);
	else
		goto finished;

	if (!task) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	prv_add_task(task, sender, task->path);

finished:

	return;
}

static const gchar *prv_get_device_id(const gchar *object, GError **error)
{
	dld_device_t *device;

	device = dld_device_from_path(object,
				dld_upnp_get_device_udn_map(g_context.upnp));


	if (!device) {
		DLEYNA_LOG_WARNING("Cannot locate device for %s", object);

		*error = g_error_new(DLEYNA_SERVER_ERROR,
				     DLEYNA_ERROR_OBJECT_NOT_FOUND,
				     "Cannot locate device corresponding to the specified path");
		goto on_error;
	}

	return device->path;

on_error:

	return NULL;
}

static void prv_props_method_call(dleyna_connector_id_t conn,
				  const gchar *sender,
				  const gchar *object,
				  const gchar *interface,
				  const gchar *method,
				  GVariant *parameters,
				  dleyna_connector_msg_id_t invocation)
{
	dld_task_t *task;
	const gchar *device_id;
	GError *error = NULL;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, DLD_INTERFACE_GET_ALL))
		task = dld_task_get_props_new(invocation, object, parameters);
	else if (!strcmp(method, DLD_INTERFACE_GET))
		task = dld_task_get_prop_new(invocation, object, parameters);
	else
		goto finished;

	prv_add_task(task, sender, device_id);

finished:

	return;
}

static void prv_device_method_call(dleyna_connector_id_t conn,
				   const gchar *sender,
				   const gchar *object,
				   const gchar *interface,
				   const gchar *method,
				   GVariant *parameters,
				   dleyna_connector_msg_id_t invocation)
{
	dld_task_t *task;
	const gchar *device_id = NULL;
	GError *error = NULL;
	const dleyna_task_queue_key_t *queue_id;

	device_id = prv_get_device_id(object, &error);
	if (!device_id) {
		g_context.connector->return_error(invocation, error);
		g_error_free(error);

		goto finished;
	}

	if (!strcmp(method, DLD_INTERFACE_CANCEL)) {
		queue_id = dleyna_task_processor_lookup_queue(
							g_context.processor,
							sender, device_id);
		if (queue_id)
			dleyna_task_processor_cancel_queue(queue_id);

		g_context.connector->return_response(invocation, NULL);
	} else if (!strcmp(method, DLD_INTERFACE_GET_ICON)) {
		task = dld_task_get_icon_new(invocation, object, parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_GET_TEST_INFO)) {
		task = dld_task_get_test_info_new(invocation, object,
						  parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_CANCEL_TEST)) {
		task = dld_task_cancel_test_new(invocation, object,
						parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_PING)) {
		task = dld_task_ping_new(invocation, object, parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_GET_PING_RESULT)) {
		task = dld_task_get_ping_result_new(invocation, object,
						    parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_NSLOOKUP)) {
		task = dld_task_nslookup_new(invocation, object, parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_GET_NSLOOKUP_RESULT)) {
		task = dld_task_get_nslookup_result_new(invocation, object,
							parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_TRACEROUTE)) {
		task = dld_task_traceroute_new(invocation, object, parameters);
		prv_add_task(task, sender, device_id);
	} else if (!strcmp(method, DLD_INTERFACE_GET_TRACEROUTE_RESULT)) {
		task = dld_task_get_traceroute_result_new(invocation, object,
							  parameters);
		prv_add_task(task, sender, device_id);
	}

finished:

	return;
}

static void prv_found_diagnostics_device(const gchar *path)
{
	DLEYNA_LOG_INFO("New Diagnostics Device: %s", path);

	(void) g_context.connector->notify(g_context.connection,
					   DLEYNA_DIAGNOSTICS_OBJECT,
					   DLEYNA_DIAGNOSTICS_INTERFACE_MANAGER,
					   DLD_INTERFACE_FOUND_DEVICE,
					   g_variant_new("(o)", path),
					   NULL);
}

static void prv_lost_diagnostics_device(const gchar *path)
{
	DLEYNA_LOG_INFO("Lost: %s", path);

	(void) g_context.connector->notify(g_context.connection,
					   DLEYNA_DIAGNOSTICS_OBJECT,
					   DLEYNA_DIAGNOSTICS_INTERFACE_MANAGER,
					   DLD_INTERFACE_LOST_DEVICE,
					   g_variant_new("(o)", path),
					   NULL);

	dleyna_task_processor_remove_queues_for_sink(g_context.processor, path);
}

static void prv_white_list_init(void)
{
	gboolean enabled;
	GVariant *entries;
	dleyna_white_list_t *wl;

	DLEYNA_LOG_DEBUG("Enter");

	enabled = dleyna_settings_is_white_list_enabled(g_context.settings);
	entries = dleyna_settings_white_list_entries(g_context.settings);

	wl = dld_manager_get_white_list(g_context.manager);

	dleyna_white_list_enable(wl, enabled);
	dleyna_white_list_add_entries(wl, entries);

	DLEYNA_LOG_DEBUG("Exit");
}

static gboolean prv_control_point_start_service(
					dleyna_connector_id_t connection)
{
	gboolean retval = TRUE;
	uint i;

	g_context.connection = connection;

	for (i = 0; i < DLD_MANAGER_INTERFACE_INFO_MAX; i++)
		g_context.dld_id[i] = g_context.connector->publish_object(
						connection,
						DLEYNA_DIAGNOSTICS_OBJECT,
						TRUE,
						g_manager_interfaces[i],
						g_root_vtables + i);

	if (g_context.dld_id[DLD_MANAGER_INTERFACE_MANAGER]) {
		g_context.upnp = dld_upnp_new(connection,
					     g_server_vtables,
					     prv_found_diagnostics_device,
					     prv_lost_diagnostics_device);

		g_context.manager = dld_manager_new(connection,
			       dld_upnp_get_context_manager(g_context.upnp));

		prv_white_list_init();
	} else {
		retval = FALSE;
	}

	return retval;
}

static const gchar *prv_control_point_diagnostics_device_name(void)
{
	return DLEYNA_DIAGNOSTICS_NAME;
}

static const gchar *prv_control_point_diagnostics_device_introspection(void)
{
	return g_diagnostics_device_introspection;
}

static const gchar *prv_control_point_root_introspection(void)
{
	return g_root_introspection;
}

static const gchar *prv_control_point_get_version(void)
{
	return VERSION;
}

static const dleyna_control_point_t g_control_point = {
	prv_control_point_initialize,
	prv_control_point_free,
	prv_control_point_diagnostics_device_name,
	prv_control_point_diagnostics_device_introspection,
	prv_control_point_root_introspection,
	prv_control_point_start_service,
	prv_control_point_stop_service,
	prv_control_point_get_version
};

const dleyna_control_point_t *dleyna_control_point_get(void)
{
	return &g_control_point;
}

