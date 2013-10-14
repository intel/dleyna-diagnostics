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

#include <libdleyna/core/error.h>
#include <libdleyna/core/task-processor.h>

#include "async.h"
#include "server.h"

dld_task_t *dld_task_rescan_new(dleyna_connector_msg_id_t invocation)
{
	dld_task_t *task = g_new0(dld_task_t, 1);

	task->type = DLD_TASK_RESCAN;
	task->invocation = invocation;
	task->synchronous = TRUE;

	return task;
}

dld_task_t *dld_task_get_version_new(dleyna_connector_msg_id_t invocation)
{
	dld_task_t *task = g_new0(dld_task_t, 1);

	task->type = DLD_TASK_GET_VERSION;
	task->invocation = invocation;
	task->result_format = "(@s)";
	task->result = g_variant_ref_sink(g_variant_new_string(VERSION));
	task->synchronous = TRUE;

	return task;
}

dld_task_t *dld_task_get_devices_new(dleyna_connector_msg_id_t invocation)
{
	dld_task_t *task = g_new0(dld_task_t, 1);

	task->type = DLD_TASK_GET_DEVICES;
	task->invocation = invocation;
	task->result_format = "(@ao)";
	task->synchronous = TRUE;

	return task;
}


static void prv_dld_task_delete(dld_task_t *task)
{
	if (!task->synchronous)
		dld_async_task_delete((dld_async_task_t *)task);

	switch (task->type) {
	case DLD_TASK_GET_ALL_PROPS:
	case DLD_TASK_MANAGER_GET_ALL_PROPS:
		g_free(task->ut.get_props.interface_name);
		break;
	case DLD_TASK_GET_PROP:
	case DLD_TASK_MANAGER_GET_PROP:
		g_free(task->ut.get_prop.interface_name);
		g_free(task->ut.get_prop.prop_name);
		break;
	case DLD_TASK_MANAGER_SET_PROP:
		g_free(task->ut.set_prop.interface_name);
		g_free(task->ut.set_prop.prop_name);
		g_variant_unref(task->ut.set_prop.params);
		break;
	case DLD_TASK_GET_ICON:
		g_free(task->ut.get_icon.mime_type);
		g_free(task->ut.get_icon.resolution);
		break;
	case DLD_TASK_GET_TEST_INFO:
		break;
	case DLD_TASK_CANCEL_TEST:
		break;
	case DLD_TASK_PING:
		g_free(task->ut.ping.host);
		break;
	case DLD_TASK_GET_PING_RESULT:
		break;
	case DLD_TASK_NSLOOKUP:
		g_free(task->ut.nslookup.hostname);
		g_free(task->ut.nslookup.dns_server);
		break;
	case DLD_TASK_GET_NSLOOKUP_RESULT:
		break;
	case DLD_TASK_TRACEROUTE:
		g_free(task->ut.traceroute.host);
		break;
	case DLD_TASK_GET_TRACEROUTE_RESULT:
		break;
	default:
		break;
	}

	g_free(task->path);
	if (task->result)
		g_variant_unref(task->result);

	g_free(task);
}

static dld_task_t *prv_device_task_new(dld_task_type_t type,
				       dleyna_connector_msg_id_t invocation,
				       const gchar *path,
				       const gchar *result_format)
{
	dld_task_t *task = (dld_task_t *)g_new0(dld_async_task_t, 1);

	task->type = type;
	task->invocation = invocation;
	task->result_format = result_format;

	task->path = g_strdup(path);
	g_strstrip(task->path);

	return task;
}

dld_task_t *dld_task_get_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_GET_PROP, invocation, path, "(v)");

	g_variant_get(parameters, "(ss)", &task->ut.get_prop.interface_name,
		      &task->ut.get_prop.prop_name);

	g_strstrip(task->ut.get_prop.interface_name);
	g_strstrip(task->ut.get_prop.prop_name);

	return task;
}

dld_task_t *dld_task_get_props_new(dleyna_connector_msg_id_t invocation,
				   const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_GET_ALL_PROPS, invocation, path,
				   "(@a{sv})");

	g_variant_get(parameters, "(s)", &task->ut.get_props.interface_name);
	g_strstrip(task->ut.get_props.interface_name);

	return task;
}

dld_task_t *dld_task_get_icon_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_GET_ICON, invocation, path,
				   "(@ays)");
	task->multiple_retvals = TRUE;

	g_variant_get(parameters, "(ss)", &task->ut.get_icon.mime_type,
		      &task->ut.get_icon.resolution);

	return task;
}

dld_task_t *dld_task_manager_get_prop_new(dleyna_connector_msg_id_t invocation,
					  const gchar *path,
					  GVariant *parameters,
					  GError **error)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_MANAGER_GET_PROP, invocation, path,
				   "(v)");

	g_variant_get(parameters, "(ss)", &task->ut.get_prop.interface_name,
		      &task->ut.get_prop.prop_name);

	g_strstrip(task->ut.get_prop.interface_name);
	g_strstrip(task->ut.get_prop.prop_name);

	return task;
}

dld_task_t *dld_task_manager_get_props_new(dleyna_connector_msg_id_t invocation,
					   const gchar *path,
					   GVariant *parameters,
					   GError **error)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_MANAGER_GET_ALL_PROPS, invocation,
				   path, "(@a{sv})");

	g_variant_get(parameters, "(s)", &task->ut.get_props.interface_name);

	g_strstrip(task->ut.get_props.interface_name);

	return task;
}

