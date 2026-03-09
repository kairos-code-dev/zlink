/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/gateway/routing_id_utils.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <algorithm>
#include <stdlib.h>
#include <string.h>

namespace zlink
{
static const uint32_t spot_node_tag_value = 0x1e6700d9;
static const uint32_t default_heartbeat_ms = 5000;
static const uint64_t discovery_refresh_ms = 500;
static const int default_registry_control_send_timeout_ms = 2000;
static const int default_registry_control_recv_timeout_ms = 5000;
static const size_t spot_pub_queue_hwm_default = 1024;
static const size_t spot_sub_queue_hwm_default = 1000;

static void spot_debugf (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_SPOT_DEBUG"))
        return;
    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-node] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    va_end (args);
}

static int resolve_spot_idle_sleep_ms ()
{
    static int cached = -1;
    if (cached >= 0)
        return cached;

    int value = 1;
    const char *env = getenv ("ZLINK_SPOT_IDLE_SLEEP_MS");
    if (env && *env) {
        char *end = NULL;
        const long parsed = strtol (env, &end, 10);
        if (end != env && parsed >= 0 && parsed <= 1000)
            value = static_cast<int> (parsed);
    }
    cached = value;
    return cached;
}

static void close_parts (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

static bool copy_parts_from_msgv (zlink_msg_t *parts_,
                                  size_t part_count_,
                                  std::vector<msg_t> *out_)
{
    out_->clear ();
    if (part_count_ == 0)
        return true;
    out_->resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        msg_t &dst = (*out_)[i];
        if (dst.init () != 0) {
            close_parts (out_);
            return false;
        }
        msg_t &src = *reinterpret_cast<msg_t *> (&parts_[i]);
        if (dst.copy (src) != 0) {
            close_parts (out_);
            return false;
        }
    }
    return true;
}

static bool copy_parts_from_vec (const std::vector<msg_t> &src_,
                                 std::vector<msg_t> *out_)
{
    out_->clear ();
    if (src_.empty ())
        return true;
    out_->resize (src_.size ());
    for (size_t i = 0; i < src_.size (); ++i) {
        msg_t &dst = (*out_)[i];
        if (dst.init () != 0) {
            close_parts (out_);
            return false;
        }
        msg_t &src = const_cast<msg_t &> (src_[i]);
        if (dst.copy (src) != 0) {
            close_parts (out_);
            return false;
        }
    }
    return true;
}

static void release_shared_message (spot_shared_message_t *shared_)
{
    if (!shared_)
        return;
    if (shared_->refs.sub (1))
        return;
    close_parts (&shared_->parts);
    delete shared_;
}

static void close_msgv (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

static bool copy_parts_to_msgv (const std::vector<msg_t> &src_,
                                std::vector<zlink_msg_t> *out_)
{
    out_->clear ();
    if (src_.empty ())
        return true;

    out_->resize (src_.size ());
    for (size_t i = 0; i < src_.size (); ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&(*out_)[i]);
        if (dst->init () != 0) {
            close_msgv (out_);
            return false;
        }
        msg_t &src = const_cast<msg_t &> (src_[i]);
        if (dst->copy (src) != 0) {
            close_msgv (out_);
            return false;
        }
    }

    return true;
}

static bool apply_service_routing_id (socket_base_t *socket_,
                                      const std::string *override_id_,
                                      zlink_routing_id_t *out_)
{
    return zlink::discovery::set_socket_routing_id (socket_, override_id_,
                                                    out_);
}

static int send_frame (socket_base_t *socket_,
                       const void *data_,
                       size_t size_,
                       int flags_)
{
    if (!socket_) {
        errno = ENOTSUP;
        return -1;
    }
    msg_t msg;
    if (msg.init_size (size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (msg.data (), data_, size_);
    if (socket_->send (&msg, flags_) != 0) {
        msg.close ();
        return -1;
    }
    msg.close ();
    return 0;
}

static int send_u16 (socket_base_t *socket_, uint16_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

static int send_u32 (socket_base_t *socket_, uint32_t value_, int flags_)
{
    return send_frame (socket_, &value_, sizeof (value_), flags_);
}

static int send_string (socket_base_t *socket_,
                        const std::string &value_,
                        int flags_)
{
    return send_frame (socket_, value_.empty () ? NULL : value_.data (),
                       value_.size (), flags_);
}

spot_node_t::spot_node_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (spot_node_tag_value),
    _pub (NULL),
    _sub (NULL),
    _dealer (NULL),
    _node_id (0),
    _registered (false),
    _heartbeat_interval_ms (default_heartbeat_ms),
    _last_heartbeat_ms (0),
    _discovery (NULL),
    _next_discovery_refresh_ms (0),
    _pub_queue_hwm (spot_pub_queue_hwm_default),
    _sub_queue_hwm (spot_sub_queue_hwm_default),
    _sub_recv_timeout_ms (-1),
    _sub_queue_nodrop (false),
    _pub_mode (ZLINK_SPOT_NODE_PUB_MODE_SYNC),
    _pub_queue_full_policy (ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN),
    _pub_pollable_mode (0),
    _sub_pollable_mode (0),
    _tls_trust_system (0),
    _stop (0),
    _task_id (0),
    _control_tick_ms (
      static_cast<uint32_t> (std::max (1, resolve_spot_idle_sleep_ms ()))),
    _pub_monitor (ctx_),
    _sub_monitor (ctx_)
{
    zlink_assert (_ctx);

    _routing_id.size = 0;
    _pub_routing_id.size = 0;
    _sub_routing_id.size = 0;
    _node_id = zlink::generate_random ();
    if (_node_id == 0)
        _node_id = 1;
    _routing_id.size = sizeof (_node_id);
    memcpy (_routing_id.data, &_node_id, sizeof (_node_id));

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime) {
        _task_id = runtime->add_periodic_task (control_task, this,
                                               _control_tick_ms, true);
    }
    if (_task_id == 0)
        _tag = 0xdeadbeef;
}

spot_node_t::~spot_node_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_node_t::check_tag () const
{
    return _tag == spot_node_tag_value;
}

int spot_node_t::ensure_pub_facade_mode () const
{
    if (_pub_pollable_mode.get () != 0) {
        errno = EFSM;
        return -1;
    }
    return 0;
}

int spot_node_t::ensure_sub_facade_mode () const
{
    if (_sub_pollable_mode.get () != 0) {
        errno = EFSM;
        return -1;
    }
    return 0;
}

int spot_node_t::ensure_sub_socket_mutation_allowed () const
{
    if (_sub_pollable_mode.get () != 0) {
        errno = EFSM;
        return -1;
    }
    return 0;
}

bool spot_node_t::validate_topic (const char *topic_, std::string *out_)
{
    if (!topic_ || topic_[0] == '\0')
        return false;
    const size_t len = strlen (topic_);
    if (len == 0 || len > 255)
        return false;
    if (out_)
        *out_ = std::string (topic_, len);
    return true;
}

bool spot_node_t::validate_pattern (const char *pattern_, std::string *prefix_)
{
    if (!pattern_ || pattern_[0] == '\0')
        return false;
    const size_t len = strlen (pattern_);
    if (len == 0 || len > 255)
        return false;
    const char *star = strchr (pattern_, '*');
    if (!star)
        return false;
    if (star != pattern_ + len - 1)
        return false;
    if (strchr (star + 1, '*'))
        return false;
    if (prefix_)
        *prefix_ = std::string (pattern_, len - 1);
    return true;
}

bool spot_node_t::validate_service_name (const std::string &name_)
{
    if (name_.empty () || name_.size () > 64)
        return false;
    for (size_t i = 0; i < name_.size (); ++i) {
        const char c = name_[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'
              || c == '-'))
            return false;
    }
    return true;
}

std::string spot_node_t::resolve_advertise (const std::string &bind_endpoint_)
{
    if (bind_endpoint_.empty ())
        return std::string ();

    std::string endpoint = bind_endpoint_;
    std::string::size_type pos = endpoint.find ("tcp://");
    if (pos == 0) {
        std::string::size_type host_start = strlen ("tcp://");
        std::string::size_type colon = endpoint.find (':', host_start);
        if (colon != std::string::npos) {
            std::string host = endpoint.substr (host_start, colon - host_start);
            if (host == "*" || host == "0.0.0.0")
                endpoint.replace (host_start, host.size (), "127.0.0.1");
        }
    }

    return endpoint;
}

void spot_node_t::add_filter (const std::string &filter_)
{
    if (filter_.empty ())
        return;
    size_t &count = _filter_refcount[filter_];
    if (count == 0)
        _pending_subscribe.push_back (filter_);
    ++count;
}

void spot_node_t::remove_filter (const std::string &filter_)
{
    std::map<std::string, size_t>::iterator it =
      _filter_refcount.find (filter_);
    if (it == _filter_refcount.end ())
        return;
    if (it->second <= 1) {
        _pending_unsubscribe.push_back (filter_);
        _filter_refcount.erase (it);
        return;
    }
    --it->second;
}

int spot_node_t::bind (const char *endpoint_)
{
    if (!endpoint_) {
        errno = EINVAL;
        return -1;
    }

    int rc = -1;
    {
        scoped_lock_t pub_lock (_pub_sync);
        if (!_pub) {
            _pub = _ctx->create_socket (ZLINK_PUB);
            if (!_pub)
                return -1;
            for (size_t i = 0; i < _pub_opts.size (); ++i) {
                if (!_pub_opts[i].value.empty ())
                    _pub->setsockopt (_pub_opts[i].option,
                                      &_pub_opts[i].value[0],
                                      _pub_opts[i].value.size ());
            }
            if (!apply_service_routing_id (
                  _pub, &_pub_routing_id_override, &_pub_routing_id)) {
                _pub->close ();
                _pub = NULL;
                return -1;
            }
        }
        if (!_tls_cert.empty ()) {
            if (_pub->setsockopt (ZLINK_TLS_CERT, _tls_cert.data (),
                                  _tls_cert.size ())
                  != 0
                || _pub->setsockopt (ZLINK_TLS_KEY, _tls_key.data (),
                                     _tls_key.size ())
                     != 0)
                return -1;
        }
        rc = _pub->bind (endpoint_);
    }
    if (rc == 0) {
        scoped_lock_t lock (_sync);
        _bind_endpoints.push_back (endpoint_);
        emit_pub_event (ZLINK_SPOT_PUB_QUEUE_DRAINED, endpoint_, 1, 0);
    }
    return rc;
}

int spot_node_t::connect_registry (const char *registry_router_endpoint_)
{
    if (!registry_router_endpoint_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_registry_endpoints.insert (registry_router_endpoint_).second)
        _pending_registry_connect.push_back (registry_router_endpoint_);
    request_control_tick ();
    return 0;
}

int spot_node_t::connect_peer_pub (const char *peer_pub_endpoint_)
{
    if (!peer_pub_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_sub_socket_mutation_allowed () != 0)
        return -1;

    uint32_t count = 0;
    {
        scoped_lock_t lock (_sync);
        if (_peer_endpoints.count (peer_pub_endpoint_))
            return 0;
        _peer_endpoints.insert (peer_pub_endpoint_);
        _pending_peer_connect.push_back (peer_pub_endpoint_);
        request_control_tick ();
        count = static_cast<uint32_t> (_peer_endpoints.size ());
    }
    emit_sub_event (ZLINK_MONITOR_EVENT_PEER_UP, peer_pub_endpoint_, count, 0);
    report_sub_topology (ZLINK_TOPOLOGY_STATE_READY, peer_pub_endpoint_, count,
                         0);
    return 0;
}

int spot_node_t::disconnect_peer_pub (const char *peer_pub_endpoint_)
{
    if (!peer_pub_endpoint_) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_sub_socket_mutation_allowed () != 0)
        return -1;

    uint32_t count = 0;
    {
        scoped_lock_t lock (_sync);
        _peer_endpoints.erase (peer_pub_endpoint_);
        _pending_peer_disconnect.push_back (peer_pub_endpoint_);
        request_control_tick ();
        count = static_cast<uint32_t> (_peer_endpoints.size ());
    }
    emit_sub_event (ZLINK_MONITOR_EVENT_PEER_DOWN, peer_pub_endpoint_, count,
                    0);
    report_sub_topology (
      count > 0 ? ZLINK_TOPOLOGY_STATE_CONNECTING : ZLINK_TOPOLOGY_STATE_LOST,
      peer_pub_endpoint_, count, 0);
    return 0;
}

