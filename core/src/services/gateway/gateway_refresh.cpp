/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/gateway/gateway_runtime.hpp"

#include <cstdlib>

namespace zlink
{
namespace
{
static const uint32_t gateway_tag_value = 0x1e6700d7;

static int resolve_gateway_refresh_sleep_ms ()
{
    static int cached = -1;
    if (cached >= 0)
        return cached;

    int value = 1;
    const char *env = getenv ("ZLINK_GATEWAY_REFRESH_SLEEP_MS");
    if (env && *env) {
        char *end = NULL;
        const long parsed = strtol (env, &end, 10);
        if (end != env && parsed >= 0 && parsed <= 1000)
            value = static_cast<int> (parsed);
    }
    cached = value;
    return cached;
}
}

gateway_service_pool_t::gateway_service_pool_t () :
    send_snapshot (std::shared_ptr<const send_snapshot_t> (new send_snapshot_t)),
    control_snapshot (std::shared_ptr<control_snapshot_t> (
      new control_snapshot_t)),
    rr_cursor (0),
    lb_strategy (ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN),
    dirty (true)
{
}

gateway_runtime_t::gateway_runtime_t (gateway_t *owner_) :
    owner (owner_),
    lifecycle (owner_ ? owner_->_ctx : NULL),
    monitor_socket (NULL),
    router_socket (NULL),
    stop (0),
    refresh_task_id (0),
    primary_pool (NULL),
    next_gateway_peer_report_ms (0),
    force_refresh_all (false)
{
}

gateway_t::gateway_t (ctx_t *ctx_,
                      const char *service_name_,
                      const char *routing_id_) :
    _ctx (ctx_),
    _discovery (NULL),
    _tag (gateway_tag_value),
    _runtime (NULL),
    _use_lock (true),
    _pollable_mode (false),
    _routing_id_locked (false),
    _service_ready_emitted (false),
    _refresh_interval_ms (
      static_cast<uint32_t> (resolve_gateway_refresh_sleep_ms ())),
    _server_weight (0),
    _last_summary_error (0),
    _summary_last_changed_ms (0),
    _tls_trust_system (0),
    _service_name (service_name_ ? service_name_ : ""),
    _routing_id_override (routing_id_ ? routing_id_ : ""),
    _handler (NULL),
    _handler_userdata (NULL),
    _send_ready_handler (NULL),
    _send_ready_handler_userdata (NULL),
    _monitor (ctx_)
{
    zlink_assert (_ctx);
    _runtime = new (std::nothrow) gateway_runtime_t (this);
    if (!_runtime) {
        errno = ENOMEM;
        _tag = 0xdeadbeef;
        return;
    }
    _routing_id.size = 0;
    if (_service_name.empty ()) {
        _tag = 0xdeadbeef;
        return;
    }
    if (init_router_socket () != 0)
        _tag = 0xdeadbeef;
    if (_tag != gateway_tag_value)
        _tag = 0xdeadbeef;
}

gateway_t::~gateway_t ()
{
    _tag = 0xdeadbeef;
    delete _runtime;
    _runtime = NULL;
}

bool gateway_t::check_tag () const
{
    return _tag == gateway_tag_value;
}

void gateway_t::refresh_task (void *arg_)
{
    gateway_t *self = static_cast<gateway_t *> (arg_);
    self->refresh_tick ();
}

void gateway_t::refresh_tick ()
{
    if (_runtime->stop.get () != 0)
        return;

    const uint64_t refresh_task_id = _runtime->refresh_task_id;
    std::vector<std::string> services_to_refresh;
    uint64_t now_ms = 0;
    {
        scoped_lock_t lock (_sync);
        process_monitor_events ();
        now_ms = _runtime->clock.now_ms ();
        for (std::map<std::string, uint64_t>::iterator it =
               _runtime->down_until_ms.begin ();
             it != _runtime->down_until_ms.end ();) {
            if (now_ms >= it->second) {
                _runtime->down_endpoints.erase (it->first);
                it = _runtime->down_until_ms.erase (it);
                _runtime->force_refresh_all = true;
            } else {
                ++it;
            }
        }
        if (_runtime->force_refresh_all) {
            for (std::map<std::string, gateway_service_pool_t>::iterator it =
                   _runtime->pools.begin ();
                 it != _runtime->pools.end (); ++it) {
                it->second.dirty = true;
                services_to_refresh.push_back (it->first);
            }
        } else {
            for (std::set<std::string>::iterator sit =
                   _runtime->pending_updates.begin ();
                 sit != _runtime->pending_updates.end (); ++sit) {
                std::map<std::string, gateway_service_pool_t>::iterator pit =
                  _runtime->pools.find (*sit);
                if (pit != _runtime->pools.end ()) {
                    pit->second.dirty = true;
                    services_to_refresh.push_back (*sit);
                }
            }
        }
        _runtime->pending_updates.clear ();
        _runtime->force_refresh_all = false;
    }
    if (!services_to_refresh.empty ()) {
        for (size_t i = 0; i < services_to_refresh.size (); ++i) {
            const std::string &service = services_to_refresh[i];
            std::vector<provider_info_t> providers;
            uint64_t seq = 0;
            if (_discovery) {
                _discovery->snapshot_providers (service, &providers);
                seq = _discovery->service_update_seq (service);
            }
            scoped_lock_t lock (_sync);
            std::map<std::string, gateway_service_pool_t>::iterator it =
              _runtime->pools.find (service);
            if (it == _runtime->pools.end ())
                continue;
            if (!it->second.dirty)
                continue;
            refresh_pool (&it->second, providers, seq);
        }
    }

    bool stop_refresh_task = false;
    {
        scoped_lock_t lock (_sync);
        sync_gateway_peer_reports (now_ms);
        stop_refresh_task =
          refresh_task_id != 0 && _runtime->refresh_task_id == refresh_task_id
          && can_suspend_refresh_task ();
        if (stop_refresh_task)
            _runtime->refresh_task_id = 0;
    }

    if (stop_refresh_task) {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime)
            (void) runtime->remove_task (refresh_task_id);
    }
}

