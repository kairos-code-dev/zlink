/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/gateway/receiver.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include "utils/err.hpp"
#include "utils/random.hpp"
#include "services/gateway/routing_id_utils.hpp"

#if defined ZLINK_HAVE_WINDOWS
#include "utils/windows.hpp"
#else
#include <unistd.h>
#endif

#include <string.h>
#include <vector>

namespace zlink
{
static const uint32_t provider_tag_value = 0x1e6700d8;

static void sleep_ms (int ms_)
{
#if defined ZLINK_HAVE_WINDOWS
    Sleep (ms_);
#else
    usleep (static_cast<useconds_t> (ms_) * 1000);
#endif
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

static int recv_register_ack (socket_base_t *socket_,
                              int *status_out_,
                              std::string *resolved_out_,
                              std::string *error_out_)
{
    if (!socket_ || !status_out_) {
        errno = EINVAL;
        return -1;
    }

    *status_out_ = -1;
    if (resolved_out_)
        resolved_out_->clear ();
    if (error_out_)
        error_out_->clear ();

    zlink_msg_t reply;
    zlink_msg_init (&reply);
    if (socket_->recv (reinterpret_cast<msg_t *> (&reply), 0) != 0) {
        zlink_msg_close (&reply);
        return -1;
    }

    std::vector<zlink_msg_t> frames;
    frames.push_back (reply);
    while (zlink_msg_more (&frames.back ())) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        if (socket_->recv (reinterpret_cast<msg_t *> (&frame), 0) != 0) {
            zlink_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
    }

    uint16_t msg_id = 0;
    if (frames.size () >= 2
        && discovery_protocol::read_u16 (frames[0], &msg_id)
        && msg_id == discovery_protocol::msg_register_ack) {
        uint8_t status = 0xFF;
        if (zlink_msg_size (&frames[1]) == sizeof (uint8_t))
            memcpy (&status, zlink_msg_data (&frames[1]), sizeof (uint8_t));
        *status_out_ = static_cast<int> (status);
        if (resolved_out_ && frames.size () >= 3)
            *resolved_out_ = discovery_protocol::read_string (frames[2]);
        if (error_out_ && frames.size () >= 4)
            *error_out_ = discovery_protocol::read_string (frames[3]);
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zlink_msg_close (&frames[i]);

    return 0;
}

provider_t::provider_t (ctx_t *ctx_, const char *routing_id_) :
    _ctx (ctx_),
    _tag (provider_tag_value),
    _router (NULL),
    _dealer (NULL),
    _routing_id_override (routing_id_ ? routing_id_ : ""),
    _weight (1),
    _last_status (-1),
    _heartbeat_interval_ms (5000),
    _stop (0)
{
    zlink_assert (_ctx);
    _routing_id.size = 0;
    _router = _ctx->create_socket (ZLINK_ROUTER);
    _dealer = _ctx->create_socket (ZLINK_DEALER);
    if (!_router || !_dealer) {
        if (_router) {
            _router->close ();
            _router = NULL;
        }
        if (_dealer) {
            _dealer->close ();
            _dealer = NULL;
        }
        _tag = 0xdeadbeef;
    } else {
        zlink::discovery::set_socket_routing_id (_router, &_routing_id_override,
                                                 NULL);
    int hwm = 1000000;
        _router->setsockopt (ZLINK_SNDHWM, &hwm, sizeof (hwm));
        _router->setsockopt (ZLINK_RCVHWM, &hwm, sizeof (hwm));
        // Allow a reconnecting gateway with the same routing id to take over.
        int handover = 1;
        _router->setsockopt (ZLINK_ROUTER_HANDOVER, &handover,
                              sizeof (handover));
    }
}

provider_t::~provider_t ()
{
    _tag = 0xdeadbeef;
}

bool provider_t::check_tag () const
{
    return _tag == provider_tag_value;
}

static int create_socket (ctx_t *ctx_, int type_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (type_);
    if (!*socket_)
        return -1;
    return 0;
}

int provider_t::bind (const char *endpoint_)
{
    if (!endpoint_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_router) {
        errno = ENOTSUP;
        return -1;
    }
    if (!_tls_cert.empty ()) {
        if (_router->setsockopt (ZLINK_TLS_CERT, _tls_cert.data (),
                                 _tls_cert.size ())
              != 0
            || _router->setsockopt (ZLINK_TLS_KEY, _tls_key.data (),
                                    _tls_key.size ())
                 != 0)
            return -1;
    }

    _bind_endpoint = endpoint_;
    return _router->bind (endpoint_);
}

bool provider_t::ensure_routing_id ()
{
    if (!_router)
        return false;

    zlink_routing_id_t rid;
    size_t size = sizeof (rid.data);
    int rc = zlink_getsockopt (static_cast<void *> (_router), ZLINK_ROUTING_ID,
                               rid.data, &size);
    if (rc == 0) {
        rid.size = static_cast<uint8_t> (size);
        if (rid.size > 0) {
            _routing_id = rid;
            return true;
        }
    }

    if (!zlink::discovery::set_socket_routing_id (
          _router, &_routing_id_override, &_routing_id))
        return false;
    return true;
}

int provider_t::connect_registry (const char *registry_router_endpoint_)
{
    if (!registry_router_endpoint_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_dealer) {
        if (create_socket (_ctx, ZLINK_DEALER, &_dealer) != 0)
            return -1;
    }

    if (!ensure_routing_id ()) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t rid;
    size_t size = sizeof (rid.data);
    int rc =
      zlink_getsockopt (static_cast<void *> (_router), ZLINK_ROUTING_ID,
                        rid.data, &size);
    if (rc == 0) {
        rid.size = static_cast<uint8_t> (size);
        if (rid.size > 0)
            _dealer->setsockopt (ZLINK_ROUTING_ID, rid.data, rid.size);
    }

    _registry_endpoint = registry_router_endpoint_;
    return _dealer->connect (registry_router_endpoint_);
}

std::string provider_t::resolve_advertise (const char *advertise_endpoint_)
{
    if (advertise_endpoint_ && advertise_endpoint_[0] != '\0')
        return advertise_endpoint_;

    if (_bind_endpoint.empty ())
        return std::string ();

    std::string endpoint = _bind_endpoint;
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

int provider_t::register_service (const char *service_name_,
                                  const char *advertise_endpoint_,
                                  uint32_t weight_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_dealer) {
        errno = ENOTSUP;
        return -1;
    }

    _service_name = service_name_;
    _advertise_endpoint = resolve_advertise (advertise_endpoint_);
    if (_advertise_endpoint.empty ()) {
        errno = EINVAL;
        return -1;
    }
    _weight = weight_ == 0 ? 1 : weight_;

    if (send_u16 (_dealer, discovery_protocol::msg_register, ZLINK_SNDMORE)
          != 0
        || send_u16 (_dealer,
                     discovery_protocol::service_type_gateway_receiver,
                     ZLINK_SNDMORE)
             != 0
        || send_string (_dealer, _service_name, ZLINK_SNDMORE) != 0
        || send_string (_dealer, _advertise_endpoint, ZLINK_SNDMORE) != 0
        || send_u32 (_dealer, _weight, 0) != 0)
        return -1;

    std::string resolved;
    std::string error;
    if (recv_register_ack (_dealer, &_last_status, &resolved, &error) != 0)
        return -1;
    _last_resolved.swap (resolved);
    _last_error.swap (error);

    if (_last_status != 0) {
        errno = EINVAL;
        return -1;
    }

    if (!_heartbeat_thread.get_started ()) {
        _stop.set (0);
        _heartbeat_thread.start (heartbeat_worker, this, "provbeat");
    }

    return 0;
}

int provider_t::update_weight (const char *service_name_, uint32_t weight_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_dealer) {
        errno = ENOTSUP;
        return -1;
    }

    const uint32_t value = weight_ == 0 ? 1 : weight_;
    if (send_u16 (_dealer, discovery_protocol::msg_update_weight,
                  ZLINK_SNDMORE)
          != 0
        || send_u16 (_dealer,
                     discovery_protocol::service_type_gateway_receiver,
                     ZLINK_SNDMORE)
             != 0
        || send_string (_dealer, service_name_, ZLINK_SNDMORE) != 0
        || send_string (_dealer, _advertise_endpoint, ZLINK_SNDMORE) != 0
        || send_u32 (_dealer, value, 0) != 0)
        return -1;

    int status = -1;
    if (recv_register_ack (_dealer, &status, NULL, NULL) != 0)
        return -1;
    if (status != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int provider_t::unregister_service (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_dealer) {
        errno = ENOTSUP;
        return -1;
    }

    if (send_u16 (_dealer, discovery_protocol::msg_unregister, ZLINK_SNDMORE)
          != 0
        || send_u16 (_dealer,
                     discovery_protocol::service_type_gateway_receiver,
                     ZLINK_SNDMORE)
             != 0
        || send_string (_dealer, service_name_, ZLINK_SNDMORE) != 0
        || send_string (_dealer, _advertise_endpoint, 0) != 0)
        return -1;
    return 0;
}

int provider_t::register_result (const char *service_name_,
                                 int *status_,
                                 char *resolved_endpoint_,
                                 char *error_message_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (status_)
        *status_ = _last_status;
    if (resolved_endpoint_) {
        memset (resolved_endpoint_, 0, 256);
        strncpy (resolved_endpoint_, _last_resolved.c_str (), 255);
    }
    if (error_message_) {
        memset (error_message_, 0, 256);
        strncpy (error_message_, _last_error.c_str (), 255);
    }
    return 0;
}

int provider_t::set_tls_server (const char *cert_, const char *key_)
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

    if (_router) {
        if (_router->setsockopt (ZLINK_TLS_CERT, _tls_cert.data (),
                                 _tls_cert.size ())
              != 0
            || _router->setsockopt (ZLINK_TLS_KEY, _tls_key.data (),
                                    _tls_key.size ())
                 != 0)
            return -1;
    }
    return 0;
}

int provider_t::set_socket_option (int socket_role_,
                                   int option_,
                                   const void *optval_,
                                   size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    socket_base_t *target = NULL;
    if (socket_role_ == ZLINK_RECEIVER_SOCKET_ROUTER)
        target = _router;
    else if (socket_role_ == ZLINK_RECEIVER_SOCKET_DEALER)
        target = _dealer;
    else {
        errno = EINVAL;
        return -1;
    }

    if (!target) {
        errno = ENOTSUP;
        return -1;
    }

    return target->setsockopt (option_, optval_, optvallen_);
}

void *provider_t::router ()
{
    scoped_lock_t lock (_sync);
    if (!_router)
        return NULL;
    return static_cast<void *> (_router);
}

void provider_t::heartbeat_worker (void *arg_)
{
    provider_t *self = static_cast<provider_t *> (arg_);
    self->send_heartbeat ();
}

void provider_t::send_heartbeat ()
{
    socket_base_t *dealer = NULL;
    std::string last_registry;
    zlink_routing_id_t rid;
    rid.size = 0;
    while (_stop.get () == 0) {
        std::string registry;
        std::string service;
        std::string advertise;
        {
            scoped_lock_t lock (_sync);
            registry = _registry_endpoint;
            service = _service_name;
            advertise = _advertise_endpoint;
            rid = _routing_id;
        }

        if (registry != last_registry) {
            if (dealer) {
                dealer->close ();
                dealer = NULL;
            }
            last_registry = registry;
        }

        if (!dealer && !registry.empty ()) {
            dealer = _ctx->create_socket (ZLINK_DEALER);
            if (dealer) {
                if (rid.size > 0)
                    dealer->setsockopt (ZLINK_ROUTING_ID, rid.data, rid.size);
                dealer->connect (registry.c_str ());
            }
        }

        if (dealer && !service.empty () && !advertise.empty ()) {
            send_u16 (dealer, discovery_protocol::msg_heartbeat,
                      ZLINK_SNDMORE);
            send_u16 (dealer,
                      discovery_protocol::service_type_gateway_receiver,
                      ZLINK_SNDMORE);
            send_string (dealer, service, ZLINK_SNDMORE);
            send_string (dealer, advertise, 0);
        }

        uint32_t remaining = _heartbeat_interval_ms;
        while (remaining > 0 && _stop.get () == 0) {
            const uint32_t chunk = remaining > 100 ? 100 : remaining;
            sleep_ms (static_cast<int> (chunk));
            remaining -= chunk;
        }
    }

    if (dealer) {
        dealer->close ();
    }
}

int provider_t::destroy ()
{
    _stop.set (1);
    if (_heartbeat_thread.get_started ())
        _heartbeat_thread.stop ();

    scoped_lock_t lock (_sync);
    if (_dealer) {
        _dealer->close ();
        _dealer = NULL;
    }
    if (_router) {
        _router->close ();
        _router = NULL;
    }
    return 0;
}
}