int spot_node_t::register_node (const char *service_name_,
                                const char *advertise_endpoint_)
{
    std::string service = service_name_ && service_name_[0] != '\0'
                            ? service_name_
                            : "spot-node";
    if (!validate_service_name (service)) {
        errno = EINVAL;
        return -1;
    }

    std::string advertise;
    if (advertise_endpoint_ && advertise_endpoint_[0] != '\0') {
        advertise = advertise_endpoint_;
    } else {
        scoped_lock_t lock (_sync);
        if (_bind_endpoints.size () != 1) {
            errno = EINVAL;
            return -1;
        }
        advertise = resolve_advertise (_bind_endpoints[0]);
    }
    if (advertise.empty ()) {
        errno = EINVAL;
        return -1;
    }

    std::vector<std::string> registry_endpoints;
    {
        scoped_lock_t lock (_sync);
        if (_registry_endpoints.empty ()) {
            errno = ENOTSUP;
            return -1;
        }
        registry_endpoints.assign (_registry_endpoints.begin (),
                                   _registry_endpoints.end ());
    }

    {
        scoped_lock_t dealer_lock (_dealer_sync);
        if (!_dealer) {
            _dealer = _ctx->create_socket (ZLINK_DEALER);
            if (!_dealer)
                return -1;
            _dealer->setsockopt (ZLINK_SNDTIMEO,
                                 &default_registry_control_send_timeout_ms,
                                 sizeof (default_registry_control_send_timeout_ms));
            _dealer->setsockopt (ZLINK_RCVTIMEO,
                                 &default_registry_control_recv_timeout_ms,
                                 sizeof (default_registry_control_recv_timeout_ms));
            if (!_tls_ca.empty ()) {
                if (_dealer->setsockopt (ZLINK_TLS_CA, _tls_ca.data (),
                                         _tls_ca.size ())
                      != 0
                    || _dealer->setsockopt (ZLINK_TLS_HOSTNAME,
                                            _tls_hostname.data (),
                                            _tls_hostname.size ())
                         != 0
                    || _dealer->setsockopt (ZLINK_TLS_TRUST_SYSTEM,
                                            &_tls_trust_system,
                                            sizeof (_tls_trust_system))
                         != 0) {
                    _dealer->close ();
                    _dealer = NULL;
                    return -1;
                }
            }
            for (size_t i = 0; i < _dealer_opts.size (); ++i) {
                if (!_dealer_opts[i].value.empty ())
                    _dealer->setsockopt (_dealer_opts[i].option,
                                         &_dealer_opts[i].value[0],
                                         _dealer_opts[i].value.size ());
            }
            _dealer->setsockopt (ZLINK_ROUTING_ID, _routing_id.data,
                                 _routing_id.size);
        }

        std::deque<std::string> registry_connect;
        {
            scoped_lock_t lock (_sync);
            if (_pending_registry_connect.empty ()) {
                for (size_t i = 0; i < registry_endpoints.size (); ++i)
                    registry_connect.push_back (registry_endpoints[i]);
            } else {
                registry_connect.swap (_pending_registry_connect);
            }
        }
        for (std::deque<std::string>::const_iterator it =
               registry_connect.begin ();
             it != registry_connect.end (); ++it) {
            if (_dealer->connect (it->c_str ()) != 0)
                return -1;
        }

        if (send_u16 (_dealer, discovery_protocol::msg_register, ZLINK_SNDMORE)
            != 0
            || send_u16 (_dealer, discovery_protocol::service_type_spot_node,
                     ZLINK_SNDMORE)
             != 0
            || send_string (_dealer, service, ZLINK_SNDMORE) != 0
            || send_string (_dealer, advertise, ZLINK_SNDMORE) != 0
            || send_u32 (_dealer, 1, 0) != 0) {
            return -1;
        }
    }

    scoped_lock_t lock (_sync);
    _registered = true;
    _service_name = service;
    _advertise_endpoint = advertise;
    _last_heartbeat_ms = 0;
    _pending_registry_connect.clear ();
    for (std::set<std::string>::const_iterator it =
           _registry_endpoints.begin ();
         it != _registry_endpoints.end (); ++it) {
        _pending_registry_connect.push_back (*it);
    }
    request_control_tick ();
    report_pub_topology (ZLINK_TOPOLOGY_STATE_READY, advertise.c_str (), 1, 0);
    return 0;
}

