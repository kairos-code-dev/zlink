/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "discovery/gateway.hpp"

#include "utils/err.hpp"
#include "utils/clock.hpp"
#include "utils/random.hpp"
#include "sockets/socket_base.hpp"

#if defined ZLINK_HAVE_WINDOWS
#include "utils/windows.hpp"
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <inttypes.h>
#include <string.h>
#include <cstdlib>

namespace zlink
{
static const uint32_t gateway_tag_value = 0x1e6700d7;

static bool gateway_debug_enabled ()
{
    return std::getenv ("ZLINK_GATEWAY_DEBUG") != NULL;
}

static void dump_gateway_peers (socket_base_t *socket_)
{
    if (!gateway_debug_enabled () || !socket_)
        return;
    size_t count = 0;
    if (socket_->socket_peers (NULL, &count) != 0 || count == 0) {
        fprintf (stderr, "gateway peers: <none>\n");
        return;
    }
    std::vector<zlink_peer_info_t> peers;
    peers.resize (count);
    if (socket_->socket_peers (&peers[0], &count) != 0) {
        fprintf (stderr, "gateway peers: <error>\n");
        return;
    }
    fprintf (stderr, "gateway peers: %zu\n", count);
    for (size_t i = 0; i < count; ++i) {
        const zlink_peer_info_t &info = peers[i];
        fprintf (stderr,
                 "gateway peer[%zu] rid_size=%u remote=%s sent=%" PRIu64
                 " recv=%" PRIu64 " rid=0x",
                 i, static_cast<unsigned> (info.routing_id.size),
                 info.remote_addr, static_cast<uint64_t> (info.msgs_sent),
                 static_cast<uint64_t> (info.msgs_received));
        for (uint8_t j = 0; j < info.routing_id.size; ++j)
            fprintf (stderr, "%02x",
                     static_cast<unsigned> (info.routing_id.data[j]));
        fprintf (stderr, "\n");
    }
}

static bool lookup_peer_rid (socket_base_t *socket_,
                             const std::string &endpoint_,
                             zlink_routing_id_t *rid_out_)
{
    if (!socket_ || !rid_out_)
        return false;
    size_t count = 0;
    if (socket_->socket_peers (NULL, &count) != 0 || count == 0)
        return false;
    std::vector<zlink_peer_info_t> peers;
    peers.resize (count);
    if (socket_->socket_peers (&peers[0], &count) != 0)
        return false;
    for (size_t i = 0; i < count; ++i) {
        const zlink_peer_info_t &info = peers[i];
        if (!info.remote_addr)
            continue;
        if (endpoint_ != info.remote_addr)
            continue;
        if (info.routing_id.size == 0)
            continue;
        *rid_out_ = info.routing_id;
        return true;
    }
    return false;
}

static void sleep_ms (int ms_)
{
#if defined ZLINK_HAVE_WINDOWS
    Sleep (ms_);
#else
    usleep (static_cast<useconds_t> (ms_) * 1000);
#endif
}

static void ensure_gateway_routing_id (socket_base_t *socket_)
{
    if (!socket_)
        return;
    unsigned char buf[5];
    size_t size = sizeof (buf);
    if (socket_->getsockopt (ZLINK_ROUTING_ID, buf, &size) == 0 && size > 0)
        return;

    uint32_t random_id = generate_random ();
    if (random_id == 0)
        random_id = 1;
    buf[0] = 0;
    memcpy (buf + 1, &random_id, sizeof (random_id));
    socket_->setsockopt (ZLINK_ROUTING_ID, buf, sizeof (buf));
}

static bool wait_for_peer_ready (socket_base_t *socket_,
                                 const zlink_routing_id_t &rid_,
                                 int timeout_ms_)
{
    if (!socket_)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      timeout_ms_ > 0 ? std::chrono::steady_clock::now ()
                            + std::chrono::milliseconds (timeout_ms_)
                      : std::chrono::steady_clock::time_point ();

    while (true) {
        const int state =
          socket_->get_peer_state (rid_.data, rid_.size);
        if (state >= 0) {
            if (state & ZLINK_POLLOUT)
                return true;
        } else if (errno != EHOSTUNREACH) {
            return false;
        }
        if (timeout_ms_ == 0)
            return false;
        if (timeout_ms_ > 0
            && std::chrono::steady_clock::now () >= deadline)
            return false;
        sleep_ms (1);
    }
}

gateway_t::gateway_t (ctx_t *ctx_, discovery_t *discovery_) :
    _ctx (ctx_),
    _discovery (discovery_),
    _tag (gateway_tag_value),
    _tls_trust_system (0)
{
    zlink_assert (_ctx);
}

gateway_t::~gateway_t ()
{
    _tag = 0xdeadbeef;
}

bool gateway_t::check_tag () const
{
    return _tag == gateway_tag_value;
}

static int allocate_router (ctx_t *ctx_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (ZLINK_ROUTER);
    if (!*socket_)
        return -1;
    return 0;
}

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

gateway_t::service_pool_t *
gateway_t::get_or_create_pool (const std::string &service_name_)
{
    std::map<std::string, service_pool_t>::iterator it =
      _pools.find (service_name_);
    if (it != _pools.end ())
        return &it->second;

    service_pool_t pool;
    pool.service_name = service_name_;
    pool.socket = NULL;
    pool.rr_index = 0;
    pool.lb_strategy = ZLINK_GATEWAY_LB_ROUND_ROBIN;

    if (allocate_router (_ctx, &pool.socket) != 0)
        return NULL;
    ensure_gateway_routing_id (pool.socket);
    if (gateway_debug_enabled () && pool.socket) {
        unsigned char buf[256];
        size_t size = sizeof (buf);
        if (pool.socket->getsockopt (ZLINK_ROUTING_ID, buf, &size) == 0) {
            fprintf (stderr, "gateway socket routing_id size=%zu data=0x",
                     size);
            for (size_t i = 0; i < size; ++i)
                fprintf (stderr, "%02x", static_cast<unsigned> (buf[i]));
            fprintf (stderr, "\n");
        }
    }
    if (apply_tls_client (pool.socket, _tls_ca, _tls_hostname,
                          _tls_trust_system)
        != 0) {
        pool.socket->close ();
        pool.socket = NULL;
        return NULL;
    }
    int mandatory = 1;
    pool.socket->setsockopt (ZLINK_ROUTER_MANDATORY, &mandatory,
                             sizeof (mandatory));
    int sndtimeo = 2000;
    pool.socket->setsockopt (ZLINK_SNDTIMEO, &sndtimeo, sizeof (sndtimeo));
    int probe = 1;
    pool.socket->setsockopt (ZLINK_PROBE_ROUTER, &probe, sizeof (probe));

    _pools.insert (std::make_pair (service_name_, pool));
    return &_pools.find (service_name_)->second;
}

void gateway_t::refresh_pool (service_pool_t *pool_)
{
    if (!pool_ || !_discovery)
        return;

    std::vector<provider_info_t> providers;
    _discovery->snapshot_providers (pool_->service_name, &providers);

    std::vector<std::string> next_endpoints;
    std::map<std::string, zlink_routing_id_t> routing_map;
    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &entry = providers[i];
        next_endpoints.push_back (entry.endpoint);
        routing_map[entry.endpoint] = entry.routing_id;
    }

