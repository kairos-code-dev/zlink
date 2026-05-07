/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_debug.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_hwm.hpp"
#include "services/spot/spot_node_control_policy.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"
#include "api/service_api_internal.hpp"
#include "core/recv_internal.hpp"
#include "utils/sleep.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace zlink
{
namespace
{
static void spot_control_diagf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf ("ZLINK_DEBUG_SPOT_CONTROL", "[spot-control] ", fmt_,
                    args);
    va_end (args);
}

static void spot_ready_ack_debugf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_DEBUG_SPOT_READY_ACK",
                              "[spot-ready-ack] ",
                              spot_debug::ready_ack_log_path, fmt_, args);
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

    {
        scoped_lock_t lock (_sync);
        if (!_peer_state.subscription_replay_pending)
            return;
        if (_peer_state.active_endpoints.empty ())
            return;
    }

    std::set<std::string> replay_filters;
    snapshot_raw_subscription_filters (&replay_filters);

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

    if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] emit pending replay\n");
    if (send_data_plane_command (
          spot_control_protocol::cmd_replay_handle_state_subscriptions)
        != 0) {
        debug_mark_fault (errno);
        return;
    }
}

std::string spot_node_t::first_connected_peer_endpoint () const
{
    scoped_lock_t lock (_sync);
    if (_peer_state.connected_endpoints.empty ())
        return std::string ();
    return *_peer_state.connected_endpoints.begin ();
}

int spot_node_t::send_subscription_update (const std::string &raw_filter_,
                                           bool subscribe_)
{
    if (raw_filter_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return send_data_plane_command (
      subscribe_ ? spot_control_protocol::cmd_subscription_handle_state_subscribe
                 : spot_control_protocol::cmd_subscription_unsubscribe,
      raw_filter_.c_str ());
}

}