int spot_node_t::unregister_node (const char *service_name_)
{
    std::string service = service_name_ && service_name_[0] != '\0'
                            ? service_name_
                            : "spot-node";
    if (!validate_service_name (service)) {
        errno = EINVAL;
        return -1;
    }

    std::string advertise;
    {
        scoped_lock_t lock (_sync);
        advertise = _advertise_endpoint;
        _registered = false;
    }

    std::vector<std::string> registry_endpoints;
    {
        scoped_lock_t lock (_sync);
        if (_registry_endpoints.empty ()) {
            errno = ENOTSUP;
            return -1;
        }
        registry_endpoints.assign (_registry_endpoints.begin (),
                                   _registry_endpoints.end ());
    }

    scoped_lock_t dealer_lock (_dealer_sync);
    if (!_dealer)
        return 0;

    {
        std::deque<std::string> registry_connect;
        scoped_lock_t lock (_sync);
        registry_connect.swap (_pending_registry_connect);
        for (std::deque<std::string>::const_iterator it =
               registry_connect.begin ();
             it != registry_connect.end (); ++it) {
            if (_dealer->connect (it->c_str ()) != 0)
                return -1;
        }
    }

    if (send_u16 (_dealer, discovery_protocol::msg_unregister, ZLINK_SNDMORE)
          != 0
        || send_u16 (_dealer, discovery_protocol::service_type_spot_node,
                     ZLINK_SNDMORE)
             != 0
        || send_string (_dealer, service, ZLINK_SNDMORE) != 0
        || send_string (_dealer, advertise, 0) != 0)
        return -1;

    return 0;
}

int spot_node_t::set_discovery (discovery_t *discovery_,
                                const char *service_name_)
{
    if (!discovery_) {
        errno = EINVAL;
        return -1;
    }
    if (discovery_->service_type ()
        != discovery_protocol::service_type_spot_node) {
        errno = EINVAL;
        return -1;
    }

    std::string service = service_name_ && service_name_[0] != '\0'
                            ? service_name_
                            : "spot-node";
    if (!validate_service_name (service)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    _discovery = discovery_;
    _discovery_service = service;
    _next_discovery_refresh_ms = 0;
    request_control_tick ();
    return 0;
}

int spot_node_t::set_tls_server (const char *cert_, const char *key_)
{
    if (!cert_ || !key_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (cert_[0] == '\0' || key_[0] == '\0') {
        _tls_cert.clear ();
        _tls_key.clear ();
        return 0;
    }

    _tls_cert = cert_;
    _tls_key = key_;

    scoped_lock_t pub_lock (_pub_sync);
    if (_pub) {
        if (_pub
            && (_pub->setsockopt (ZLINK_TLS_CERT, _tls_cert.data (),
                                  _tls_cert.size ())
                  != 0
                || _pub->setsockopt (ZLINK_TLS_KEY, _tls_key.data (),
                                     _tls_key.size ())
                     != 0))
            return -1;
    }
    return 0;
}

int spot_node_t::set_tls_client (const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    if (!ca_cert_ || !hostname_) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_sub_socket_mutation_allowed () != 0)
        return -1;

    scoped_lock_t lock (_sync);
    if (ca_cert_[0] == '\0' || hostname_[0] == '\0') {
        _tls_ca.clear ();
        _tls_hostname.clear ();
        _tls_trust_system = trust_system_;
        return 0;
    }

    _tls_ca = ca_cert_;
    _tls_hostname = hostname_;
    _tls_trust_system = trust_system_;
    if (_sub) {
        if (_sub->setsockopt (ZLINK_TLS_CA, _tls_ca.data (), _tls_ca.size ())
              != 0
            || _sub->setsockopt (ZLINK_TLS_HOSTNAME, _tls_hostname.data (),
                                 _tls_hostname.size ())
                 != 0
            || _sub->setsockopt (ZLINK_TLS_TRUST_SYSTEM, &_tls_trust_system,
                                 sizeof (_tls_trust_system))
                 != 0)
            return -1;
    }
    if (_dealer) {
        if (_dealer->setsockopt (ZLINK_TLS_CA, _tls_ca.data (),
                                 _tls_ca.size ())
              != 0
            || _dealer->setsockopt (ZLINK_TLS_HOSTNAME, _tls_hostname.data (),
                                    _tls_hostname.size ())
                 != 0
            || _dealer->setsockopt (ZLINK_TLS_TRUST_SYSTEM, &_tls_trust_system,
                                    sizeof (_tls_trust_system))
                 != 0)
            return -1;
    }
    return 0;
}

int spot_node_t::set_pub_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_pub_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_pub_pollable_mode.get () != 0 || !_bind_endpoints.empty ()
        || !_advertise_endpoint.empty ()) {
        errno = EFSM;
        return -1;
    }
    _pub_routing_id_override.assign (static_cast<const char *> (data_), size_);
    memcpy (_pub_routing_id.data, data_, size_);
    _pub_routing_id.size = static_cast<uint8_t> (size_);
    if (_pub
        && _pub->setsockopt (ZLINK_ROUTING_ID, _pub_routing_id.data,
                             _pub_routing_id.size)
             != 0)
        return -1;
    return 0;
}

int spot_node_t::set_sub_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_sub_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_sub_pollable_mode.get () != 0 || !_peer_endpoints.empty ()
        || !_filter_refcount.empty ()) {
        errno = EFSM;
        return -1;
    }
    _sub_routing_id_override.assign (static_cast<const char *> (data_), size_);
    memcpy (_sub_routing_id.data, data_, size_);
    _sub_routing_id.size = static_cast<uint8_t> (size_);
    if (_sub
        && _sub->setsockopt (ZLINK_ROUTING_ID, _sub_routing_id.data,
                             _sub_routing_id.size)
             != 0)
        return -1;
    return 0;
}

int spot_node_t::pub_routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_pub_routing_id.size == 0) {
        if (_pub) {
            size_t size = sizeof (out_->data);
            if (zlink_getsockopt (static_cast<void *> (_pub), ZLINK_ROUTING_ID,
                                  out_->data, &size)
                != 0)
                return -1;
            out_->size = static_cast<uint8_t> (size);
            return 0;
        }
        errno = ENOENT;
        return -1;
    }
    *out_ = _pub_routing_id;
    return 0;
}

int spot_node_t::sub_routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_sub_routing_id.size == 0) {
        if (_sub) {
            size_t size = sizeof (out_->data);
            if (zlink_getsockopt (static_cast<void *> (_sub), ZLINK_ROUTING_ID,
                                  out_->data, &size)
                != 0)
                return -1;
            out_->size = static_cast<uint8_t> (size);
            return 0;
        }
        errno = ENOENT;
        return -1;
    }
    *out_ = _sub_routing_id;
    return 0;
}

int spot_node_t::set_pub_option (int option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
            return set_socket_option (spot_node_socket_pub, ZLINK_SNDHWM,
                                      optval_, optvallen_);
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
            return set_socket_option (spot_node_socket_pub,
                                      ZLINK_SNDTIMEO, optval_, optvallen_);
        case ZLINK_SPOT_PUB_OPT_LINGER:
            return set_socket_option (spot_node_socket_pub, ZLINK_LINGER,
                                      optval_, optvallen_);
        case ZLINK_SPOT_PUB_OPT_NODROP:
            return set_socket_option (spot_node_socket_pub,
                                      ZLINK_XPUB_NODROP, optval_, optvallen_);
        case ZLINK_SPOT_PUB_OPT_MODE:
            return set_socket_option (spot_node_socket_node,
                                      spot_node_opt_pub_mode, optval_,
                                      optvallen_);
        case ZLINK_SPOT_PUB_OPT_QUEUE_HWM:
            return set_socket_option (spot_node_socket_node,
                                      spot_node_opt_pub_queue_hwm,
                                      optval_, optvallen_);
        case ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY:
            return set_socket_option (
              spot_node_socket_node, spot_node_opt_pub_queue_full_policy,
              optval_, optvallen_);
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
            return set_socket_option (spot_node_socket_pub, ZLINK_SNDBUF,
                                      optval_, optvallen_);
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            return set_socket_option (spot_node_socket_pub, ZLINK_RCVBUF,
                                      optval_, optvallen_);
        default:
            errno = EINVAL;
            return -1;
    }
}

int spot_node_t::set_sub_option (int option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            return set_socket_option (spot_node_socket_sub, ZLINK_RCVHWM,
                                      optval_, optvallen_);
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            return set_socket_option (spot_node_socket_sub,
                                      ZLINK_RCVTIMEO, optval_, optvallen_);
        case ZLINK_SPOT_SUB_OPT_LINGER:
            return set_socket_option (spot_node_socket_sub, ZLINK_LINGER,
                                      optval_, optvallen_);
        case ZLINK_SPOT_SUB_OPT_QUEUE_NODROP:
            return set_socket_option (spot_node_socket_sub,
                                      ZLINK_XPUB_NODROP, optval_, optvallen_);
        case ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY:
            return set_socket_option (spot_node_socket_sub,
                                      ZLINK_XPUB_NODROP, optval_, optvallen_);
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            return set_socket_option (spot_node_socket_sub, ZLINK_SNDBUF,
                                      optval_, optvallen_);
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            return set_socket_option (spot_node_socket_sub, ZLINK_RCVBUF,
                                      optval_, optvallen_);
        default:
            errno = EINVAL;
            return -1;
    }
}

