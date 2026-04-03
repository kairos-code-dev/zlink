/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_MONITOR_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_MONITOR_INTERNAL_HPP_INCLUDED__

#include <stdint.h>

// Internal-only SPOT monitor event bits retained for runtime implementation.
// These are no longer part of the public monitor contract.
static const uint32_t zlink_spot_monitor_event_peer_up = (1u << 2);
static const uint32_t zlink_spot_monitor_event_peer_down = (1u << 3);
static const uint32_t zlink_spot_monitor_event_error = (1u << 4);
static const uint32_t zlink_spot_monitor_event_sub_filter_applied = (1u << 13);
static const uint32_t zlink_spot_monitor_event_connection_ready = (1u << 14);
static const uint32_t zlink_spot_monitor_event_pub_queue_full = (1u << 15);
static const uint32_t zlink_spot_monitor_event_pub_queue_drained = (1u << 16);
static const uint32_t zlink_spot_monitor_event_closed = (1u << 17);

#endif
