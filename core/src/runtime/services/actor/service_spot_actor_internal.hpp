/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_ACTOR_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_ACTOR_INTERNAL_HPP_INCLUDED__

namespace zlink
{
namespace spot_actor_internal
{
int node_has_any_actor (void *node_);
int set_stream_owner (void *stream_, void *node_);
}
}

#endif
