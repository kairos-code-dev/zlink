/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_POLLER_SURFACE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_POLLER_SURFACE_INTERNAL_HPP_INCLUDED__

#include "api/monitoring/poller_api_internal.hpp"

int zlink_service_poller_add_internal (poller_handle_t *poller_,
                                       void *socket_,
                                       void *user_data_,
                                       short events_);
int zlink_service_poller_modify_internal (poller_handle_t *poller_, void *socket_, short events_);
int zlink_service_poller_remove_internal (poller_handle_t *poller_, void *socket_);

#endif
