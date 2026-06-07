/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_MODE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_MODE_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

#include "api/monitoring/poller_api_internal.hpp"

struct spot_handle_t;

namespace zlink
{
class spot_node_t;
}

int spot_node_transition_to_callback_mode (zlink::spot_node_t *node_);
void spot_node_revert_callback_transition (zlink::spot_node_t *node_);
int spot_transition_to_callback_mode (spot_handle_t *spot_);
void spot_revert_callback_transition (spot_handle_t *spot_);
int spot_node_require_recv_model (zlink::spot_node_t *node_);
int spot_require_recv_model (spot_handle_t *spot_);
bool in_spot_dispatch_event_callback (void *spot_);
int spot_install_dispatch_event_sub_handler (spot_handle_t *spot_);
int spot_activate_send_ready_mode (spot_handle_t *spot_, bool *already_active_out_);
void spot_revert_send_ready_mode (spot_handle_t *spot_);
int spot_node_activate_send_ready_mode (zlink::spot_node_t *node_, bool *already_active_out_);
void spot_node_revert_send_ready_mode (zlink::spot_node_t *node_);
int increment_spot_node_poller_ref (zlink::spot_node_t *node_);
int increment_spot_node_poller_ref (zlink::spot_node_t *node_, short events_);
void decrement_spot_node_poller_ref (zlink::spot_node_t *node_);
void decrement_spot_node_poller_ref (zlink::spot_node_t *node_, short events_);
int increment_spot_poller_ref (spot_handle_t *spot_);
int increment_spot_poller_ref (spot_handle_t *spot_, short events_);
void decrement_spot_poller_ref (spot_handle_t *spot_);
void decrement_spot_poller_ref (spot_handle_t *spot_, short events_);
int validate_recv_flags (int flags_);
int validate_spot_generic_poller_events (short events_, bool *is_pub_out_);
void release_poller_registration (const poller_registration_t &registration_);
int increment_spot_subject_poller_ref (void *spot_or_node_, short events_);
poller_subject_kind_t poller_spot_pub_kind_for_subject (void *spot_or_node_);
poller_subject_kind_t poller_spot_sub_kind_for_subject (void *spot_or_node_);

#endif
