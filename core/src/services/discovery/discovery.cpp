/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"
#include "services/control/service_control_runtime.hpp"

#include "utils/err.hpp"

#include <algorithm>
#include <string.h>

namespace zlink
{
static const uint32_t discovery_tag_value = 0x1e6700d6;

discovery_t::discovery_t (ctx_t *ctx_,
                          uint16_t service_type_,
                          const std::string &service_name_) :
    _ctx (ctx_),
    _tag (discovery_tag_value),
    _lifecycle (ctx_),
    _stop (0),
    _task_id (0),
    _sub_socket (NULL),
    _bootstrap_runtime (new discovery_bootstrap_runtime_t ()),
    _uplink_runtime (new discovery_uplink_runtime_t ()),
    _monitor_ready_count (0),
    _service_type (service_type_),
    _service_name (service_name_),
    _dealer_peer_mode (ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER),
    _discovery_summary_enabled (true),
    _monitor (ctx_)
{
    zlink_assert (_ctx);
    zlink_assert (discovery_protocol::is_valid_service_type (_service_type));
    zlink_assert (_bootstrap_runtime);
    zlink_assert (_uplink_runtime);
    if (_service_name.empty ())
        _tag = 0xdeadbeef;
}

discovery_t::~discovery_t ()
{
    delete _uplink_runtime;
    _uplink_runtime = NULL;
    delete _bootstrap_runtime;
    _bootstrap_runtime = NULL;
    _tag = 0xdeadbeef;
}

bool discovery_t::check_tag () const
{
    return _tag == discovery_tag_value;
}

int discovery_t::set_dealer_peer_mode (zlink_discovery_dealer_peer_mode_t mode_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (_service_type != discovery_protocol::service_type_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (mode_ != ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER
        && mode_ != ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER) {
        errno = EINVAL;
        return -1;
    }

    bool changed = false;
    {
        scoped_lock_t lock (_sync);
        if (_dealer_peer_mode != mode_) {
            _dealer_peer_mode = mode_;
            changed = true;
        }
    }

    if (changed) {
        std::set<std::string> services;
        services.insert (_service_name);
        notify_observers (services);
    }
    return 0;
}

zlink_discovery_dealer_peer_mode_t discovery_t::dealer_peer_mode () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _dealer_peer_mode;
}

void discovery_t::emit_ready_changed (uint32_t ready_count_)
{
    scoped_lock_t lock (_sync);
    _monitor_ready_count = ready_count_;
}

void discovery_t::set_discovery_summary_enabled (bool enabled_)
{
    scoped_lock_t lock (_sync);
    _discovery_summary_enabled = enabled_;
}

socket_base_t *discovery_t::create_tracked_socket (int socket_type_)
{
    socket_base_t *socket = _ctx ? _ctx->create_socket (socket_type_) : NULL;
    if (socket)
        _lifecycle.register_socket (socket);
    return socket;
}

int discovery_t::close_tracked_socket (socket_base_t *&socket_, int timeout_ms_)
{
    return _lifecycle.close_socket (socket_, timeout_ms_);
}

int discovery_t::close_tracked_socket_and_wait (socket_base_t *&socket_,
                                                int timeout_ms_)
{
    return _lifecycle.close_socket_and_wait (socket_, timeout_ms_);
}

service_control_runtime_t *discovery_t::control_runtime () const
{
    return _ctx ? _ctx->service_control_runtime () : NULL;
}

int discovery_t::ensure_control_task_active ()
{
    service_control_runtime_t *runtime = control_runtime ();
    if (!runtime) {
        errno = ENOTSUP;
        return -1;
    }

    if (_task_id == 0) {
        _task_id = runtime->add_periodic_task (discovery_t::control_task, this, 1,
                                               true);
        return _task_id == 0 ? -1 : 0;
    }

    runtime->wakeup_task (_task_id);
    return 0;
}

}