    for (size_t i = 0; i < next_endpoints.size (); ++i) {
        const std::string &endpoint = next_endpoints[i];
        if (std::find (pool_->endpoints.begin (), pool_->endpoints.end (),
                       endpoint)
            == pool_->endpoints.end ()) {
            const std::map<std::string, zlink_routing_id_t>::iterator it =
              routing_map.find (endpoint);
            if (it != routing_map.end () && it->second.size > 0) {
                pool_->socket->setsockopt (ZLINK_CONNECT_ROUTING_ID,
                                           it->second.data, it->second.size);
            }
            pool_->socket->connect (endpoint.c_str ());
        }
    }

    for (size_t i = 0; i < pool_->endpoints.size (); ++i) {
        const std::string &endpoint = pool_->endpoints[i];
        if (std::find (next_endpoints.begin (), next_endpoints.end (),
                       endpoint)
            == next_endpoints.end ()) {
            pool_->socket->term_endpoint (endpoint.c_str ());
        }
    }

    pool_->providers.swap (providers);
    pool_->endpoints.swap (next_endpoints);
}

bool gateway_t::select_provider (service_pool_t *pool_,
                                 zlink_routing_id_t *rid_)
{
    if (!pool_ || pool_->providers.empty () || !rid_)
        return false;

    if (pool_->lb_strategy == ZLINK_GATEWAY_LB_WEIGHTED) {
        uint32_t total = 0;
        for (size_t i = 0; i < pool_->providers.size (); ++i) {
            uint32_t weight = pool_->providers[i].weight;
            if (weight == 0)
                weight = 1;
            total += weight;
        }
        if (total == 0)
            total = 1;
        uint32_t pick = zlink::generate_random () % total;
        uint32_t acc = 0;
        for (size_t i = 0; i < pool_->providers.size (); ++i) {
            uint32_t weight = pool_->providers[i].weight;
            if (weight == 0)
                weight = 1;
            acc += weight;
            if (pick < acc) {
                *rid_ = pool_->providers[i].routing_id;
                return true;
            }
        }
    }

    const size_t index = pool_->rr_index % pool_->providers.size ();
    pool_->rr_index++;
    *rid_ = pool_->providers[index].routing_id;
    return true;
}