void *spot_node_t::pub_monitor_open (int events_)
{
    return _pub_monitor.open (events_);
}

void *spot_node_t::sub_monitor_open (int events_)
{
    return _sub_monitor.open (events_);
}

void spot_node_t::emit_pub_event (uint32_t event_type_,
                                  const char *endpoint_,
                                  uint32_t value_,
                                  int32_t error_code_)
{
    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    ev.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    ev.event_type = event_type_;
    ev.value = value_;
    ev.error_code = error_code_;
    ev.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    ev.routing_id = _pub_routing_id;
    if (endpoint_ && endpoint_[0] != '\0') {
        ev.detail_flags |= ZLINK_EVENT_DETAIL_ENDPOINT;
        strncpy (ev.endpoint, endpoint_, sizeof (ev.endpoint) - 1);
    }
    _pub_monitor.emit (ev);
}

void spot_node_t::emit_sub_event (uint32_t event_type_,
                                  const char *endpoint_,
                                  uint32_t value_,
                                  int32_t error_code_)
{
    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    ev.service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    ev.event_type = event_type_;
    ev.value = value_;
    ev.error_code = error_code_;
    ev.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    ev.routing_id = _sub_routing_id;
    if (endpoint_ && endpoint_[0] != '\0') {
        ev.detail_flags |= ZLINK_EVENT_DETAIL_ENDPOINT;
        strncpy (ev.endpoint, endpoint_, sizeof (ev.endpoint) - 1);
    }
    _sub_monitor.emit (ev);
}

void spot_node_t::report_pub_topology (uint16_t state_,
                                       const char *endpoint_,
                                       uint32_t ready_count_,
                                       int32_t error_code_)
{
    std::string service;
    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        service = !_service_name.empty () ? _service_name : _discovery_service;
        discovery = _discovery;
    }
    if (service.empty ())
        service = "spot-pub";
    if (_pub_routing_id.size == 0 || !discovery)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = _pub_routing_id;
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    strncpy (entry.service_name, service.c_str (),
             sizeof (entry.service_name) - 1);
    if (endpoint_ && endpoint_[0] != '\0')
        strncpy (entry.endpoint, endpoint_, sizeof (entry.endpoint) - 1);
    entry.source =
      _discovery ? ZLINK_TOPOLOGY_SOURCE_DISCOVERY : ZLINK_TOPOLOGY_SOURCE_REGISTRY;
    entry.state = state_;
    entry.desired_count = 1;
    entry.ready_count = ready_count_;
    entry.error_code = static_cast<uint32_t> (error_code_);
    entry.last_reported_ms = zlink::clock_t ().now_ms ();
    spot_debugf ("report pub topology service=%s state=%u ready=%u",
                 entry.service_name, static_cast<unsigned int> (state_),
                 static_cast<unsigned int> (ready_count_));
    discovery->upsert_service_summary (entry);
}

void spot_node_t::report_sub_topology (uint16_t state_,
                                       const char *endpoint_,
                                       uint32_t ready_count_,
                                       int32_t error_code_)
{
    std::string service;
    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        service = !_discovery_service.empty () ? _discovery_service : _service_name;
        discovery = _discovery;
    }
    if (service.empty ())
        service = "spot-sub";
    if (_sub_routing_id.size == 0 || !discovery)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = _sub_routing_id;
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    strncpy (entry.service_name, service.c_str (),
             sizeof (entry.service_name) - 1);
    if (endpoint_ && endpoint_[0] != '\0')
        strncpy (entry.endpoint, endpoint_, sizeof (entry.endpoint) - 1);
    entry.source =
      _discovery ? ZLINK_TOPOLOGY_SOURCE_DISCOVERY : ZLINK_TOPOLOGY_SOURCE_MANUAL;
    entry.state = state_;
    entry.desired_count = 1;
    entry.ready_count = ready_count_;
    entry.error_code = static_cast<uint32_t> (error_code_);
    entry.last_reported_ms = zlink::clock_t ().now_ms ();
    spot_debugf ("report sub topology service=%s state=%u ready=%u endpoint=%s",
                 entry.service_name, static_cast<unsigned int> (state_),
                 static_cast<unsigned int> (ready_count_),
                 endpoint_ ? endpoint_ : "");
    discovery->upsert_service_summary (entry);
}

