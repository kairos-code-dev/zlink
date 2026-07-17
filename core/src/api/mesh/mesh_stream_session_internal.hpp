/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_API_MESH_STREAM_SESSION_INTERNAL_HPP_INCLUDED
#define ZLINK_API_MESH_STREAM_SESSION_INTERNAL_HPP_INCLUDED

#include "services/mesh/mesh_runtime.hpp"

namespace zlink
{
namespace mesh
{
//  True while a STREAM session service retains the raw socket handle.
bool stream_session_owns_socket (void *socket_);

//  Actor-destroy support. Both walk every session service attached to
//  node_ and match bindings by actor id + generation. Callers must not hold
//  the node mutex (the service mutex is taken independently).
//  True while any binding to actor_ still carries queued barrier control.
bool session_bindings_pending (mesh_node_t *node_, const zlink_actor_ref_t &actor_);
//  Drops every binding to actor_ so no session keeps addressing the
//  destroyed generation.
void session_bindings_remove_actor (mesh_node_t *node_, const zlink_actor_ref_t &actor_);
}
}

#endif