int gateway_t::send_request_frames (service_pool_t *pool_,
                                    const zlink_routing_id_t &rid_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    int flags_)
{
    if (!pool_ || !pool_->socket) {
        errno = ENOTSUP;
        return -1;
    }
    if (flags_ != 0) {
        errno = ENOTSUP;
        return -1;
    }

    zlink_msg_t rid_msg;
    if (zlink_msg_init_size (&rid_msg, rid_.size) != 0)
        return -1;
    if (rid_.size > 0)
        memcpy (zlink_msg_data (&rid_msg), rid_.data, rid_.size);
    int flags = part_count_ > 0 ? ZLINK_SNDMORE : 0;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    bool rid_sent = false;
    while (true) {
        if (zlink_msg_send (&rid_msg, pool_->socket, flags) >= 0) {
            rid_sent = true;
            break;
        }
        if (errno != EAGAIN) {
            if (gateway_debug_enabled ()) {
                fprintf (stderr, "gateway send rid failed errno=%d\n", errno);
            }
            zlink_msg_close (&rid_msg);
            return -1;
        }
        if (std::chrono::steady_clock::now () >= deadline) {
            if (gateway_debug_enabled ()) {
                fprintf (stderr, "gateway send rid timeout errno=%d\n", errno);
            }
            zlink_msg_close (&rid_msg);
            return -1;
        }
        sleep_ms (1);
    }
    if (rid_sent)
        zlink_msg_close (&rid_msg);

    for (size_t i = 0; i < part_count_; ++i) {
        flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        while (true) {
            if (zlink_msg_send (&parts_[i], pool_->socket, flags) >= 0) {
                zlink_msg_close (&parts_[i]);
                break;
            }
            if (errno != EAGAIN) {
                if (gateway_debug_enabled ()) {
                    fprintf (stderr, "gateway send part failed errno=%d\n",
                             errno);
                }
                return -1;
            }
            if (std::chrono::steady_clock::now () >= deadline) {
                if (gateway_debug_enabled ()) {
                    fprintf (stderr,
                             "gateway send part timeout errno=%d\n", errno);
                }
                return -1;
            }
            sleep_ms (1);
        }
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
    if (flags_ != 0) {
        errno = ENOTSUP;
        return -1;
    }

    // Simple, non-threadsafe send path.
    service_pool_t *pool = get_or_create_pool (service_name_);
    if (!pool) {
        errno = ENOMEM;
        return -1;
    }

    for (int i = 0; i < 200; ++i) {
        refresh_pool (pool);
        if (!pool->endpoints.empty ())
            break;
        sleep_ms (10);
    }
    zlink_routing_id_t rid;
    if (!select_provider (pool, &rid)) {
        errno = EHOSTUNREACH;
        return -1;
    }

    if (gateway_debug_enabled ()) {
        fprintf (stderr, "gateway send: service=%s rid_size=%u rid=0x",
                 service_name_, static_cast<unsigned> (rid.size));
        for (uint8_t i = 0; i < rid.size; ++i)
            fprintf (stderr, "%02x", static_cast<unsigned> (rid.data[i]));
        fprintf (stderr, "\n");
        dump_gateway_peers (pool->socket);
    }
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    while (true) {
        if (send_request_frames (pool, rid, parts_, part_count_, flags_) == 0)
            return 0;
        if (errno != EHOSTUNREACH && errno != EAGAIN)
            return -1;
        if (std::chrono::steady_clock::now () >= deadline)
            return -1;
        refresh_pool (pool);
        sleep_ms (5);
    }

    return -1;
}

zlink_msg_t *gateway_t::alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                             size_t *count_)
{
    if (count_)
        *count_ = 0;
    if (!parts_ || parts_->empty ())
        return NULL;

    const size_t count = parts_->size ();
    zlink_msg_t *out =
      static_cast<zlink_msg_t *> (malloc (count * sizeof (zlink_msg_t)));
    if (!out) {
        errno = ENOMEM;
        close_parts (parts_);
        return NULL;
    }

    for (size_t i = 0; i < count; ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&out[i]);
        if (dst->init () != 0 || dst->move ((*parts_)[i]) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&out[j]);
            free (out);
            close_parts (parts_);
            errno = EFAULT;
            return NULL;
        }
    }

    parts_->clear ();
    if (count_)
        *count_ = count;
    return out;
}

