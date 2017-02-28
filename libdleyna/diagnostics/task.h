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

#ifndef DLD_TASK_H__
#define DLD_TASK_H__

#include <gio/gio.h>
#include <glib.h>

#include <libdleyna/core/connector.h>
#include <libdleyna/core/task-atom.h>

enum dld_task_type_t_ {
	DLD_TASK_GET_VERSION,
	DLD_TASK_GET_DEVICES,
	DLD_TASK_RESCAN,
	DLD_TASK_GET_ALL_PROPS,
	DLD_TASK_GET_PROP,
	DLD_TASK_GET_ICON,
	DLD_TASK_MANAGER_GET_ALL_PROPS,
	DLD_TASK_MANAGER_GET_PROP,
	DLD_TASK_MANAGER_SET_PROP,
	DLD_TASK_GET_TEST_INFO,
	DLD_TASK_CANCEL_TEST,
	DLD_TASK_PING,
	DLD_TASK_GET_PING_RESULT,
	DLD_TASK_NSLOOKUP,
	DLD_TASK_GET_NSLOOKUP_RESULT,
	DLD_TASK_TRACEROUTE,
	DLD_TASK_GET_TRACEROUTE_RESULT
};
typedef enum dld_task_type_t_ dld_task_type_t;

typedef void (*dld_cancel_task_t)(void *handle);

typedef struct dld_task_get_props_t_ dld_task_get_props_t;
struct dld_task_get_props_t_ {
	gchar *interface_name;
};

typedef struct dld_task_get_prop_t_ dld_task_get_prop_t;
struct dld_task_get_prop_t_ {
	gchar *prop_name;
	gchar *interface_name;
};

typedef struct dld_task_set_prop_t_ dld_task_set_prop_t;
struct dld_task_set_prop_t_ {
	gchar *prop_name;
	gchar *interface_name;
	GVariant *params;
};

typedef struct dld_task_get_icon_t_ dld_task_get_icon_t;
struct dld_task_get_icon_t_ {
	gchar *mime_type;
	gchar *resolution;
};

typedef struct dld_task_test_t_ dld_task_test_t;
struct dld_task_test_t_ {
	guint id;
};

typedef struct dld_task_ping_t_ dld_task_ping_t;
struct dld_task_ping_t_ {
	gchar *host;
	guint repeat_count;
	guint interval;
	guint data_block_size;
	guint dscp;
};

typedef struct dld_task_nslookup_t_ dld_task_nslookup_t;
struct dld_task_nslookup_t_ {
	gchar *hostname;
	gchar *dns_server;
	guint repeat_count;
	guint interval;
};

typedef struct dld_task_traceroute_t_ dld_task_traceroute_t;
struct dld_task_traceroute_t_ {
	gchar *host;
	guint timeout;
	guint data_block_size;
	guint max_hop_count;
	guint dscp;
};

typedef struct dld_task_t_ dld_task_t;
struct dld_task_t_ {
	dleyna_task_atom_t atom; /* pseudo inheritance - MUST be first field */
	dld_task_type_t type;
	gchar *path;
	const gchar *result_format;
	GVariant *result;
	dleyna_connector_msg_id_t invocation;
	gboolean synchronous;
	gboolean multiple_retvals;
	union {
		dld_task_get_props_t get_props;
		dld_task_get_prop_t get_prop;
		dld_task_set_prop_t set_prop;
		dld_task_get_icon_t get_icon;
		dld_task_test_t test;
		dld_task_ping_t ping;
		dld_task_nslookup_t nslookup;
		dld_task_traceroute_t traceroute;
	} ut;
};

dld_task_t *dld_task_rescan_new(dleyna_connector_msg_id_t invocation);

dld_task_t *dld_task_get_version_new(dleyna_connector_msg_id_t invocation);

dld_task_t *dld_task_get_devices_new(dleyna_connector_msg_id_t invocation);

dld_task_t *dld_task_get_prop_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);

dld_task_t *dld_task_get_props_new(dleyna_connector_msg_id_t invocation,
				   const gchar *path, GVariant *parameters);

dld_task_t *dld_task_get_icon_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);

dld_task_t *dld_task_manager_get_prop_new(dleyna_connector_msg_id_t invocation,
					  const gchar *path,
					  GVariant *parameters,
					  GError **error);

dld_task_t *dld_task_manager_set_prop_new(dleyna_connector_msg_id_t invocation,
					  const gchar *path,
					  GVariant *parameters,
					  GError **error);

dld_task_t *dld_task_manager_get_props_new(dleyna_connector_msg_id_t invocation,
					   const gchar *path,
					   GVariant *parameters,
					   GError **error);

dld_task_t *dld_task_get_test_info_new(dleyna_connector_msg_id_t invocation,
				       const gchar *path, GVariant *parameters);

dld_task_t *dld_task_cancel_test_new(dleyna_connector_msg_id_t invocation,
				     const gchar *path, GVariant *parameters);

dld_task_t *dld_task_ping_new(dleyna_connector_msg_id_t invocation,
			      const gchar *path, GVariant *parameters);

dld_task_t *dld_task_get_ping_result_new(dleyna_connector_msg_id_t invocation,
					 const gchar *path,
					 GVariant *parameters);

dld_task_t *dld_task_nslookup_new(dleyna_connector_msg_id_t invocation,
				  const gchar *path, GVariant *parameters);

dld_task_t *dld_task_get_nslookup_result_new(
					dleyna_connector_msg_id_t invocation,
					const gchar *path,
					GVariant *parameters);

dld_task_t *dld_task_traceroute_new(dleyna_connector_msg_id_t invocation,
				    const gchar *path,
				    GVariant *parameters);

dld_task_t *dld_task_get_traceroute_result_new(
					dleyna_connector_msg_id_t invocation,
					const gchar *path,
					GVariant *parameters);

void dld_task_complete(dld_task_t *task);

void dld_task_fail(dld_task_t *task, GError *error);

void dld_task_delete(dld_task_t *task);

void dld_task_cancel(dld_task_t *task);

#endif