dld_task_t *dld_task_manager_set_prop_new(dleyna_connector_msg_id_t invocation,
					  const gchar *path,
					  GVariant *parameters,
					  GError **error)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_MANAGER_SET_PROP, invocation, path,
				   NULL);

	g_variant_get(parameters, "(ssv)", &task->ut.set_prop.interface_name,
		      &task->ut.set_prop.prop_name, &task->ut.set_prop.params);

	g_strstrip(task->ut.set_prop.interface_name);
	g_strstrip(task->ut.set_prop.prop_name);

	return task;
}

dld_task_t *dld_task_get_test_info_new(dleyna_connector_msg_id_t invocation,
				       const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_GET_TEST_INFO, invocation, path,
				   "(ss)");
	task->multiple_retvals = TRUE;

	g_variant_get(parameters, "(u)", &task->ut.test.id);

	return task;
}

dld_task_t *dld_task_cancel_test_new(dleyna_connector_msg_id_t invocation,
				     const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_CANCEL_TEST, invocation, path,
				   NULL);

	g_variant_get(parameters, "(u)", &task->ut.test.id);

	return task;
}

dld_task_t *dld_task_ping_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_PING, invocation, path, "(@u)");

	g_variant_get(parameters, "(suuuu)",
		      &task->ut.ping.host,
		      &task->ut.ping.repeat_count,
		      &task->ut.ping.interval,
		      &task->ut.ping.data_block_size,
		      &task->ut.ping.dscp);

	return task;
}

dld_task_t *dld_task_get_ping_result_new(dleyna_connector_msg_id_t invocation,
					 const gchar *path,
					 GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_GET_PING_RESULT, invocation, path,
				   "(ssuuuuu)");
	task->multiple_retvals = TRUE;

	g_variant_get(parameters, "(u)", &task->ut.test.id);

	return task;
}

dld_task_t *dld_task_nslookup_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_NSLOOKUP, invocation, path, "(@u)");

	g_variant_get(parameters, "(ssuu)",
		      &task->ut.nslookup.hostname,
		      &task->ut.nslookup.dns_server,
		      &task->ut.nslookup.repeat_count,
		      &task->ut.nslookup.interval);

	return task;
}

dld_task_t *dld_task_get_nslookup_result_new(
					dleyna_connector_msg_id_t invocation,
					const gchar *path,
					GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_GET_NSLOOKUP_RESULT,
				   invocation, path, "(ssua(sssassu))");
	task->multiple_retvals = TRUE;

	g_variant_get(parameters, "(u)", &task->ut.test.id);

	return task;
}

dld_task_t *dld_task_traceroute_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_TRACEROUTE,
				   invocation, path, "(@u)");

	g_variant_get(parameters, "(suuuu)",
		      &task->ut.traceroute.host,
		      &task->ut.traceroute.timeout,
		      &task->ut.traceroute.data_block_size,
		      &task->ut.traceroute.max_hop_count,
		      &task->ut.traceroute.dscp);

	return task;
}

dld_task_t *dld_task_get_traceroute_result_new(
					dleyna_connector_msg_id_t invocation,
					const gchar *path, GVariant *parameters)
{
	dld_task_t *task;

	task = prv_device_task_new(DLD_TASK_GET_TRACEROUTE_RESULT,
				   invocation, path, "(ssuas)");
	task->multiple_retvals = TRUE;

	g_variant_get(parameters, "(u)", &task->ut.test.id);

	return task;
}

void dld_task_complete(dld_task_t *task)
{
	GVariant *result;

	if (!task)
		goto finished;

	if (task->invocation) {
		if (task->result_format && task->result) {
			if (task->multiple_retvals)
				result = task->result;
			else
				result = g_variant_new(task->result_format,
						       task->result);

			g_variant_ref_sink(result);
			dld_diagnostics_get_connector()->return_response(
							task->invocation,
							result);
			g_variant_unref(result);
		} else {
			dld_diagnostics_get_connector()->return_response(
							task->invocation,
							NULL);
		}

		task->invocation = NULL;
	}

finished:

	return;
}

void dld_task_fail(dld_task_t *task, GError *error)
{
	if (!task)
		goto finished;

	if (task->invocation) {
		dld_diagnostics_get_connector()->return_error(task->invocation,
							      error);
		task->invocation = NULL;
	}

finished:

	return;
}

void dld_task_cancel(dld_task_t *task)
{
	GError *error;

	if (!task)
		goto finished;

	if (task->invocation) {
		error = g_error_new(DLEYNA_SERVER_ERROR, DLEYNA_ERROR_CANCELLED,
				    "Operation cancelled.");
		dld_diagnostics_get_connector()->return_error(task->invocation,
							      error);
		task->invocation = NULL;
		g_error_free(error);
	}

	if (!task->synchronous)
		dld_async_task_cancel((dld_async_task_t *)task);

finished:

	return;
}

void dld_task_delete(dld_task_t *task)
{
	GError *error;

	if (!task)
		goto finished;

	if (task->invocation) {
		error = g_error_new(DLEYNA_SERVER_ERROR, DLEYNA_ERROR_DIED,
				    "Unable to complete command.");
		dld_diagnostics_get_connector()->return_error(task->invocation,
							      error);
		g_error_free(error);
	}

	prv_dld_task_delete(task);

finished:

	return;
}
