/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_API_MESH_API_INTERNAL_HPP_INCLUDED
#define ZLINK_API_MESH_API_INTERNAL_HPP_INCLUDED

#include <zlink.h>

#include <stddef.h>

//  Internal seam between the generic public surface (common options, routing
//  id, TLS) and the mesh service module. Generic entry points classify the
//  handle here so socket paths stay independent from mesh internals.
namespace zlink
{
namespace mesh
{
enum handle_kind_t
{
    handle_none = 0,
    handle_mesh_node,
    handle_spot,
    handle_publisher,
    handle_stream_session
};

//  Returns the mesh handle kind, or handle_none when the pointer does not
//  carry a live mesh service tag. Never sets errno.
handle_kind_t classify_handle (void *handle_);

//  Generic option surface for mesh handles. All return 0 on success or -1
//  with errno set; callers translate to the public result enums.
int set_common_option (void *handle_, int option_, const void *optval_, size_t optvallen_);
int get_common_option (void *handle_, int option_, void *optval_, size_t *optvallen_);
int set_routing_id (void *handle_, const void *data_, size_t size_);
int get_routing_id (void *handle_, zlink_routing_id_t *out_);
int set_tls_server (void *handle_, const char *cert_, const char *key_, int require_client_cert_);
int set_tls_client (void *handle_, const char *ca_cert_, const char *hostname_, int trust_system_);

//  Creates a Spot-owned eventing timer bound to the Spot lifecycle
//  generation. Returns NULL with errno set on failure.
void *spot_timer_new (void *spot_);

//  Spot timer lifecycle seam, called by the generic timer machinery. All
//  three are safe no-ops for timers that are not Spot-owned.
//  enter_turn returns false when the tick must be skipped (the owning Spot
//  generation ended or the node stopped); on true the caller runs the
//  handler and then calls leave_turn. While the turn is held, the Spot's
//  application claim cannot be taken (handler mutual exclusion).
bool spot_timer_enter_turn (void *timer_);
void spot_timer_leave_turn (void *timer_);
//  Cancels a parked enter_turn wait during timer destroy so an in-flight
//  fire can finish even while the caller holds the Spot's application claim.
void spot_timer_cancel (void *timer_);
//  Queue-delivery gate (recv/poller consumption): false once the owning Spot
//  generation ended, so stale ticks are never delivered.
bool spot_timer_tick_allowed (void *timer_);
//  Releases the timer's reference on its Spot generation at destroy.
void spot_timer_closed (void *timer_);

//  Poller integration for MeshNode readiness sources.
int poller_add (void *poller_, void *handle_, void *user_data_, short events_);
int poller_modify (void *poller_, void *handle_, short events_);
int poller_remove (void *poller_, void *handle_);
}
}

#endif