int spot_node_t::set_socket_option (int socket_role_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    if (socket_role_ == spot_node_socket_node) {
        if (optvallen_ != sizeof (int)) {
            errno = EINVAL;
            return -1;
        }

        int value = 0;
        memcpy (&value, optval_, sizeof (value));
        switch (option_) {
            case spot_node_opt_pub_mode:
                if (value != ZLINK_SPOT_NODE_PUB_MODE_SYNC
                    && value != ZLINK_SPOT_NODE_PUB_MODE_ASYNC) {
                    errno = EINVAL;
                    return -1;
                }
                _pub_mode.set (value);
                return 0;
            case spot_node_opt_pub_queue_hwm:
                if (value <= 0) {
                    errno = EINVAL;
                    return -1;
                }
                {
                    scoped_lock_t queue_lock (_pub_queue_sync);
                    _pub_queue_hwm = static_cast<size_t> (value);
                }
                return 0;
            case spot_node_opt_pub_queue_full_policy:
                if (value != ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN
                    && value != ZLINK_SPOT_NODE_PUB_QUEUE_FULL_DROP) {
                    errno = EINVAL;
                    return -1;
                }
                _pub_queue_full_policy.set (value);
                return 0;
            default:
                errno = EINVAL;
                return -1;
        }
    }

    scoped_lock_t lock (_sync);
    std::vector<socket_opt_t> *opts = NULL;
    socket_base_t *existing = NULL;
    switch (socket_role_) {
        case spot_node_socket_pub:
            opts = &_pub_opts;
            existing = _pub;
            break;
        case spot_node_socket_sub:
            if (ensure_sub_socket_mutation_allowed () != 0)
                return -1;
            opts = &_sub_opts;
            existing = _sub;
            break;
        case spot_node_socket_dealer:
            opts = &_dealer_opts;
            existing = _dealer;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (socket_role_ == spot_node_socket_sub
        && option_ == ZLINK_RCVHWM && optvallen_ == sizeof (int)) {
        int value = 0;
        memcpy (&value, optval_, sizeof (value));
        if (value > 0) {
            _sub_queue_hwm = static_cast<size_t> (value);
            for (std::set<spot_sub_t *>::iterator it = _subs.begin ();
                 it != _subs.end (); ++it) {
                if (*it) {
                    scoped_lock_t queue_lock ((*it)->_queue_sync);
                    (*it)->_queue_hwm = _sub_queue_hwm;
                }
            }
        }
    }

    if (socket_role_ == spot_node_socket_sub
        && option_ == ZLINK_RCVTIMEO && optvallen_ == sizeof (int)) {
        int value = 0;
        memcpy (&value, optval_, sizeof (value));
        if (value < -1) {
            errno = EINVAL;
            return -1;
        }
        _sub_recv_timeout_ms = value;
        for (std::set<spot_sub_t *>::iterator it = _subs.begin ();
             it != _subs.end (); ++it) {
            if (*it) {
                scoped_lock_t queue_lock ((*it)->_queue_sync);
                (*it)->_recv_timeout_ms = _sub_recv_timeout_ms;
            }
        }
    }

    if (socket_role_ == spot_node_socket_sub
        && option_ == ZLINK_XPUB_NODROP && optvallen_ == sizeof (int)) {
        int value = 0;
        memcpy (&value, optval_, sizeof (value));
        _sub_queue_nodrop = value != 0;
        for (std::set<spot_sub_t *>::iterator it = _subs.begin ();
             it != _subs.end (); ++it) {
            if (*it) {
                scoped_lock_t queue_lock ((*it)->_queue_sync);
                (*it)->_queue_nodrop = _sub_queue_nodrop;
            }
        }
        return 0;
    }

    for (size_t i = 0; i < opts->size (); ++i) {
        if ((*opts)[i].option == option_) {
            (*opts)[i].value.assign (
              static_cast<const unsigned char *> (optval_),
              static_cast<const unsigned char *> (optval_) + optvallen_);
            if (existing) {
                if (socket_role_ == spot_node_socket_pub) {
                    scoped_lock_t pub_lock (_pub_sync);
                    if (_pub)
                        _pub->setsockopt (option_, optval_, optvallen_);
                } else {
                    existing->setsockopt (option_, optval_, optvallen_);
                }
            }
            return 0;
        }
    }
    socket_opt_t opt;
    opt.option = option_;
    opt.value.assign (static_cast<const unsigned char *> (optval_),
                      static_cast<const unsigned char *> (optval_)
                        + optvallen_);
    opts->push_back (opt);
    if (existing) {
        if (socket_role_ == spot_node_socket_pub) {
            scoped_lock_t pub_lock (_pub_sync);
            if (_pub)
                _pub->setsockopt (option_, optval_, optvallen_);
        } else {
            existing->setsockopt (option_, optval_, optvallen_);
        }
    }
    return 0;
}

void *spot_node_t::pub_socket_unsafe ()
{
    void *socket = pub_socket_for_poller ();
    if (!socket)
        return NULL;
    _pub_pollable_mode.set (1);
    return socket;
}

void *spot_node_t::pub_socket_for_poller ()
{
    std::vector<std::string> bind_endpoints;
    {
        scoped_lock_t lock (_sync);
        bind_endpoints.assign (_bind_endpoints.begin (), _bind_endpoints.end ());
    }
    if (!_pub && !bind_endpoints.empty ()) {
        scoped_lock_t pub_lock (_pub_sync);
        if (!_pub) {
            _pub = _ctx->create_socket (ZLINK_PUB);
            if (_pub) {
                for (size_t i = 0; i < _pub_opts.size (); ++i) {
                    if (!_pub_opts[i].value.empty ())
                        _pub->setsockopt (_pub_opts[i].option,
                                          &_pub_opts[i].value[0],
                                          _pub_opts[i].value.size ());
                }
                if (!apply_service_routing_id (
                      _pub, &_pub_routing_id_override, &_pub_routing_id)) {
                    _pub->close ();
                    _pub = NULL;
                    return NULL;
                }
                if (!_tls_cert.empty ()) {
                    if (_pub->setsockopt (ZLINK_TLS_CERT, _tls_cert.data (),
                                          _tls_cert.size ())
                          != 0
                        || _pub->setsockopt (ZLINK_TLS_KEY, _tls_key.data (),
                                             _tls_key.size ())
                             != 0) {
                        _pub->close ();
                        _pub = NULL;
                    }
                }
                if (_pub) {
                    for (size_t i = 0; i < bind_endpoints.size (); ++i) {
                        if (_pub->bind (bind_endpoints[i].c_str ()) != 0) {
                            _pub->close ();
                            _pub = NULL;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (!_pub)
        return NULL;
    return static_cast<void *> (_pub);
}

void *spot_node_t::sub_socket_unsafe ()
{
    void *socket = sub_socket_for_poller ();
    if (!socket)
        return NULL;
    _sub_pollable_mode.set (1);
    return socket;
}

void *spot_node_t::sub_socket_for_poller ()
{
    const bool control_task_suspended = suspend_control_task ();
    ensure_control_sockets ();
    flush_pending ();
    if (!_sub)
    {
        if (control_task_suspended)
            resume_control_task ();
        return NULL;
    }
    if (control_task_suspended)
        resume_control_task ();
    return static_cast<void *> (_sub);
}

spot_pub_t *spot_node_t::create_spot_pub ()
{
    spot_pub_t *pub = new (std::nothrow) spot_pub_t (this);
    if (!pub) {
        errno = ENOMEM;
        return NULL;
    }

    scoped_lock_t lock (_sync);
    _pubs.insert (pub);
    return pub;
}

spot_sub_t *spot_node_t::create_spot_sub ()
{
    spot_sub_t *sub = new (std::nothrow) spot_sub_t (this);
    if (!sub) {
        errno = ENOMEM;
        return NULL;
    }

    scoped_lock_t lock (_sync);
    sub->_queue_hwm = _sub_queue_hwm;
    sub->_recv_timeout_ms = _sub_recv_timeout_ms;
    sub->_queue_nodrop = _sub_queue_nodrop;
    _subs.insert (sub);
    return sub;
}

void spot_node_t::remove_spot_pub (spot_pub_t *pub_)
{
    if (!pub_)
        return;

    scoped_lock_t lock (_sync);
    _pubs.erase (pub_);
}

void spot_node_t::remove_spot_sub (spot_sub_t *sub_)
{
    if (!sub_)
        return;

    scoped_lock_t lock (_sync);
    remove_spot_sub_locked (sub_);
}

void spot_node_t::remove_spot_sub_locked (spot_sub_t *sub_)
{
    if (!sub_)
        return;

    if (_subs.erase (sub_) == 0)
        return;
    for (std::deque<handler_delivery_t>::iterator it =
           _pending_handler_delivery.begin ();
         it != _pending_handler_delivery.end ();) {
        std::vector<spot_sub_t *> &targets = it->targets;
        targets.erase (std::remove (targets.begin (), targets.end (), sub_),
                       targets.end ());
        if (targets.empty ()) {
            close_parts (&it->payload);
            it = _pending_handler_delivery.erase (it);
        } else {
            ++it;
        }
    }

    for (std::set<std::string>::const_iterator it = sub_->_topics.begin ();
         it != sub_->_topics.end (); ++it) {
        std::map<std::string, std::set<spot_sub_t *> >::iterator idx =
          _topic_index.find (*it);
        if (idx != _topic_index.end ()) {
            idx->second.erase (sub_);
            if (idx->second.empty ())
                _topic_index.erase (idx);
        }
        remove_filter (*it);
    }

    for (std::set<std::string>::const_iterator it =
           sub_->_patterns.begin ();
         it != sub_->_patterns.end (); ++it) {
        remove_filter (*it);
    }
    _pattern_subs.erase (sub_);

    sub_->_topics.clear ();
    sub_->_patterns.clear ();
    sub_->clear_queue ();
}

int spot_node_t::subscribe (spot_sub_t *sub_, const char *topic_)
{
    if (!sub_) {
        errno = EINVAL;
        return -1;
    }
    std::string topic;
    if (!validate_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (_subs.count (sub_) == 0) {
            errno = EFAULT;
            return -1;
        }
        if (!sub_->_topics.insert (topic).second)
            return 0;
        _topic_index[topic].insert (sub_);

        add_filter (topic);
        request_control_tick ();
    }
    emit_sub_event (ZLINK_SPOT_SUB_FILTER_APPLIED, topic.c_str (), 1, 0);
    report_sub_topology (ZLINK_TOPOLOGY_STATE_READY, topic.c_str (), 1, 0);
    return 0;
}

int spot_node_t::subscribe_pattern (spot_sub_t *sub_, const char *pattern_)
{
    if (!sub_) {
        errno = EINVAL;
        return -1;
    }
    std::string prefix;
    if (!validate_pattern (pattern_, &prefix)) {
        errno = EINVAL;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (_subs.count (sub_) == 0) {
            errno = EFAULT;
            return -1;
        }
        if (!sub_->_patterns.insert (prefix).second)
            return 0;
        _pattern_subs.insert (sub_);
        add_filter (prefix);
        request_control_tick ();
    }
    emit_sub_event (ZLINK_SPOT_SUB_FILTER_APPLIED, prefix.c_str (), 1, 0);
    report_sub_topology (ZLINK_TOPOLOGY_STATE_READY, prefix.c_str (), 1, 0);
    return 0;
}

int spot_node_t::unsubscribe (spot_sub_t *sub_, const char *topic_or_pattern_)
{
    if (!sub_ || !topic_or_pattern_) {
        errno = EINVAL;
        return -1;
    }

    std::string prefix;
    if (validate_pattern (topic_or_pattern_, &prefix)) {
        scoped_lock_t lock (_sync);
        if (_subs.count (sub_) == 0) {
            errno = EFAULT;
            return -1;
        }
        if (sub_->_patterns.erase (prefix) == 0) {
            errno = EINVAL;
            return -1;
        }
        if (sub_->_patterns.empty ())
            _pattern_subs.erase (sub_);
        remove_filter (prefix);
        request_control_tick ();
        return 0;
    }

    std::string topic;
    if (!validate_topic (topic_or_pattern_, &topic)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_subs.count (sub_) == 0) {
        errno = EFAULT;
        return -1;
    }
    if (sub_->_topics.erase (topic) == 0) {
        errno = EINVAL;
        return -1;
    }
    std::map<std::string, std::set<spot_sub_t *> >::iterator idx =
      _topic_index.find (topic);
    if (idx != _topic_index.end ()) {
        idx->second.erase (sub_);
        if (idx->second.empty ())
            _topic_index.erase (idx);
    }
    remove_filter (topic);
    request_control_tick ();
    return 0;
}

int spot_node_t::publish (const char *topic_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          int flags_)
{
    if (ensure_pub_facade_mode () != 0)
        return -1;

    std::string topic;
    if (!validate_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    const int send_flags_base = flags_ & ZLINK_DONTWAIT;

    if (_pub_mode.get () == ZLINK_SPOT_NODE_PUB_MODE_ASYNC) {
        async_publish_t pending;
        pending.topic = topic;
        if (!copy_parts_from_msgv (parts_, part_count_, &pending.payload))
            return -1;

        bool accepted = false;
        bool dropped = false;
        {
            scoped_lock_t queue_lock (_pub_queue_sync);
            if (_pending_pub.size () >= _pub_queue_hwm) {
                if (_pub_queue_full_policy.get ()
                    == ZLINK_SPOT_NODE_PUB_QUEUE_FULL_DROP) {
                    dropped = true;
                } else {
                    errno = EAGAIN;
                }
            } else {
                _pending_pub.push_back (async_publish_t ());
                _pending_pub.back ().topic.swap (pending.topic);
                _pending_pub.back ().payload.swap (pending.payload);
                accepted = true;
            }
        }

        close_parts (&pending.payload);
        if (!accepted && !dropped) {
            emit_pub_event (ZLINK_SPOT_PUB_QUEUE_FULL, NULL,
                            static_cast<uint32_t> (_pub_queue_hwm), EAGAIN);
            return -1;
        }

        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        if (accepted)
            request_control_tick ();
        return 0;
    }

    std::vector<msg_t> payload;
    bool needs_local_dispatch = false;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::set<spot_sub_t *> >::const_iterator exact =
          _topic_index.find (topic);
        if (exact != _topic_index.end () && !exact->second.empty ())
            needs_local_dispatch = true;
        if (!needs_local_dispatch) {
            for (std::set<spot_sub_t *>::const_iterator it =
                   _pattern_subs.begin ();
                 it != _pattern_subs.end (); ++it) {
                if ((*it)->matches (topic)) {
                    needs_local_dispatch = true;
                    break;
                }
            }
        }
    }

    if (needs_local_dispatch) {
        if (!copy_parts_from_msgv (parts_, part_count_, &payload))
            return -1;

        scoped_lock_t lock (_sync);
        dispatch_local (topic, payload);
    }

    {
        scoped_lock_t pub_lock (_pub_sync);
        if (!_pub) {
            close_parts (&payload);
            for (size_t i = 0; i < part_count_; ++i)
                zlink_msg_close (&parts_[i]);
            return 0;
        }

        if (send_flags_base != 0) {
            int events = 0;
            size_t events_size = sizeof (events);
            if (_pub->getsockopt (ZLINK_EVENTS, &events, &events_size) != 0) {
                close_parts (&payload);
                return -1;
            }
            if ((events & ZLINK_POLLOUT) == 0) {
                close_parts (&payload);
                errno = EAGAIN;
                return -1;
            }
        }

        msg_t topic_frame;
        if (topic_frame.init_size (topic.size ()) != 0) {
            close_parts (&payload);
            return -1;
        }
        if (!topic.empty ())
            memcpy (topic_frame.data (), topic.data (), topic.size ());

        int flags = (part_count_ > 0 ? ZLINK_SNDMORE : 0) | send_flags_base;
        if (_pub->send (&topic_frame, flags) != 0) {
            topic_frame.close ();
            close_parts (&payload);
            return -1;
        }
        topic_frame.close ();

        for (size_t i = 0; i < part_count_; ++i) {
            msg_t &part = *reinterpret_cast<msg_t *> (&parts_[i]);
            flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
            flags |= send_flags_base;
            if (_pub->send (&part, flags) != 0) {
                close_parts (&payload);
                return -1;
            }
        }
    }

    close_parts (&payload);
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);

    return 0;
}

void spot_node_t::dispatch_local (const std::string &topic_,
                                  const std::vector<msg_t> &payload_)
{
    std::vector<spot_sub_t *> handler_targets;
    std::set<spot_sub_t *> matched;
    spot_shared_message_t *shared = NULL;

    std::map<std::string, std::set<spot_sub_t *> >::iterator exact =
      _topic_index.find (topic_);
    if (exact != _topic_index.end ())
        matched.insert (exact->second.begin (), exact->second.end ());

    for (std::set<spot_sub_t *>::iterator it = _pattern_subs.begin ();
         it != _pattern_subs.end (); ++it) {
        spot_sub_t *sub = *it;
        if (matched.count (sub))
            continue;
        if (!sub->matches (topic_))
            continue;
        matched.insert (sub);
    }

    for (std::set<spot_sub_t *>::iterator it = matched.begin ();
         it != matched.end (); ++it) {
        spot_sub_t *sub = *it;
        if (sub->callback_enabled ())
            handler_targets.push_back (sub);
        else {
            if (!shared) {
                shared = new (std::nothrow) spot_shared_message_t ();
                if (!shared)
                    break;
                shared->topic = topic_;
                if (!copy_parts_from_vec (payload_, &shared->parts)) {
                    delete shared;
                    shared = NULL;
                    break;
                }
            }
            sub->enqueue_shared_message (shared);
        }
    }
    release_shared_message (shared);
    enqueue_handler_delivery (topic_, payload_, handler_targets);
}

void spot_node_t::enqueue_handler_delivery (
  const std::string &topic_,
  const std::vector<msg_t> &payload_,
  const std::vector<spot_sub_t *> &targets_)
{
    if (targets_.empty ())
        return;

    handler_delivery_t delivery;
    delivery.topic = topic_;
    if (!copy_parts_from_vec (payload_, &delivery.payload))
        return;
    delivery.targets = targets_;
    _pending_handler_delivery.push_back (handler_delivery_t ());
    _pending_handler_delivery.back ().topic.swap (delivery.topic);
    _pending_handler_delivery.back ().payload.swap (delivery.payload);
    _pending_handler_delivery.back ().targets.swap (delivery.targets);
}

bool spot_node_t::pop_handler_delivery (handler_delivery_t *out_)
{
    if (!out_ || _pending_handler_delivery.empty ())
        return false;

    handler_delivery_t &front = _pending_handler_delivery.front ();
    out_->topic.swap (front.topic);
    out_->payload.swap (front.payload);
    out_->targets.swap (front.targets);
    _pending_handler_delivery.pop_front ();
    return true;
}

void spot_node_t::invoke_pending_callbacks (
  const std::string &topic_,
  const std::vector<msg_t> &payload_,
  const std::vector<spot_sub_t *> &targets_)
{
    struct pending_callback_t
    {
        spot_sub_t *sub;
        zlink_spot_sub_handler_fn handler;
        void *userdata;
    };

    std::vector<pending_callback_t> callbacks;
    callbacks.reserve (targets_.size ());
    {
        scoped_lock_t lock (_sync);
        for (size_t i = 0; i < targets_.size (); ++i) {
            spot_sub_t *sub = targets_[i];
            if (!sub || _subs.count (sub) == 0)
                continue;
            if (sub->_handler_state != spot_sub_t::handler_active
                || !sub->_handler)
                continue;
            sub->_callback_inflight.add (1);
            pending_callback_t cb;
            cb.sub = sub;
            cb.handler = sub->_handler;
            cb.userdata = sub->_handler_userdata;
            callbacks.push_back (cb);
        }
    }

    if (callbacks.empty ())
        return;

    std::vector<zlink_msg_t> msgv;
    if (!copy_parts_to_msgv (payload_, &msgv)) {
        for (size_t i = 0; i < callbacks.size (); ++i) {
            scoped_lock_t lock (_sync);
            spot_sub_t *sub = callbacks[i].sub;
            if (!sub->_callback_inflight.sub (1)
                && sub->_handler_state == spot_sub_t::handler_clearing) {
                sub->_handler_state = spot_sub_t::handler_none;
                sub->_callback_cv.broadcast ();
            }
        }
        return;
    }

    const zlink_msg_t *parts = msgv.empty () ? NULL : &msgv[0];
    for (size_t i = 0; i < callbacks.size (); ++i) {
        pending_callback_t cb = callbacks[i];
        cb.handler (topic_.data (), topic_.size (), parts, msgv.size (),
                    cb.userdata);

        scoped_lock_t lock (_sync);
        if (!cb.sub->_callback_inflight.sub (1)
            && cb.sub->_handler_state == spot_sub_t::handler_clearing) {
            cb.sub->_handler_state = spot_sub_t::handler_none;
            cb.sub->_callback_cv.broadcast ();
        }
    }

    close_msgv (&msgv);
}

bool spot_node_t::process_handler_delivery ()
{
    handler_delivery_t delivery;
    {
        scoped_lock_t lock (_sync);
        if (!pop_handler_delivery (&delivery))
            return false;
    }

    invoke_pending_callbacks (delivery.topic, delivery.payload,
                              delivery.targets);
    close_parts (&delivery.payload);
    return true;
}

bool spot_node_t::process_async_publish ()
{
    async_publish_t pending;
    bool drained = false;
    {
        scoped_lock_t queue_lock (_pub_queue_sync);
        if (_pending_pub.empty ())
            return false;
        pending.topic.swap (_pending_pub.front ().topic);
        pending.payload.swap (_pending_pub.front ().payload);
        _pending_pub.pop_front ();
        drained = _pending_pub.empty ();
    }

    {
        scoped_lock_t lock (_sync);
        dispatch_local (pending.topic, pending.payload);
    }

    {
        scoped_lock_t pub_lock (_pub_sync);
        if (_pub) {
            msg_t topic_frame;
            if (topic_frame.init_size (pending.topic.size ()) == 0) {
                if (!pending.topic.empty ()) {
                    memcpy (topic_frame.data (), pending.topic.data (),
                            pending.topic.size ());
                }

                int flags = !pending.payload.empty () ? ZLINK_SNDMORE : 0;
                if (_pub->send (&topic_frame, flags) == 0) {
                    for (size_t i = 0; i < pending.payload.size (); ++i) {
                        msg_t &part = pending.payload[i];
                        flags = (i + 1 < pending.payload.size ())
                                  ? ZLINK_SNDMORE
                                  : 0;
                        if (_pub->send (&part, flags) != 0)
                            break;
                    }
                }
                topic_frame.close ();
            }
        }
    }

    close_parts (&pending.payload);
    if (drained)
        emit_pub_event (ZLINK_SPOT_PUB_QUEUE_DRAINED, NULL, 0, 0);
    return true;
}

void spot_node_t::ensure_control_sockets ()
{
    if (!_sub) {
        _sub = _ctx->create_socket (ZLINK_SUB);
        if (_sub) {
            if (!apply_service_routing_id (
                  _sub, &_sub_routing_id_override, &_sub_routing_id)) {
                _sub->close ();
                _sub = NULL;
                return;
            }
            if (!_tls_ca.empty ()) {
                if (_sub->setsockopt (ZLINK_TLS_CA, _tls_ca.data (),
                                      _tls_ca.size ())
                      != 0
                    || _sub->setsockopt (ZLINK_TLS_HOSTNAME,
                                         _tls_hostname.data (),
                                         _tls_hostname.size ())
                         != 0
                    || _sub->setsockopt (ZLINK_TLS_TRUST_SYSTEM,
                                         &_tls_trust_system,
                                         sizeof (_tls_trust_system))
                         != 0) {
                    _sub->close ();
                    _sub = NULL;
                    return;
                }
            }
            for (size_t i = 0; i < _sub_opts.size (); ++i) {
                if (!_sub_opts[i].value.empty ())
                    _sub->setsockopt (_sub_opts[i].option,
                                      &_sub_opts[i].value[0],
                                      _sub_opts[i].value.size ());
            }
            scoped_lock_t lock (_sync);
            _pending_subscribe.clear ();
            _pending_unsubscribe.clear ();
            _pending_peer_connect.clear ();
            _pending_peer_disconnect.clear ();
            for (std::map<std::string, size_t>::const_iterator it =
                   _filter_refcount.begin ();
                 it != _filter_refcount.end (); ++it) {
                if (it->second > 0)
                    _pending_subscribe.push_back (it->first);
            }
            for (std::set<std::string>::const_iterator it =
                   _peer_endpoints.begin ();
                 it != _peer_endpoints.end (); ++it) {
                _pending_peer_connect.push_back (*it);
            }
        }
    }

    {
        scoped_lock_t dealer_lock (_dealer_sync);
        if (!_dealer) {
            _dealer = _ctx->create_socket (ZLINK_DEALER);
            if (_dealer) {
                if (getenv ("PERF_DEBUG"))
                    fprintf (stderr, "[spot-node] dealer created\n");
                _dealer->setsockopt (ZLINK_SNDTIMEO,
                                     &default_registry_control_send_timeout_ms,
                                     sizeof (default_registry_control_send_timeout_ms));
                _dealer->setsockopt (ZLINK_RCVTIMEO,
                                     &default_registry_control_recv_timeout_ms,
                                     sizeof (default_registry_control_recv_timeout_ms));
                if (!_tls_ca.empty ()) {
                    if (_dealer->setsockopt (ZLINK_TLS_CA, _tls_ca.data (),
                                             _tls_ca.size ())
                          != 0
                        || _dealer->setsockopt (ZLINK_TLS_HOSTNAME,
                                                _tls_hostname.data (),
                                                _tls_hostname.size ())
                             != 0
                        || _dealer->setsockopt (ZLINK_TLS_TRUST_SYSTEM,
                                                &_tls_trust_system,
                                                sizeof (_tls_trust_system))
                             != 0) {
                        _dealer->close ();
                        _dealer = NULL;
                        return;
                    }
                }
                for (size_t i = 0; i < _dealer_opts.size (); ++i) {
                    if (!_dealer_opts[i].value.empty ())
                        _dealer->setsockopt (_dealer_opts[i].option,
                                             &_dealer_opts[i].value[0],
                                             _dealer_opts[i].value.size ());
                }
                _dealer->setsockopt (ZLINK_ROUTING_ID, _routing_id.data,
                                     _routing_id.size);
                scoped_lock_t lock (_sync);
                _pending_registry_connect.clear ();
                for (std::set<std::string>::const_iterator it =
                       _registry_endpoints.begin ();
                     it != _registry_endpoints.end (); ++it) {
                    _pending_registry_connect.push_back (*it);
                }
            }
        }
    }
}

void spot_node_t::flush_pending ()
{
    std::deque<std::string> subscribe;
    std::deque<std::string> unsubscribe;
    std::deque<std::string> peer_connect;
    std::deque<std::string> peer_disconnect;
    std::deque<std::string> registry_connect;

    {
        scoped_lock_t lock (_sync);
        if (_sub) {
            subscribe.swap (_pending_subscribe);
            unsubscribe.swap (_pending_unsubscribe);
            peer_connect.swap (_pending_peer_connect);
            peer_disconnect.swap (_pending_peer_disconnect);
        }
        if (_dealer)
            registry_connect.swap (_pending_registry_connect);
    }

    if (_sub && _sub_pollable_mode.get () == 0) {
        for (std::deque<std::string>::const_iterator it =
               subscribe.begin ();
             it != subscribe.end (); ++it)
            _sub->setsockopt (ZLINK_SUBSCRIBE, it->data (), it->size ());
        for (std::deque<std::string>::const_iterator it =
               unsubscribe.begin ();
             it != unsubscribe.end (); ++it)
            _sub->setsockopt (ZLINK_UNSUBSCRIBE, it->data (), it->size ());
        for (std::deque<std::string>::const_iterator it =
               peer_connect.begin ();
             it != peer_connect.end (); ++it)
            if (_sub->connect (it->c_str ()) != 0 && getenv ("PERF_DEBUG")) {
                fprintf (stderr, "[spot-node] sub connect failed %s: %s\n",
                         it->c_str (), strerror (errno));
            }
        for (std::deque<std::string>::const_iterator it =
               peer_disconnect.begin ();
             it != peer_disconnect.end (); ++it)
            _sub->term_endpoint (it->c_str ());
    }

    if (_dealer) {
        scoped_lock_t dealer_lock (_dealer_sync);
        for (std::deque<std::string>::const_iterator it =
               registry_connect.begin ();
             it != registry_connect.end (); ++it)
            if (_dealer->connect (it->c_str ()) != 0 && getenv ("PERF_DEBUG")) {
                fprintf (stderr, "[spot-node] dealer connect failed %s: %s\n",
                         it->c_str (), strerror (errno));
            }
    }
}

void spot_node_t::process_sub ()
{
    if (!_sub)
        return;
    if (_sub_pollable_mode.get () != 0)
        return;

    while (true) {
        if (_sub_queue_nodrop) {
            bool queue_full = false;
            {
                scoped_lock_t lock (_sync);
                for (std::set<spot_sub_t *>::iterator it = _subs.begin ();
                     it != _subs.end (); ++it) {
                    spot_sub_t *sub = *it;
                    if (!sub || sub->callback_enabled ())
                        continue;

                    scoped_lock_t queue_lock (sub->_queue_sync);
                    if (sub->_queue_hwm > 0
                        && sub->_queue.size () >= sub->_queue_hwm) {
                        queue_full = true;
                        break;
                    }
                }
            }
            if (queue_full)
                return;
        }

        msg_t topic_frame;
        if (topic_frame.init () != 0)
            return;
        if (_sub->recv (&topic_frame, ZLINK_DONTWAIT) != 0) {
            topic_frame.close ();
            return;
        }

        const bool more = (topic_frame.flags () & msg_t::more) != 0;
        std::string topic;
        if (topic_frame.size () > 0) {
            const char *data = static_cast<const char *> (topic_frame.data ());
            topic.assign (data, data + topic_frame.size ());
        }
        topic_frame.close ();

        if (!more)
            continue;

        std::vector<msg_t> payload;
        bool has_more = more;
        while (has_more) {
            msg_t part;
            if (part.init () != 0) {
                close_parts (&payload);
                break;
            }
            if (_sub->recv (&part, ZLINK_DONTWAIT) != 0) {
                part.close ();
                close_parts (&payload);
                break;
            }
            has_more = (part.flags () & msg_t::more) != 0;
            payload.push_back (msg_t ());
            payload.back ().init ();
            payload.back ().move (part);
        }

        if (!topic.empty ()) {
            scoped_lock_t lock (_sync);
            dispatch_local (topic, payload);
        }
        close_parts (&payload);
    }
}

void spot_node_t::refresh_peers ()
{
    discovery_t *disc = NULL;
    std::string service;
    std::string self_advertise;
    {
        scoped_lock_t lock (_sync);
        disc = _discovery;
        service = _discovery_service;
        self_advertise = _advertise_endpoint;
    }
    if (!disc || !_sub || _sub_pollable_mode.get () != 0)
        return;

    std::vector<provider_info_t> providers;
    disc->snapshot_providers (service, &providers);

    std::set<std::string> next;
    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &entry = providers[i];
        if (entry.endpoint.empty ())
            continue;
        if (!self_advertise.empty () && entry.endpoint == self_advertise)
            continue;
        next.insert (entry.endpoint);
    }

    std::vector<std::string> to_connect;
    std::vector<std::string> to_disconnect;
    {
        scoped_lock_t lock (_sync);
        for (std::set<std::string>::iterator it = next.begin ();
             it != next.end (); ++it) {
            if (_peer_endpoints.count (*it) == 0)
                to_connect.push_back (*it);
        }
        for (std::set<std::string>::iterator it = _peer_endpoints.begin ();
             it != _peer_endpoints.end (); ++it) {
            if (next.count (*it) == 0)
                to_disconnect.push_back (*it);
        }
        _peer_endpoints = next;
    }

    for (size_t i = 0; i < to_connect.size (); ++i) {
        if (_sub->connect (to_connect[i].c_str ()) != 0
            && getenv ("PERF_DEBUG")) {
            fprintf (stderr, "[spot-node] refresh connect failed %s: %s\n",
                     to_connect[i].c_str (), strerror (errno));
        }
    }
    for (size_t i = 0; i < to_disconnect.size (); ++i)
        _sub->term_endpoint (to_disconnect[i].c_str ());
}

bool spot_node_t::suspend_control_task ()
{
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime || _task_id == 0 || runtime->is_current_thread ())
        return false;

    const uint64_t task_id = _task_id;
    _task_id = 0;
    runtime->remove_task (task_id);
    return true;
}

void spot_node_t::resume_control_task ()
{
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime || _task_id != 0)
        return;

    _task_id = runtime->add_periodic_task (control_task, this, _control_tick_ms,
                                           true);
}

void spot_node_t::send_heartbeat (uint64_t now_ms_)
{
    socket_base_t *dealer = NULL;
    std::string service;
    std::string endpoint;
    {
        scoped_lock_t lock (_sync);
        if (!_registered || !_dealer)
            return;
        dealer = _dealer;
        service = _service_name;
        endpoint = _advertise_endpoint;
    }

    int rc = -1;
    {
        scoped_lock_t dealer_lock (_dealer_sync);
        rc =
          send_u16 (dealer, discovery_protocol::msg_heartbeat, ZLINK_SNDMORE)
                    == 0
                && send_u16 (dealer, discovery_protocol::service_type_spot_node,
                             ZLINK_SNDMORE)
                     == 0
                && send_string (dealer, service, ZLINK_SNDMORE) == 0
                && send_string (dealer, endpoint, 0) == 0
              ? 0
              : -1;
    }

    if (rc == 0) {
        scoped_lock_t lock (_sync);
        _last_heartbeat_ms = now_ms_;
    } else if (getenv ("PERF_DEBUG")) {
        fprintf (stderr, "[spot-node] heartbeat send failed: %s\n",
                 strerror (errno));
    }
}

bool spot_node_t::is_control_thread () const
{
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    return runtime && runtime->is_current_thread ();
}

void spot_node_t::request_control_tick ()
{
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->wakeup_task (_task_id);
}

void spot_node_t::control_task (void *arg_)
{
    spot_node_t *self = static_cast<spot_node_t *> (arg_);
    self->control_tick ();
}

void spot_node_t::control_tick ()
{
    if (_stop.get () != 0)
        return;

    ensure_control_sockets ();
    flush_pending ();

    process_sub ();
    while (process_handler_delivery ()) {
    }
    while (process_async_publish ()) {
    }
    while (process_handler_delivery ()) {
    }

    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();
    bool do_heartbeat = false;
    bool do_refresh = false;
    {
        scoped_lock_t lock (_sync);
        if (_registered && (now - _last_heartbeat_ms) >= _heartbeat_interval_ms)
            do_heartbeat = true;
        if (_discovery && now >= _next_discovery_refresh_ms) {
            _next_discovery_refresh_ms = now + discovery_refresh_ms;
            do_refresh = true;
        }
    }

    if (do_heartbeat)
    {
        if (getenv ("PERF_DEBUG"))
            fprintf (stderr, "[spot-node] heartbeat due\n");
        send_heartbeat (now);
    }

    if (do_refresh)
        refresh_peers ();
}

int spot_node_t::destroy ()
{
    _stop.set (1);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->remove_task (_task_id);
    _task_id = 0;

    socket_base_t *dealer = NULL;
    socket_base_t *pub = NULL;
    socket_base_t *sub = NULL;
    std::deque<async_publish_t> pending_pub;
    {
        scoped_lock_t lock (_sync);
        dealer = _dealer;
        _dealer = NULL;
        pub = _pub;
        _pub = NULL;
        sub = _sub;
        _sub = NULL;

        for (std::set<spot_pub_t *>::iterator it = _pubs.begin ();
             it != _pubs.end (); ++it)
            (*it)->_node = NULL;
        for (std::set<spot_sub_t *>::iterator it = _subs.begin ();
             it != _subs.end (); ++it)
            (*it)->_node = NULL;
        _pubs.clear ();
        _subs.clear ();
        for (std::deque<handler_delivery_t>::iterator it =
               _pending_handler_delivery.begin ();
             it != _pending_handler_delivery.end (); ++it) {
            close_parts (&it->payload);
        }
        _pending_handler_delivery.clear ();
        _filter_refcount.clear ();
        _topic_index.clear ();
        _pattern_subs.clear ();
        _peer_endpoints.clear ();
        _registry_endpoints.clear ();
        _bind_endpoints.clear ();
    }
    {
        scoped_lock_t queue_lock (_pub_queue_sync);
        pending_pub.swap (_pending_pub);
    }

    zlink_service_event_t pub_terminal;
    memset (&pub_terminal, 0, sizeof (pub_terminal));
    pub_terminal.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    pub_terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
    pub_terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    pub_terminal.routing_id = _pub_routing_id;
    _pub_monitor.close_all (&pub_terminal);

    zlink_service_event_t sub_terminal;
    memset (&sub_terminal, 0, sizeof (sub_terminal));
    sub_terminal.service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    sub_terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
    sub_terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    sub_terminal.routing_id = _sub_routing_id;
    _sub_monitor.close_all (&sub_terminal);

    if (dealer)
        dealer->close ();
    if (pub) {
        scoped_lock_t pub_lock (_pub_sync);
        pub->close ();
    }
    if (sub)
        sub->close ();
    for (std::deque<async_publish_t>::iterator it = pending_pub.begin ();
         it != pending_pub.end (); ++it)
        close_parts (&it->payload);
    return 0;
}
}
