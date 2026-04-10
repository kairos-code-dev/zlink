/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/discovery/registry.hpp"
#include "services/control/service_control_runtime.hpp"

namespace zlink
{
static const uint32_t registry_tag_value = 0x1e6700d5;

registry_t::registry_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (registry_tag_value),
    _lifecycle (ctx_),
    _pub_endpoint (_endpoint_config.pub_endpoint),
    _router_endpoint (_endpoint_config.router_endpoint),
    _peer_pubs (_endpoint_config.peer_pubs),
    _registry_id (_coordination_state.registry_id),
    _registry_id_set (_coordination_state.registry_id_set),
    _list_seq (_coordination_state.list_seq),
    _last_summary_error (_coordination_state.last_summary_error),
    _summary_last_changed_ms (_coordination_state.summary_last_changed_ms),
    _heartbeat_interval_ms (_coordination_state.heartbeat_interval_ms),
    _heartbeat_timeout_ms (_coordination_state.heartbeat_timeout_ms),
    _broadcast_interval_ms (_coordination_state.broadcast_interval_ms),
    _pub_opts (_socket_option_state.pub_opts),
    _router_opts (_socket_option_state.router_opts),
    _peer_sub_opts (_socket_option_state.peer_sub_opts),
    _stop (_runtime_socket_state.stop),
    _task_id (_runtime_socket_state.task_id),
    _pub_socket (_runtime_socket_state.pub_socket),
    _router_socket (_runtime_socket_state.router_socket),
    _peer_sub_socket (_runtime_socket_state.peer_sub_socket),
    _peer_connected (_runtime_socket_state.peer_connected),
    _next_broadcast_ms (_runtime_socket_state.next_broadcast_ms),
    _last_sent_seq (_runtime_socket_state.last_sent_seq),
    _started (_runtime_socket_state.started),
    _next_socket_retry_ms (_runtime_socket_state.next_socket_retry_ms),
    _services (_projection_state.services),
    _topology (_projection_state.topology),
    _peer_seq (_projection_state.peer_seq),
    _peer_last_seen (_projection_state.peer_last_seen),
    _metadata_max_size (_projection_state.metadata_max_size)
{
    zlink_assert (_ctx);

    // Default to failing undeliverable registry replies instead of
    // silently dropping them on the ROUTER socket.
    socket_opt_t mandatory_opt;
    mandatory_opt.option = ZLINK_INTERNAL_OPT_ROUTER_MANDATORY;
    mandatory_opt.value.resize (sizeof (int));
    const int mandatory = 1;
    memcpy (&mandatory_opt.value[0], &mandatory, sizeof (mandatory));
    _router_opts.push_back (mandatory_opt);
}

registry_t::~registry_t ()
{
    _tag = 0xdeadbeef;
}

bool registry_t::check_tag () const
{
    return _tag == registry_tag_value;
}

int registry_t::start ()
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    {
        scoped_lock_t lock (_sync);
        if (_pub_endpoint.empty () || _router_endpoint.empty ()) {
            errno = EINVAL;
            return -1;
        }
        if (_started) {
            errno = EBUSY;
            return -1;
        }
        _stop.set (0);
        _started = true;
        _next_broadcast_ms = 0;
        _last_sent_seq = _list_seq;
    }

    // Ensure bind succeeds before reporting start success. With async-only
    // startup this could return 0 even when endpoints are busy, which leaves
    // clients blocked in register-ack waits.
    if (ensure_sockets () != 0) {
        scoped_lock_t lock (_sync);
        _started = false;
        return -1;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        errno = ENOTSUP;
        close_sockets ();
        scoped_lock_t lock (_sync);
        _started = false;
        return -1;
    }

    _task_id = runtime->add_periodic_task (control_task, this, 1, true);
    if (_task_id == 0) {
        close_sockets ();
        scoped_lock_t lock (_sync);
        _started = false;
        return -1;
    }
    return 0;
}

int registry_t::destroy ()
{
    if (!_public_api.begin_close_or_fail_busy ())
        return -1;
    _stop.set (1);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->remove_task (_task_id);
    _task_id = 0;
    close_sockets ();
    scoped_lock_t lock (_sync);
    _started = false;
    return 0;
}

void registry_t::control_task (void *arg_)
{
    registry_t *self = static_cast<registry_t *> (arg_);
    self->tick ();
}
}
