/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/registry/registry.hpp"
#include "services/control/service_control_runtime.hpp"

namespace zlink
{
static const uint32_t registry_tag_value = 0x1e6700d5;

registry_t::registry_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (registry_tag_value),
    _lifecycle (ctx_)
{
    zlink_assert (_ctx);

    // Default to failing undeliverable registry replies instead of
    // silently dropping them on the ROUTER socket.
    socket_opt_t mandatory_opt;
    mandatory_opt.option = ZLINK_INTERNAL_OPT_ROUTER_MANDATORY;
    mandatory_opt.value.resize (sizeof (int));
    const int mandatory = 1;
    memcpy (&mandatory_opt.value[0], &mandatory, sizeof (mandatory));
    _socket_option_state.router_opts.push_back (mandatory_opt);

    socket_opt_t handover_opt;
    handover_opt.option = ZLINK_INTERNAL_OPT_ROUTER_HANDOVER;
    handover_opt.value.resize (sizeof (int));
    const int handover = 1;
    memcpy (&handover_opt.value[0], &handover, sizeof (handover));
    _socket_option_state.router_opts.push_back (handover_opt);
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
        if (_endpoint_config.pub_endpoint.empty () || _endpoint_config.router_endpoint.empty ()) {
            errno = EINVAL;
            return -1;
        }
        if (_runtime_socket_state.started) {
            errno = EBUSY;
            return -1;
        }
        _runtime_socket_state.stop.set (0);
        _runtime_socket_state.started = true;
        _runtime_socket_state.next_broadcast_ms = 0;
        _runtime_socket_state.last_sent_seq = _coordination_state.list_seq;
    }

    // Ensure bind succeeds before reporting start success. With async-only
    // startup this could return 0 even when endpoints are busy, which leaves
    // clients blocked in register-ack waits.
    if (ensure_sockets () != 0) {
        scoped_lock_t lock (_sync);
        _runtime_socket_state.started = false;
        return -1;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        errno = ENOTSUP;
        close_sockets ();
        scoped_lock_t lock (_sync);
        _runtime_socket_state.started = false;
        return -1;
    }

    _runtime_socket_state.task_id = runtime->add_periodic_task (control_task, this, 1, true);
    if (_runtime_socket_state.task_id == 0) {
        close_sockets ();
        scoped_lock_t lock (_sync);
        _runtime_socket_state.started = false;
        return -1;
    }
    return 0;
}

int registry_t::destroy ()
{
    if (!_public_api.begin_close_or_fail_busy ())
        return -1;
    _runtime_socket_state.stop.set (1);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _runtime_socket_state.task_id != 0)
        runtime->remove_task (_runtime_socket_state.task_id);
    _runtime_socket_state.task_id = 0;
    close_sockets ();
    scoped_lock_t lock (_sync);
    _runtime_socket_state.started = false;
    return 0;
}

void registry_t::control_task (void *arg_)
{
    registry_t *self = static_cast<registry_t *> (arg_);
    self->tick ();
}
}
