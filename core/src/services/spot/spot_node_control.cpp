/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_budget.hpp"
#include "services/spot/spot_node_control_policy.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"
#include "api/service_api_internal.hpp"
#include "core/recv_internal.hpp"
#include "utils/sleep.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <vector>

namespace zlink
{
namespace
{
static void spot_control_diagf (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DEBUG_SPOT_CONTROL"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-control] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    va_end (args);
}

static void spot_ready_ack_debugf (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DEBUG_SPOT_READY_ACK"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-ready-ack] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    FILE *fp = std::fopen ("/tmp/zlink_spot_ready_ack.log", "a");
    if (fp) {
        va_list file_args;
        va_start (file_args, fmt_);
        std::vfprintf (fp, fmt_, file_args);
        std::fprintf (fp, "\n");
        va_end (file_args);
        std::fclose (fp);
    }
    va_end (args);
}

}

void spot_node_t::notify_service_subscribe_readable ()
{
    // SUBSCRIBE_READABLE must reflect actual per-spot readable work.
    // Node-wide service attachment readability is too coarse and can wake
    // unrelated spots, so dispatch is now driven only by spot-owned paths.
}

void spot_node_t::emit_pending_subscription_replays ()
{
    if (is_shutting_down ()) {
        scoped_lock_t lock (_sync);
        _peer_state.subscription_replay_pending = false;
        _peer_state.subscription_replay_holdoff_ticks = 0;
        _peer_state.subscription_replay_attempts = 0;
        return;
    }

    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        if (!_peer_state.subscription_replay_pending)
            return;
        if (_peer_state.active_endpoints.empty ())
            return;
        subs.reserve (_subs.size ());
        subs.assign (_subs.begin (), _subs.end ());
    }

    std::set<std::string> replay_filters;
    spot_node_control_policy::collect_replay_raw_filters (subs,
                                                          &replay_filters);

    bool should_replay = false;
    {
        scoped_lock_t lock (_sync);
        if (!_peer_state.subscription_replay_pending)
            return;
        if (_peer_state.active_endpoints.empty ())
            return;
        if (replay_filters.empty ()) {
            _peer_state.subscription_replay_pending = false;
            _peer_state.subscription_replay_holdoff_ticks = 0;
            _peer_state.subscription_replay_attempts = 0;
            return;
        }
        if (_peer_state.subscription_replay_attempts == 0) {
            _peer_state.subscription_replay_pending = false;
            _peer_state.subscription_replay_holdoff_ticks = 0;
            return;
        }
        if (_peer_state.subscription_replay_holdoff_ticks > 0) {
            --_peer_state.subscription_replay_holdoff_ticks;
            return;
        }
        should_replay = true;
        --_peer_state.subscription_replay_attempts;
        _peer_state.subscription_replay_holdoff_ticks =
          spot_node_control_policy::subscription_replay_holdoff_ticks (
            _peer_state.connected_endpoints);
        if (_peer_state.subscription_replay_attempts == 0)
            _peer_state.subscription_replay_pending = false;
    }

    if (!should_replay)
        return;

    if (std::getenv ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] emit pending replay\n");
    if (send_data_plane_command ("replay_subscriptions") != 0) {
        debug_mark_fault (errno);
        return;
    }
}

std::string spot_node_t::first_connected_peer_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_peer_state.connected_endpoints.empty ())
        return std::string ();
    return *_peer_state.connected_endpoints.begin ();
}

std::string spot_node_t::single_peer_route_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));

    if (_peer_state.active_endpoints.size () != 1 || !_runtime) {
        return std::string ();
    }

    std::string route_endpoint;
    if (!spot_control_protocol::derive_peer_route_bind_endpoint (
          *_peer_state.active_endpoints.begin (),
          _runtime->node_id,
          &route_endpoint))
        return std::string ();
    return route_endpoint;
}

int spot_node_t::send_subscription_update (const std::string &raw_filter_,
                                           bool subscribe_)
{
    if (raw_filter_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return send_data_plane_command (
      subscribe_ ? "subscription_subscribe" : "subscription_unsubscribe",
      raw_filter_.c_str ());
}

}
