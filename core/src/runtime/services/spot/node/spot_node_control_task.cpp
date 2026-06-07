/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/node/spot_node_control_policy.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include <cstdio>

namespace zlink
{
namespace
{
const int spot_control_task_interval_ms = 10;

static void spot_control_diagf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf ("ZLINK_DEBUG_SPOT_CONTROL", "[spot-control] ", fmt_, args);
    va_end (args);
}
}

int spot_node_t::replay_subscriptions_if_active_peers ()
{
    if (is_shutting_down ())
        return 0;
    if (ensure_healthy () != 0)
        return -1;
    if (!has_local_filtered_subs ())
        return 0;
    if (!has_active_peers ())
        return 0;
    if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] immediate replay request\n");
    if (send_data_plane_command (spot_control_protocol::cmd_replay_handle_state_subscriptions) != 0)
        return -1;
    queue_all_subscription_ready_filters ();
    return 0;
}

void spot_node_t::schedule_subscription_replay ()
{
    if (is_shutting_down ())
        return;

    (void) apply_service_subscription_filters ();

    unsigned int attempts = 0;
    {
        scoped_lock_t lock (_sync);
        _peer_state.subscription_replay_pending = true;
        const unsigned int target_attempts =
          spot_node_control_policy::subscription_replay_attempt_count (
            _peer_state.active_endpoints);
        if (_peer_state.subscription_replay_attempts < target_attempts)
            _peer_state.subscription_replay_attempts = target_attempts;
        _peer_state.subscription_replay_holdoff_ticks = 0;
        attempts = _peer_state.subscription_replay_attempts;
    }
    if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] scheduled attempts=%u\n", attempts);
    wake_control_task ();
}

void spot_node_t::control_task (void *arg_)
{
    static_cast<spot_node_t *> (arg_)->control_tick ();
}

int spot_node_t::ensure_control_task_running ()
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (_runtime->control_task_id () != 0)
            return 0;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        errno = ETERM;
        return -1;
    }

    const uint64_t task_id =
      runtime->add_periodic_task (control_task, this, spot_control_task_interval_ms, true);
    if (task_id == 0)
        return -1;

    spot_control_diagf ("ensure-control-task node=%p scheduled=%llu", this,
                        static_cast<unsigned long long> (task_id));

    if (!_runtime->try_set_control_task_id (task_id))
        (void) runtime->remove_task (task_id);
    return 0;
}

void spot_node_t::wake_control_task ()
{
    if (!_runtime || _runtime->stop.get () != 0)
        return;

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime)
        return;
    if (ensure_control_task_running () != 0)
        return;

    const uint64_t task_id = _runtime->control_task_id ();
    if (task_id != 0)
        runtime->wakeup_task (task_id);
}

bool spot_node_t::can_suspend_control_task () const
{
    if (_discovery_state.discovery != NULL)
        return false;
    if (!_service_attachment_state.discoveries.empty ())
        return false;
    for (std::map<std::string, spot_node_router_channel_peer_state_t>::const_iterator it =
           _service_attachment_state.router_channel_peers.begin ();
         it != _service_attachment_state.router_channel_peers.end (); ++it) {
        if (it->second.discovery)
            return false;
    }
    if (!_discovery_state.pending_service_updates.empty ())
        return false;
    if (!_service_attachment_state.pending_router_channel_refreshes.empty ())
        return false;
    if (_peer_state.subscription_replay_pending || _peer_state.subscription_ready_refresh_pending
        || _peer_state.pub_delivery_ready_refresh_pending) {
        return false;
    }
    if (!_runtime)
        return false;
    return mesh_peer_version (&_runtime->execution.mesh_peer_state)
           == _runtime->connected_peer_version_seen ();
}

void spot_node_t::control_tick ()
{
    if (!_runtime || _runtime->stop.get () != 0)
        return;
    spot_control_diagf ("control-tick node=%p", this);
    if (ensure_healthy () != 0)
        return;

    bool has_pending_service_refresh = false;
    bool has_pending_router_channel_refresh = false;
    {
        scoped_lock_t lock (_sync);
        has_pending_service_refresh = !_service_attachment_state.pending_refresh_services.empty ();
        has_pending_router_channel_refresh =
          !_service_attachment_state.pending_router_channel_refreshes.empty ();
    }
    if (has_pending_service_refresh)
        refresh_service_discovery_attachments ();
    if (has_pending_router_channel_refresh)
        refresh_router_channel_discovery_peers ();
    refresh_discovery_peers ();
    bool skip_extra = false;
    {
        scoped_lock_t lock (_sync);
        const uint64_t connected_peer_version =
          mesh_peer_version (&_runtime->execution.mesh_peer_state);
        skip_extra = _discovery_state.discovery == NULL
                     && connected_peer_version == _runtime->connected_peer_version_seen ()
                     && !_peer_state.subscription_replay_pending
                     && !_peer_state.subscription_ready_refresh_pending
                     && !_peer_state.pub_delivery_ready_refresh_pending;
    }
    if (!skip_extra) {
        refresh_connected_peer_endpoints ();
        emit_pending_subscription_replays ();
        emit_pending_subscription_ready_events ();
        emit_pending_pub_delivery_ready_events ();
    }
    notify_service_subscribe_readable ();

    spot_runtime_t *runtime_state = _runtime;
    const uint64_t task_id =
      runtime_state && can_suspend_control_task () ? runtime_state->clear_control_task_id () : 0;

    if (task_id != 0) {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime)
            (void) runtime->remove_task (task_id);
    }
}
}
