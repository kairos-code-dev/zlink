/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SPOT_MESH_PEER_STATE_HPP_INCLUDED
#define ZLINK_SPOT_MESH_PEER_STATE_HPP_INCLUDED

#include <zlink.h>

#include "utils/mutex.hpp"

#include <atomic>
#include <set>
#include <string>

namespace zlink
{
struct spot_mesh_peer_state_t
{
    spot_mesh_peer_state_t () :
        version (0), hwm_version (0), mesh_pub_ready_peer_count (0), connected_ready_peer_count (0)
    {
    }

    mutable mutex_t sync;
    std::atomic<uint64_t> version;
    std::atomic<uint64_t> hwm_version;
    std::atomic<uint32_t> mesh_pub_ready_peer_count;
    std::atomic<uint32_t> connected_ready_peer_count;
    std::set<std::string> connected_endpoints;
};

inline bool sync_monitor_ready_endpoint (std::set<std::string> *endpoints_,
                                         const zlink_monitor_event_t &raw_)
{
    if (!endpoints_ || raw_.remote_addr[0] == '\0')
        return false;
    if (raw_.event == ZLINK_EVENT_DISCONNECTED)
        return endpoints_->erase (raw_.remote_addr) != 0;
    if (raw_.event != ZLINK_EVENT_CONNECTION_READY)
        return false;
    return endpoints_->insert (raw_.remote_addr).second;
}

inline bool sync_monitor_connected_endpoint (std::set<std::string> *endpoints_,
                                             const zlink_monitor_event_t &raw_)
{
    if (!endpoints_ || raw_.remote_addr[0] == '\0')
        return false;
    if (raw_.event == ZLINK_EVENT_DISCONNECTED)
        return endpoints_->erase (raw_.remote_addr) != 0;
    if (raw_.event == ZLINK_EVENT_CONNECTION_READY)
        return endpoints_->insert (raw_.remote_addr).second;
    return false;
}

inline bool apply_monitor_ready_peer_count (uint32_t *ready_peer_count_out_, size_t endpoint_count_)
{
    if (!ready_peer_count_out_)
        return false;
    const uint32_t next_ready_count = static_cast<uint32_t> (endpoint_count_);
    if (*ready_peer_count_out_ == next_ready_count)
        return false;
    *ready_peer_count_out_ = next_ready_count;
    return true;
}

inline uint32_t clamp_ready_peer_count (uint32_t ready_count_, uint32_t connected_peer_count_)
{
    return ready_count_ < connected_peer_count_ ? ready_count_ : connected_peer_count_;
}

inline void snapshot_connected_mesh_peer_endpoints (const spot_mesh_peer_state_t *state_,
                                                    std::set<std::string> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (!state_)
        return;
    scoped_lock_t lock (state_->sync);
    *out_ = state_->connected_endpoints;
}

inline bool sync_mesh_peer_monitor_state (spot_mesh_peer_state_t *state_,
                                          const zlink_monitor_event_t &raw_,
                                          bool *endpoint_membership_changed_out_ = NULL)
{
    if (!state_ || raw_.remote_addr[0] == '\0')
        return false;

    scoped_lock_t lock (state_->sync);
    bool endpoint_membership_changed =
      sync_monitor_connected_endpoint (&state_->connected_endpoints, raw_);
    bool changed = endpoint_membership_changed;
    if (raw_.event == ZLINK_EVENT_DISCONNECTED && state_->connected_endpoints.empty ()
        && state_->connected_ready_peer_count.load (std::memory_order_acquire) != 0) {
        state_->connected_ready_peer_count.store (0, std::memory_order_release);
        changed = true;
    }

    uint32_t ready_peer_count = state_->connected_ready_peer_count.load (std::memory_order_acquire);
    if (apply_monitor_ready_peer_count (&ready_peer_count, state_->connected_endpoints.size ())) {
        state_->connected_ready_peer_count.store (ready_peer_count, std::memory_order_release);
        changed = true;
    }

    if (changed)
        state_->version.fetch_add (1, std::memory_order_acq_rel);
    if (endpoint_membership_changed_out_)
        *endpoint_membership_changed_out_ = endpoint_membership_changed;
    return changed;
}

inline bool clear_mesh_peer_monitor_state (spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return false;

    scoped_lock_t lock (state_->sync);
    if (!state_->connected_endpoints.empty ()) {
        state_->connected_endpoints.clear ();
        state_->connected_ready_peer_count.store (0, std::memory_order_release);
        state_->version.fetch_add (1, std::memory_order_acq_rel);
        return true;
    }
    state_->connected_ready_peer_count.store (0, std::memory_order_release);
    return false;
}

inline bool remove_connected_mesh_peer_endpoint (spot_mesh_peer_state_t *state_,
                                                 const std::string &endpoint_)
{
    if (!state_ || endpoint_.empty ())
        return false;

    scoped_lock_t lock (state_->sync);
    const std::set<std::string>::iterator it = state_->connected_endpoints.find (endpoint_);
    if (it == state_->connected_endpoints.end ())
        return false;

    state_->connected_endpoints.erase (it);
    if (state_->connected_endpoints.empty ())
        state_->connected_ready_peer_count.store (0, std::memory_order_release);
    state_->version.fetch_add (1, std::memory_order_acq_rel);
    return true;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

inline uint32_t connected_ready_peer_count (const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->connected_ready_peer_count.load (std::memory_order_acquire);
}

inline uint32_t mesh_pub_ready_peer_count (const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->mesh_pub_ready_peer_count.load (std::memory_order_acquire);
}

inline uint64_t mesh_peer_version (const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->version.load (std::memory_order_acquire);
}

inline uint64_t mesh_pub_hwm_version (const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->hwm_version.load (std::memory_order_acquire);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

inline void reset_mesh_pub_hwm_state (spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return;

    state_->mesh_pub_ready_peer_count.store (0, std::memory_order_release);
    state_->hwm_version.fetch_add (1, std::memory_order_acq_rel);
}
}

#endif
