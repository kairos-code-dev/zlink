/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "core/ctx.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_pub.hpp"

zlink::ctx_t *zlink::spot_reqrep_internal::resolve_spot_ctx (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::ctx (spot->node);
}

zlink::spot_runtime_t *
zlink::spot_reqrep_internal::resolve_spot_runtime (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::runtime (spot->node);
}

bool zlink::spot_reqrep_internal::has_valid_routing_id (
  const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

std::string zlink::spot_reqrep_internal::routing_id_key (
  const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

bool zlink::spot_reqrep_internal::resolve_spot_identity (
  void *spot_,
  routing_pair_t *out_)
{
    if (!out_) {
        errno = EFAULT;
        return false;
    }

    if (spot_handle_t *spot = as_spot_handle (spot_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return false;

        zlink::spot_pub_t *spot_pub = ensure_spot_pub (spot);
        zlink::spot_pub_t *node_pub =
          spot->node ? spot->node->ensure_default_pub () : NULL;
        if (!spot_pub || !node_pub)
            return false;

        zlink_routing_id_t node_rid;
        zlink_routing_id_t spot_rid;
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&spot_rid, 0, sizeof (spot_rid));
        if (node_pub->routing_id (&node_rid) != 0
            || spot_pub->routing_id (&spot_rid) != 0) {
            return false;
        }

        out_->node_rid = routing_id_key (&node_rid);
        out_->spot_rid = routing_id_key (&spot_rid);
        return !out_->node_rid.empty () && !out_->spot_rid.empty ();
    }

    errno = EFAULT;
    return false;
}