int gateway_t::ensure_refresh_task_running ()
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }
    if (_runtime->refresh_task_id != 0)
        return 0;

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        errno = ETERM;
        return -1;
    }

    _runtime->refresh_task_id =
      runtime->add_periodic_task (refresh_task, this, _refresh_interval_ms,
                                  true);
    if (_runtime->refresh_task_id == 0)
        return -1;
    return 0;
}

bool gateway_t::can_suspend_refresh_task () const
{
    if (_discovery)
        return false;
    if (_monitor.has_watchers ())
        return false;
    if (_runtime->force_refresh_all || !_runtime->pending_updates.empty ())
        return false;
    if (!_runtime->down_until_ms.empty () || !_runtime->down_endpoints.empty ())
        return false;
    if (!_runtime->inflight_endpoints.empty ())
        return false;
    return true;
}

gateway_service_pool_t *gateway_t::get_or_create_pool (
  const std::string &service_name_)
{
    if (_runtime->primary_pool && service_name_ == _service_name)
        return _runtime->primary_pool;

    std::map<std::string, gateway_service_pool_t>::iterator it =
      _runtime->pools.find (service_name_);
    if (it != _runtime->pools.end ())
        return &it->second;

    gateway_service_pool_t pool;
    pool.service_name = service_name_;
    pool.rr_cursor = 0;
    pool.lb_strategy = ZLINK_GATEWAY_LB_STRATEGY_ROUND_ROBIN;
    pool.dirty = true;

    if (ensure_router_socket () != 0)
        return NULL;
    _runtime->pools.insert (std::make_pair (service_name_, pool));
    if (_discovery) {
        _runtime->pending_updates.insert (service_name_);
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime && _runtime->refresh_task_id != 0)
            runtime->wakeup_task (_runtime->refresh_task_id);
    }
    gateway_service_pool_t *created = &_runtime->pools.find (service_name_)->second;
    if (service_name_ == _service_name)
        _runtime->primary_pool = created;
    return created;
}

gateway_service_pool_t *gateway_t::get_or_create_pool_cached ()
{
    return _service_name.empty () ? NULL : get_or_create_pool (_service_name);
}

} // namespace zlink
