/*
 * dLeyna
 *
 * Copyright (C) 2013-2017 Intel Corporation. All rights reserved.
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
 * Ludovic Ferrandis <ludovic.ferrandis@intel.com>
 *
 */

#ifndef DLD_MANAGER_H__
#define DLD_MANAGER_H__

#include <libdleyna/core/connector.h>
#include <libdleyna/core/settings.h>
#include <libgupnp/gupnp-context-manager.h>

#include "task.h"

typedef struct dld_manager_t_ dld_manager_t;
typedef void (*dld_manager_task_complete_t)(dld_task_t *task, GError *error);

dld_manager_t *dld_manager_new(dleyna_connector_id_t connection,
			       GUPnPContextManager *connection_manager);

void dld_manager_delete(dld_manager_t *manager);

dleyna_white_list_t *dld_manager_get_white_list(dld_manager_t *manager);

void dld_manager_get_all_props(dld_manager_t *manager,
			       dleyna_settings_t *settings,
			       dld_task_t *task,
			       dld_manager_task_complete_t cb);

void dld_manager_get_prop(dld_manager_t *manager,
			  dleyna_settings_t *settings,
			  dld_task_t *task,
			  dld_manager_task_complete_t cb);

void dld_manager_set_prop(dld_manager_t *manager,
			  dleyna_settings_t *settings,
			  dld_task_t *task,
			  dld_manager_task_complete_t cb);

#endif /* DLD_MANAGER_H__ */
