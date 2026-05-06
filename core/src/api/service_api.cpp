/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/part_helper_internal.hpp"
#include "api/service_api_internal.hpp"

#include "services/discovery/discovery_access.hpp"
#include "services/discovery/registry_access.hpp"
#include "services/spot/spot_dispatch_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

namespace
{
std::mutex g_service_handle_registry_sync;
std::set<void *> g_registered_discovery_handles;
std::set<void *> g_registered_registry_handles;
}

bool is_registered_discovery_handle (void *discovery_)
{
    std::lock_guard<std::mutex> lock (g_service_handle_registry_sync);
    return discovery_
           && g_registered_discovery_handles.find (discovery_)
                != g_registered_discovery_handles.end ();
}

bool is_registered_registry_handle (void *registry_)
{
    std::lock_guard<std::mutex> lock (g_service_handle_registry_sync);
    return registry_
           && g_registered_registry_handles.find (registry_)
                != g_registered_registry_handles.end ();
}

void register_discovery_handle (void *discovery_)
{
    if (!discovery_)
        return;
    std::lock_guard<std::mutex> lock (g_service_handle_registry_sync);
    g_registered_discovery_handles.insert (discovery_);
}

void erase_discovery_handle (void *discovery_)
{
    if (!discovery_)
        return;
    std::lock_guard<std::mutex> lock (g_service_handle_registry_sync);
    g_registered_discovery_handles.erase (discovery_);
}

void register_registry_handle (void *registry_)
{
    if (!registry_)
        return;
    std::lock_guard<std::mutex> lock (g_service_handle_registry_sync);
    g_registered_registry_handles.insert (registry_);
}

void erase_registry_handle (void *registry_)
{
    if (!registry_)
        return;
    std::lock_guard<std::mutex> lock (g_service_handle_registry_sync);
    g_registered_registry_handles.erase (registry_);
}

namespace zlink
{
extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_);

service_handle_resolution_t::service_handle_resolution_t () :
    kind (service_handle_unknown),
    discovery (NULL),
    registry (NULL)
{
}

service_handle_resolution_t resolve_service_handle (void *handle_)
{
    service_handle_resolution_t resolved;

    if (!handle_)
        return resolved;

    if (is_registered_discovery_handle (handle_)) {
        resolved.kind = service_handle_discovery;
        resolved.discovery = static_cast<discovery_t *> (handle_);
        return resolved;
    }

    if (is_registered_registry_handle (handle_)) {
        resolved.kind = service_handle_registry;
        resolved.registry = static_cast<registry_t *> (handle_);
        return resolved;
    }

    if (as_spot_pub_side_handle (handle_)) {
        resolved.kind = service_handle_spot_pub_side;
        return resolved;
    }

    if (as_spot_sub_side_handle (handle_)) {
        resolved.kind = service_handle_spot_sub_side;
        return resolved;
    }

    if (is_registered_spot_handle (handle_)) {
        resolved.kind = service_handle_spot;
        return resolved;
    }

    if (is_registered_spot_node_handle (handle_))
        resolved.kind = service_handle_spot_node;

    return resolved;
}

service_public_api_guard_t *spot_public_api_guard_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (spot)
        return &spot->public_api;
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->node () ? &pub->node ()->public_api_guard () : NULL;
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->node () ? &sub->node ()->public_api_guard () : NULL;
    return NULL;
}

void destroy_spot_handle_for_testing (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot) {
        if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_)) {
            if (pub->node ())
                pub->node ()->public_api_guard ().cancel_close ();
            int rc = pub->destroy ();
            if (rc != 0)
                rc = pub->destroy_from_node ();
            zlink_assert (rc == 0);
            delete pub;
            return;
        }
        if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_)) {
            if (sub->node ())
                sub->node ()->public_api_guard ().cancel_close ();
            int rc = sub->destroy ();
            if (rc != 0)
                rc = sub->destroy_from_node ();
            zlink_assert (rc == 0);
            delete sub;
            return;
        }
        return;
    }

    zlink_spot_request_reply_cleanup_spot (spot);
    zlink::part_helper_internal::cleanup_handle (spot);

    if (spot->logical_state && spot->logical_state->sub) {
        int rc = spot->logical_state->sub->destroy ();
        if (rc != 0)
            rc = spot->logical_state->sub->destroy_from_node ();
        zlink_assert (rc == 0);
        delete spot->logical_state->sub;
        spot->logical_state->sub = NULL;
    }
    if (spot->logical_state && spot->logical_state->pub) {
        int rc = spot->logical_state->pub->destroy ();
        if (rc != 0)
            rc = spot->logical_state->pub->destroy_from_node ();
        zlink_assert (rc == 0);
        delete spot->logical_state->pub;
        spot->logical_state->pub = NULL;
    }

    spot->tag = 0xdeadbeef;
    ::operator delete (spot);
}
}

namespace
{
int unsupported_or_fault_service_errno (void *handle_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);

    errno = resolved.kind == zlink::service_handle_unknown ? EFAULT : ENOTSUP;
    return -1;
}
}

int zlink_service_send_internal (void *handle_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_send_flags_t flags_)
{
    LIBZLINK_UNUSED (parts_);
    LIBZLINK_UNUSED (part_count_);
    LIBZLINK_UNUSED (flags_);
    return unsupported_or_fault_service_errno (handle_);
}

int zlink_service_send_rid_internal (void *handle_,
                                     const zlink_routing_id_t *target_rid_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     zlink_send_flags_t flags_)
{
    LIBZLINK_UNUSED (target_rid_);
    LIBZLINK_UNUSED (parts_);
    LIBZLINK_UNUSED (part_count_);
    LIBZLINK_UNUSED (flags_);
    return unsupported_or_fault_service_errno (handle_);
}

int zlink_service_recv_internal (void *handle_,
                                 zlink_routing_id_t *source_rid_out_,
                                 zlink_msg_t **parts_out_,
                                 size_t *part_count_out_,
                                 zlink_recv_flags_t flags_)
{
    const zlink::service_handle_resolution_t resolved =
      zlink::resolve_service_handle (handle_);
    if (resolved.kind == zlink::service_handle_spot
        || resolved.kind == zlink::service_handle_spot_node) {
        LIBZLINK_UNUSED (source_rid_out_);
        LIBZLINK_UNUSED (parts_out_);
        LIBZLINK_UNUSED (part_count_out_);
        LIBZLINK_UNUSED (flags_);
        errno = EOPNOTSUPP;
        return -1;
    }

    LIBZLINK_UNUSED (handle_);
    LIBZLINK_UNUSED (source_rid_out_);
    LIBZLINK_UNUSED (parts_out_);
    LIBZLINK_UNUSED (part_count_out_);
    LIBZLINK_UNUSED (flags_);
    errno = EFAULT;
    return -1;
}