void gateway_t::close_parts (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

int gateway_t::recv_from_pool (service_pool_t *pool_,
                               std::vector<msg_t> *parts_)
{
    if (!pool_ || !pool_->socket || !parts_) {
        errno = EINVAL;
        return -1;
    }

    msg_t routing;
    if (routing.init () != 0)
        return -1;
    if (pool_->socket->recv (&routing, ZLINK_DONTWAIT) != 0) {
        routing.close ();
        return -1;
    }

    if (!(routing.flags () & msg_t::more)) {
        routing.close ();
        errno = EINVAL;
        return -1;
    }

    msg_t current;
    if (current.init () != 0) {
        routing.close ();
        return -1;
    }
    if (pool_->socket->recv (&current, 0) != 0) {
        current.close ();
        routing.close ();
        return -1;
    }

    parts_->clear ();
    bool more = current.flags () & msg_t::more;
    parts_->push_back (msg_t ());
    parts_->back ().init ();
    parts_->back ().move (current);

    while (more) {
        msg_t part;
        if (part.init () != 0) {
            close_parts (parts_);
            routing.close ();
            return -1;
        }
        if (pool_->socket->recv (&part, 0) != 0) {
            part.close ();
            close_parts (parts_);
            routing.close ();
            return -1;
        }
        more = part.flags () & msg_t::more;
        parts_->push_back (msg_t ());
        parts_->back ().init ();
        parts_->back ().move (part);
    }

    routing.close ();
    return 0;
}

bool gateway_t::recv_any (completion_entry_t *entry_, int timeout_ms_)
{
    if (!entry_)
        return false;
    const std::chrono::steady_clock::time_point deadline =
      timeout_ms_ > 0 ? std::chrono::steady_clock::now ()
                            + std::chrono::milliseconds (timeout_ms_)
                      : std::chrono::steady_clock::time_point ();

    while (true) {
        for (std::map<std::string, service_pool_t>::iterator it =
               _pools.begin ();
             it != _pools.end (); ++it) {
            service_pool_t &pool = it->second;
            std::vector<msg_t> parts;
            if (recv_from_pool (&pool, &parts) == 0) {
                entry_->service_name = pool.service_name;
                entry_->error = 0;
                entry_->parts.swap (parts);
                return true;
            }
        }

        if (timeout_ms_ == 0)
            return false;
        if (timeout_ms_ > 0
            && std::chrono::steady_clock::now () >= deadline)
            return false;
        sleep_ms (1);
    }
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

    completion_entry_t entry;
    const int timeout_ms = (flags_ == ZLINK_DONTWAIT) ? 0 : -1;
    if (!recv_any (&entry, timeout_ms)) {
        errno = (flags_ == ZLINK_DONTWAIT) ? EAGAIN : ETIMEDOUT;
        return -1;
    }

    if (service_name_out_) {
        memset (service_name_out_, 0, 256);
        strncpy (service_name_out_, entry.service_name.c_str (), 255);
    }

    zlink_msg_t *out_parts = NULL;
    size_t out_count = 0;
    if (!entry.parts.empty ()) {
        out_parts = alloc_msgv_from_parts (&entry.parts, &out_count);
        if (!out_parts) {
            if (parts_)
                *parts_ = NULL;
            if (part_count_)
                *part_count_ = 0;
            return -1;
        }
    }

    if (parts_)
        *parts_ = out_parts;
    if (part_count_)
        *part_count_ = out_count;

    if (entry.error != 0) {
        errno = entry.error;
        return -1;
    }
    return 0;
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

    service_pool_t *pool = get_or_create_pool (service_name_);
    if (!pool)
        return -1;
    pool->lb_strategy = strategy_;
    return 0;
}

int gateway_t::connection_count (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    std::map<std::string, service_pool_t>::iterator it =
      _pools.find (service_name_);
    if (it == _pools.end ())
        return 0;
    refresh_pool (&it->second);
    return static_cast<int> (it->second.endpoints.size ());
}

int gateway_t::set_tls_client (const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_)
{
    if (!ca_cert_ || !hostname_) {
        errno = EINVAL;
        return -1;
    }

    if (ca_cert_[0] == '\0' || hostname_[0] == '\0') {
        _tls_ca.clear ();
        _tls_hostname.clear ();
        _tls_trust_system = trust_system_;
        return 0;
    }

    _tls_ca = ca_cert_;
    _tls_hostname = hostname_;
    _tls_trust_system = trust_system_;

    for (std::map<std::string, service_pool_t>::iterator it = _pools.begin ();
         it != _pools.end (); ++it) {
        if (it->second.socket
            && apply_tls_client (it->second.socket, _tls_ca, _tls_hostname,
                                 _tls_trust_system)
                 != 0)
            return -1;
    }
    return 0;
}

int gateway_t::destroy ()
{
    for (std::map<std::string, service_pool_t>::iterator it =
           _pools.begin ();
         it != _pools.end (); ++it) {
        service_pool_t &pool = it->second;
        if (pool.socket) {
            pool.socket->close ();
            pool.socket = NULL;
        }
    }
    _pools.clear ();
    return 0;
}

socket_base_t *gateway_t::get_router_socket (const char *service_name_)
{
    if (!service_name_)
        return NULL;
    service_pool_t *pool = get_or_create_pool (service_name_);
    return pool ? pool->socket : NULL;
}
}
