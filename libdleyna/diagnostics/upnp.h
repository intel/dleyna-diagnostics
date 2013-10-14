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

#ifndef DLD_UPNP_H__
#define DLD_UPNP_H__

#include <libgupnp/gupnp-context-manager.h>
#include <libdleyna/core/connector.h>

#include "server.h"
#include "task.h"

enum dld_interface_type_ {
	DLD_INTERFACE_INFO_PROPERTIES,
	DLD_INTERFACE_INFO_DEVICE,
	DLD_INTERFACE_INFO_MAX
};

typedef void (*dld_upnp_callback_t)(const gchar *path);
typedef void (*dld_upnp_task_complete_t)(dld_task_t *task, GError *error);

dld_upnp_t *dld_upnp_new(dleyna_connector_id_t connection,
			 const dleyna_connector_dispatch_cb_t *dispatch_table,
			 dld_upnp_callback_t found_device,
			 dld_upnp_callback_t lost_device);

void dld_upnp_delete(dld_upnp_t *upnp);

GVariant *dld_upnp_get_device_ids(dld_upnp_t *upnp);

GHashTable *dld_upnp_get_device_udn_map(dld_upnp_t *upnp);

void dld_upnp_get_prop(dld_upnp_t *upnp, dld_task_t *task,
		       dld_upnp_task_complete_t cb);

void dld_upnp_get_all_props(dld_upnp_t *upnp, dld_task_t *task,
			    dld_upnp_task_complete_t cb);

void dld_upnp_get_icon(dld_upnp_t *upnp, dld_task_t *task,
		       dld_upnp_task_complete_t cb);

void dld_upnp_get_test_info(dld_upnp_t *upnp, dld_task_t *task,
			    dld_upnp_task_complete_t cb);

void dld_upnp_cancel_test(dld_upnp_t *upnp, dld_task_t *task,
			  dld_upnp_task_complete_t cb);

void dld_upnp_ping(dld_upnp_t *upnp, dld_task_t *task,
		   dld_upnp_task_complete_t cb);

void dld_upnp_get_ping_result(dld_upnp_t *upnp, dld_task_t *task,
			      dld_upnp_task_complete_t cb);

void dld_upnp_nslookup(dld_upnp_t *upnp, dld_task_t *task,
		       dld_upnp_task_complete_t cb);

void dld_upnp_get_nslookup_result(dld_upnp_t *upnp, dld_task_t *task,
				  dld_upnp_task_complete_t cb);

void dld_upnp_traceroute(dld_upnp_t *upnp, dld_task_t *task,
			 dld_upnp_task_complete_t cb);

void dld_upnp_get_traceroute_result(dld_upnp_t *upnp, dld_task_t *task,
				    dld_upnp_task_complete_t cb);

void dld_upnp_unsubscribe(dld_upnp_t *upnp);

void dld_upnp_rescan(dld_upnp_t *upnp);

GUPnPContextManager *dld_upnp_get_context_manager(dld_upnp_t *upnp);

#endif /* DLD_UPNP_H__ */
