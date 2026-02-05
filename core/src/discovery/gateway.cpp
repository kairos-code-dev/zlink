/* SPDX-License-Identifier: MPL-2.0 */

#include "discovery/gateway.hpp"

#include "core/msg.hpp"
#include "utils/random.hpp"
#include "sockets/socket_base.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <thread>

#include <zlink.h>

namespace zlink
{
namespace
{
static const uint32_t gateway_tag_value = 0x1e6700d7;

static bool gateway_debug_enabled ()
{
    return std::getenv ("ZLINK_GATEWAY_DEBUG") != NULL;
}

// Ensure the ROUTER socket has a routing id so peers can reply.
static void ensure_gateway_routing_id (socket_base_t *socket_)
{
    if (!socket_)
        return;
    unsigned char buf[256];
    size_t size = sizeof (buf);
    if (socket_->getsockopt (ZLINK_ROUTING_ID, buf, &size) != 0)
        return;
    if (size > 0)
        return;

    char id[64];
    const unsigned int r = zlink::generate_random ();
    snprintf (id, sizeof (id), "gw-%u", r);
    socket_->setsockopt (ZLINK_ROUTING_ID, id, strlen (id));
}

static int allocate_router (ctx_t *ctx_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (ZLINK_ROUTER);
    if (!*socket_)
        return -1;
    return 0;
}

// Apply TLS client settings to the ROUTER socket (optional).
static int apply_tls_client (socket_base_t *socket_,
                             const std::string &ca_cert_,
                             const std::string &hostname_,
                             int trust_system_)
{
    if (!socket_)
        return -1;
    if (ca_cert_.empty () || hostname_.empty ())
        return 0;
    if (socket_->setsockopt (ZLINK_TLS_CA, ca_cert_.data (),
                             ca_cert_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_TLS_HOSTNAME, hostname_.data (),
                             hostname_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_TLS_TRUST_SYSTEM, &trust_system_,
                             sizeof (trust_system_))
        != 0)
        return -1;
    return 0;
}

static int monitor_event_mask ()
{
    return ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED
           | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
           | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
           | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
}
}

gateway_t::gateway_t (ctx_t *ctx_, discovery_t *discovery_) :
    _ctx (ctx_),
    _discovery (discovery_),
    _tag (gateway_tag_value),
    _last_pool (NULL),
    _force_refresh_all (false),
    _monitor_socket (NULL),
    _router_socket (NULL),
    _use_lock (true),
    _stop (0),
    _tls_trust_system (0)
{
    zlink_assert (_ctx);
    if (_discovery)
        _discovery->add_observer (this);
    _refresh_worker.start (refresh_run, this, "gateway-refresh");
}

gateway_t::~gateway_t ()
{
    _tag = 0xdeadbeef;
}

bool gateway_t::check_tag () const
{
    return _tag == gateway_tag_value;
}

void gateway_t::refresh_run (void *arg_)
{
    gateway_t *self = static_cast<gateway_t *> (arg_);
    self->refresh_loop ();
}

void gateway_t::refresh_loop ()
{
    while (_stop.get () == 0) {
        std::vector<std::string> services_to_refresh;
        {
            scoped_lock_t lock (_sync);
            process_monitor_events ();
            const uint64_t now_ms = _clock.now_ms ();
            for (std::map<std::string, uint64_t>::iterator it =
                   _down_until_ms.begin ();
                 it != _down_until_ms.end ();) {
                if (now_ms >= it->second) {
                    _down_endpoints.erase (it->first);
                    it = _down_until_ms.erase (it);
                    _force_refresh_all = true;
                } else {
                    ++it;
                }
            }
            if (_discovery) {
                if (_force_refresh_all) {
                    for (std::map<std::string, service_pool_t>::iterator it =
                           _pools.begin ();
                         it != _pools.end (); ++it) {
                        it->second.dirty = true;
                        services_to_refresh.push_back (it->first);
                    }
                } else {
                    for (std::set<std::string>::iterator sit =
                           _pending_updates.begin ();
                         sit != _pending_updates.end (); ++sit) {
                        std::map<std::string, service_pool_t>::iterator pit =
                          _pools.find (*sit);
                        if (pit != _pools.end ()) {
                            pit->second.dirty = true;
                            services_to_refresh.push_back (*sit);
                        }
                    }
                }
            }
            _pending_updates.clear ();
            _force_refresh_all = false;
        }
        if (_discovery && !services_to_refresh.empty ()) {
            for (size_t i = 0; i < services_to_refresh.size (); ++i) {
                const std::string &service = services_to_refresh[i];
                std::vector<provider_info_t> providers;
                _discovery->snapshot_providers (service, &providers);
                const uint64_t seq =
                  _discovery->service_update_seq (service);
                scoped_lock_t lock (_sync);
                std::map<std::string, service_pool_t>::iterator it =
                  _pools.find (service);
                if (it == _pools.end ())
                    continue;
                if (!it->second.dirty)
                    continue;
                refresh_pool (&it->second, providers, seq);
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

int gateway_t::ensure_router_socket ()
{
    if (_router_socket)
        return 0;
    if (allocate_router (_ctx, &_router_socket) != 0)
        return -1;
    ensure_gateway_routing_id (_router_socket);
    // Enable socket monitor to receive connection-ready events.
    if (!_monitor_socket) {
        void *monitor =
          zlink_socket_monitor_open (static_cast<void *> (_router_socket),
                                     monitor_event_mask ());
        _monitor_socket = monitor;
    }
    // Apply TLS settings before connecting to any providers.
    if (apply_tls_client (_router_socket, _tls_ca, _tls_hostname,
                          _tls_trust_system)
        != 0) {
        _router_socket->close ();
        _router_socket = NULL;
        return -1;
    }
    // Fail sends when routing id is unknown (no silent drops).
    int mandatory = 1;
    _router_socket->setsockopt (ZLINK_ROUTER_MANDATORY, &mandatory,
                                sizeof (mandatory));
    // Keep send from blocking too long when caller uses blocking send.
    int sndtimeo = 50;
    _router_socket->setsockopt (ZLINK_SNDTIMEO, &sndtimeo, sizeof (sndtimeo));
    // Avoid long linger during teardown.
    int linger = 0;
    _router_socket->setsockopt (ZLINK_LINGER, &linger, sizeof (linger));
    for (size_t i = 0; i < _router_opts.size (); ++i) {
        const router_opt_t &opt = _router_opts[i];
        if (!opt.value.empty ()) {
            _router_socket->setsockopt (opt.option, &opt.value[0],
                                        opt.value.size ());
        }
    }
    return 0;
}

gateway_t::service_pool_t *
  gateway_t::get_or_create_pool (const std::string &service_name_)
{
    std::map<std::string, service_pool_t>::iterator it =
      _pools.find (service_name_);
    if (it != _pools.end ())
        return &it->second;

    service_pool_t pool;
    pool.service_name = service_name_;
    pool.rr_index = 0;
    pool.lb_strategy = ZLINK_GATEWAY_LB_ROUND_ROBIN;
    pool.last_seen_seq = 0;
    pool.dirty = true;

    if (ensure_router_socket () != 0)
        return NULL;
    _pools.insert (std::make_pair (service_name_, pool));
    if (_discovery)
        _pending_updates.insert (service_name_);
    return &_pools.find (service_name_)->second;
}

gateway_t::service_pool_t *
  gateway_t::get_or_create_pool_cached (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0')
        return NULL;
    if (_last_pool && !_last_service_name.empty ()
        && _last_service_name == service_name_) {
        return _last_pool;
    }
    std::string service (service_name_);
    service_pool_t *pool = get_or_create_pool (service);
    if (pool) {
        _last_service_name = service;
        _last_pool = pool;
    }
    return pool;
}

void gateway_t::refresh_pool (service_pool_t *pool_,
                              const std::vector<provider_info_t> &providers,
                              uint64_t seq_)
{
    if (!pool_ || !_router_socket)
        return;

    process_monitor_events ();

    // 2) Build routing_id map by endpoint for this service.
    std::vector<std::string> next_endpoints;
    std::vector<zlink_routing_id_t> next_routing_ids;
    std::map<std::string, zlink_routing_id_t> routing_map;

    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &entry = providers[i];
        routing_map[entry.endpoint] = entry.routing_id;
    }

    // 3) Connect and keep only peers that are actually ready (POLLOUT).
    for (std::map<std::string, zlink_routing_id_t>::const_iterator it =
           routing_map.begin ();
         it != routing_map.end (); ++it) {
        const std::string &endpoint = it->first;
        const zlink_routing_id_t &rid = it->second;
        if (rid.size == 0)
            continue;
        // Only attempt a new connect if not already connected.
        if (std::find (pool_->endpoints.begin (), pool_->endpoints.end (),
                       endpoint)
            == pool_->endpoints.end ()) {
            _router_socket->setsockopt (ZLINK_CONNECT_ROUTING_ID, rid.data,
                                        rid.size);
            _router_socket->connect (endpoint.c_str ());
        }
        std::map<std::string, uint64_t>::iterator dit =
          _down_until_ms.find (endpoint);
        if (dit != _down_until_ms.end ()) {
            if (_clock.now_ms () < dit->second)
                continue;
            _down_until_ms.erase (dit);
            _down_endpoints.erase (endpoint);
        }
        if (_ready_endpoints.find (endpoint) == _ready_endpoints.end ()) {
            const int state = _router_socket->get_peer_state (rid.data,
                                                             rid.size);
            if (state >= 0 && (state & ZLINK_POLLOUT)) {
                _ready_endpoints.insert (endpoint);
            } else {
                continue;
            }
        }
        next_endpoints.push_back (endpoint);
        next_routing_ids.push_back (rid);
    }

    // 4) Disconnect endpoints that disappeared or are no longer ready.
    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        const std::string &endpoint = pool_->endpoints[i];
        if (std::find (next_endpoints.begin (), next_endpoints.end (),
                       endpoint)
            == next_endpoints.end ()) {
            _router_socket->term_endpoint (endpoint.c_str ());
        }
    }

    // 5) Commit refreshed pool.
    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        _endpoint_to_service.erase (pool_->endpoints[i]);
    }
    pool_->endpoints.swap (next_endpoints);
    pool_->routing_ids.swap (next_routing_ids);
    // Track endpoint->service for monitor event routing.
    for (std::map<std::string, zlink_routing_id_t>::const_iterator it =
           routing_map.begin ();
         it != routing_map.end (); ++it) {
        _endpoint_to_service[it->first] = pool_->service_name;
    }
    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        _endpoint_to_service[pool_->endpoints[i]] = pool_->service_name;
    }
    pool_->dirty = false;
    pool_->last_seen_seq = seq_;
}

bool gateway_t::select_provider (service_pool_t *pool_, size_t *index_out_)
{
    if (!pool_ || pool_->routing_ids.empty () || !index_out_)
        return false;
    // Round-robin only (minimal policy).
    const size_t index = pool_->rr_index % pool_->routing_ids.size ();
    pool_->rr_index++;
    *index_out_ = index;
    return true;
}

bool gateway_t::find_provider_index (service_pool_t *pool_,
                                     const zlink_routing_id_t *rid_,
                                     size_t *index_out_)
{
    if (!pool_ || !rid_ || !index_out_)
        return false;
    for (size_t i = 0; i < pool_->routing_ids.size (); ++i) {
        const zlink_routing_id_t &candidate = pool_->routing_ids[i];
        if (candidate.size != rid_->size)
            continue;
        if (candidate.size == 0)
            continue;
        if (memcmp (candidate.data, rid_->data, candidate.size) == 0) {
            *index_out_ = i;
            return true;
        }
    }
    return false;
}

int gateway_t::send_request_frames (service_pool_t *pool_,
                                    size_t provider_index_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_)
{
    if (!pool_ || !_router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    if (provider_index_ >= pool_->routing_ids.size ()) {
        errno = EINVAL;
        return -1;
    }

    if (gateway_debug_enabled ()) {
        fprintf (stderr,
                 "[gateway] send_request_frames service=%s idx=%zu pool=%zu down=%zu ready=%zu\n",
                 pool_->service_name.c_str (), provider_index_,
                 pool_->routing_ids.size (), _down_endpoints.size (),
                 _ready_endpoints.size ());
        fflush (stderr);
    }

    const zlink_routing_id_t &rid = pool_->routing_ids[provider_index_];
    zlink_msg_t rid_msg;
    zlink_msg_init_size (&rid_msg, rid.size);
    if (rid.size > 0)
        memcpy (zlink_msg_data (&rid_msg), rid.data, rid.size);
    int send_flags =
      (part_count_ > 0 ? ZLINK_SNDMORE : 0) | (flags_ & ZLINK_DONTWAIT);
    if (zlink_msg_send (&rid_msg, _router_socket, send_flags) < 0) {
        if (gateway_debug_enabled ()) {
            fprintf (stderr,
                     "[gateway] send rid failed errno=%d (%s)\n",
                     errno, zlink_strerror (errno));
            fflush (stderr);
        }
        zlink_msg_close (&rid_msg);
        return -1;
    }
    zlink_msg_close (&rid_msg);

    for (size_t i = 0; i < part_count_; ++i) {
        send_flags =
          (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        send_flags |= (flags_ & ZLINK_DONTWAIT);
        if (zlink_msg_send (&parts_[i], _router_socket, send_flags) < 0) {
            if (gateway_debug_enabled ()) {
                fprintf (stderr,
                         "[gateway] send part failed errno=%d (%s)\n",
                         errno, zlink_strerror (errno));
                fflush (stderr);
            }
            return -1;
        }
        zlink_msg_close (&parts_[i]);
    }

    return 0;
}

int gateway_t::send (const char *service_name_,
                     zlink_msg_t *parts_,
                     size_t part_count_,
                     int flags_)
{
    if (!service_name_ || service_name_[0] == '\0' || !parts_
        || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    service_pool_t *pool = get_or_create_pool_cached (service_name_);
    if (!pool) {
        errno = ENOMEM;
        return -1;
    }

    size_t provider_index = 0;
    if (!select_provider (pool, &provider_index)) {
        errno = EHOSTUNREACH;
        return -1;
    }

    return send_request_frames (pool, provider_index, parts_, part_count_,
                                flags_);
}

int gateway_t::recv (zlink_msg_t **parts_,
                     size_t *part_count_,
                     int flags_,
                     char *service_name_out_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    if (ensure_router_socket () != 0 || !_router_socket) {
        errno = ENOTSUP;
        return -1;
    }

    zlink_msg_t msg;
    if (zlink_msg_init (&msg) != 0) {
        errno = EFAULT;
        return -1;
    }

    const int rc = zlink_msg_recv (&msg, _router_socket, flags_);
    if (rc != 0) {
        zlink_msg_close (&msg);
        return -1;
    }

    zlink_routing_id_t rid;
    rid.size = 0;
    const size_t rid_size = zlink_msg_size (&msg);
    if (rid_size > 0) {
        size_t copy_size = rid_size;
        if (copy_size > sizeof (rid.data))
            copy_size = sizeof (rid.data);
        rid.size = static_cast<uint8_t> (copy_size);
        memcpy (rid.data, zlink_msg_data (&msg), copy_size);
    }

    std::string service_name;
    if (rid.size > 0) {
        for (std::map<std::string, service_pool_t>::const_iterator it =
               _pools.begin ();
             it != _pools.end (); ++it) {
            const service_pool_t &pool = it->second;
            for (size_t i = 0; i < pool.routing_ids.size (); ++i) {
                const zlink_routing_id_t &candidate = pool.routing_ids[i];
                if (candidate.size != rid.size)
                    continue;
                if (candidate.size == 0)
                    continue;
                if (memcmp (candidate.data, rid.data, candidate.size) == 0) {
                    service_name = pool.service_name;
                    break;
                }
            }
            if (!service_name.empty ())
                break;
        }
    }

    if (service_name_out_) {
        memset (service_name_out_, 0, 256);
        if (!service_name.empty ())
            strncpy (service_name_out_, service_name.c_str (), 255);
    }

    const int more = zlink_msg_more (&msg);
    zlink_msg_close (&msg);

    if (!more) {
        if (parts_)
            *parts_ = NULL;
        if (part_count_)
            *part_count_ = 0;
        return 0;
    }

    std::vector<zlink_msg_t> tmp_parts;
    while (true) {
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0) {
            errno = EFAULT;
            return -1;
        }
        const int prc = zlink_msg_recv (&part, _router_socket, flags_);
        if (prc != 0) {
            zlink_msg_close (&part);
            for (size_t i = 0; i < tmp_parts.size (); ++i)
                zlink_msg_close (&tmp_parts[i]);
            return -1;
        }
        tmp_parts.push_back (part);
        if (!zlink_msg_more (&part))
            break;
    }

    const size_t out_count = tmp_parts.size ();
    zlink_msg_t *out =
      static_cast<zlink_msg_t *> (malloc (sizeof (zlink_msg_t) * out_count));
    if (!out) {
        for (size_t i = 0; i < tmp_parts.size (); ++i)
            zlink_msg_close (&tmp_parts[i]);
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < out_count; ++i) {
        if (zlink_msg_init (&out[i]) != 0
            || zlink_msg_move (&out[i], &tmp_parts[i]) != 0) {
            for (size_t j = 0; j <= i && j < out_count; ++j)
                zlink_msg_close (&out[j]);
            free (out);
            for (size_t j = i; j < tmp_parts.size (); ++j)
                zlink_msg_close (&tmp_parts[j]);
            errno = EFAULT;
            return -1;
        }
    }

    if (parts_)
        *parts_ = out;
    if (part_count_)
        *part_count_ = out_count;
    return 0;
}

int gateway_t::send_rid (const char *service_name_,
                         const zlink_routing_id_t *routing_id_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         int flags_)
{
    if (!service_name_ || service_name_[0] == '\0' || !routing_id_ || !parts_
        || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    service_pool_t *pool = get_or_create_pool_cached (service_name_);
    if (!pool) {
        errno = ENOMEM;
        return -1;
    }

    size_t provider_index = 0;
    if (!find_provider_index (pool, routing_id_, &provider_index)) {
        errno = EHOSTUNREACH;
        return -1;
    }

    return send_request_frames (pool, provider_index, parts_, part_count_,
                                flags_);
}

int gateway_t::set_lb_strategy (const char *service_name_, int strategy_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (strategy_ != ZLINK_GATEWAY_LB_ROUND_ROBIN
        && strategy_ != ZLINK_GATEWAY_LB_WEIGHTED) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    service_pool_t *pool = get_or_create_pool (service_name_);
    if (!pool)
        return -1;
    pool->lb_strategy = strategy_;
    return 0;
}

int gateway_t::set_router_option (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    bool updated = false;
    for (size_t i = 0; i < _router_opts.size (); ++i) {
        if (_router_opts[i].option == option_) {
            _router_opts[i].value.assign (
              static_cast<const unsigned char *> (optval_),
              static_cast<const unsigned char *> (optval_) + optvallen_);
            updated = true;
            break;
        }
    }
    if (!updated) {
        router_opt_t opt;
        opt.option = option_;
        opt.value.assign (static_cast<const unsigned char *> (optval_),
                          static_cast<const unsigned char *> (optval_)
                            + optvallen_);
        _router_opts.push_back (opt);
    }
    if (ensure_router_socket () != 0)
        return -1;
    if (!_router_socket) {
        errno = ENOTSUP;
        return -1;
    }
    return _router_socket->setsockopt (option_, optval_, optvallen_);
}

void gateway_t::on_service_update (const std::string &service_name_)
{
    if (_stop.get () != 0)
        return;
    scoped_lock_t lock (_sync);
    if (!service_name_.empty ()) {
        _pending_updates.insert (service_name_);
        std::map<std::string, service_pool_t>::iterator pit =
          _pools.find (service_name_);
        if (pit != _pools.end ())
            pit->second.dirty = true;
    }
}

int gateway_t::connection_count (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    std::string service (service_name_);
    bool have_discovery = false;
    {
        scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
        process_monitor_events ();

        service_pool_t *pool = get_or_create_pool (service_name_);
        if (!pool)
            return 0;
        have_discovery = (_discovery != NULL);
    }

    std::vector<provider_info_t> providers;
    uint64_t seq = 0;
    if (have_discovery) {
        _discovery->snapshot_providers (service, &providers);
        seq = _discovery->service_update_seq (service);
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    process_monitor_events ();
    service_pool_t *pool = get_or_create_pool (service_name_);
    if (!pool)
        return 0;
    if (have_discovery && seq != pool->last_seen_seq)
        pool->dirty = true;
    if (pool->dirty && have_discovery) {
        if (gateway_debug_enabled ()) {
            fprintf (stderr,
                     "[gateway] connection_count refresh service=%s\n",
                     service_name_);
            fflush (stderr);
        }
        refresh_pool (pool, providers, seq);
    }
    return static_cast<int> (pool->endpoints.size ());
}

int gateway_t::set_tls_client (const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_)
{
    if (!ca_cert_ || !hostname_) {
        errno = EINVAL;
        return -1;
    }

    scoped_optional_lock_t lock (_use_lock ? &_sync : NULL);
    _tls_ca.assign (ca_cert_);
    _tls_hostname.assign (hostname_);
    _tls_trust_system = trust_system_;

    if (ensure_router_socket () != 0)
        return -1;
    if (_router_socket
        && apply_tls_client (_router_socket, _tls_ca, _tls_hostname,
                             _tls_trust_system)
             != 0)
        return -1;
    return 0;
}

int gateway_t::destroy ()
{
    _stop.set (1);
    if (_discovery)
        _discovery->remove_observer (this);
    if (_refresh_worker.get_started ())
        _refresh_worker.stop ();
    _pools.clear ();
    _last_service_name.clear ();
    _last_pool = NULL;
    _endpoint_to_service.clear ();
    _ready_endpoints.clear ();
    _down_endpoints.clear ();
    _down_until_ms.clear ();
    _force_refresh_all = false;
    _pending_updates.clear ();
    if (_monitor_socket) {
        zlink_close (_monitor_socket);
        _monitor_socket = NULL;
    }
    if (_router_socket) {
        _router_socket->close ();
        _router_socket = NULL;
    }
    return 0;
}

void gateway_t::process_monitor_events ()
{
    if (!_monitor_socket)
        return;
    while (true) {
        zlink_monitor_event_t event;
        const int rc = zlink_monitor_recv (_monitor_socket, &event,
                                           ZLINK_DONTWAIT);
        if (rc != 0) {
            if (errno == EAGAIN)
                return;
            return;
        }
        const std::string endpoint = event.remote_addr;
        if (endpoint.empty ())
            continue;
        if (event.event == ZLINK_EVENT_CONNECTION_READY) {
            _down_endpoints.erase (endpoint);
            _down_until_ms.erase (endpoint);
            _ready_endpoints.insert (endpoint);
        } else if (event.event == ZLINK_EVENT_DISCONNECTED
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_AUTH) {
            _ready_endpoints.erase (endpoint);
            _down_endpoints.insert (endpoint);
            _down_until_ms[endpoint] = _clock.now_ms () + 500;
        }
        std::map<std::string, std::string>::iterator it =
          _endpoint_to_service.find (endpoint);
        if (it != _endpoint_to_service.end ()) {
            std::map<std::string, service_pool_t>::iterator pit =
              _pools.find (it->second);
            if (pit != _pools.end ()) {
                pit->second.dirty = true;
                _pending_updates.insert (it->second);
            }
        } else {
            _force_refresh_all = true;
        }
    }
}
}
