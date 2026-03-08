/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/gateway/receiver.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/control/service_control_runtime.hpp"

#include "utils/clock.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"
#include "services/gateway/routing_id_utils.hpp"

#include <string.h>
#include <vector>
#include <stdlib.h>

namespace zlink
{
static const uint32_t receiver_tag_value = 0x1e6700d8;

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

static int recv_status_ack (socket_base_t *socket_,
                            uint16_t expected_msg_id_,
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
        && msg_id == expected_msg_id_) {
        uint8_t status = 0xFF;
        if (zlink_msg_size (&frames[1]) == sizeof (uint8_t))
            memcpy (&status, zlink_msg_data (&frames[1]), sizeof (uint8_t));
        *status_out_ = static_cast<int> (status);
        if (resolved_out_ && frames.size () >= 3
            && expected_msg_id_ == discovery_protocol::msg_register_ack)
            *resolved_out_ = discovery_protocol::read_string (frames[2]);
        if (error_out_) {
            if (expected_msg_id_ == discovery_protocol::msg_register_ack
                && frames.size () >= 4) {
                *error_out_ = discovery_protocol::read_string (frames[3]);
            } else if (expected_msg_id_ == discovery_protocol::msg_unregister_ack
                       && frames.size () >= 3) {
                *error_out_ = discovery_protocol::read_string (frames[2]);
            }
        }
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zlink_msg_close (&frames[i]);

    return 0;
}

static void close_msg_parts (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

receiver_t::receiver_t (ctx_t *ctx_, const char *routing_id_) :
    _ctx (ctx_),
    _tag (receiver_tag_value),
    _router (NULL),
    _dealer (NULL),
    _routing_id_override (routing_id_ ? routing_id_ : ""),
    _routing_id_locked (false),
    _weight (1),
    _last_status (-1),
    _stop (0),
    _monitor (ctx_)
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
        // Allow a reconnecting gateway with the same routing id to take over.
        int handover = 1;
        _router->setsockopt (ZLINK_ROUTER_HANDOVER, &handover,
                              sizeof (handover));
    }
}

receiver_t::~receiver_t ()
{
    _tag = 0xdeadbeef;
}

bool receiver_t::check_tag () const
{
    return _tag == receiver_tag_value;
}

static int create_socket (ctx_t *ctx_, int type_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (type_);
    if (!*socket_)
        return -1;
    return 0;
}

int receiver_t::bind (const char *endpoint_)
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

bool receiver_t::ensure_routing_id ()
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

int receiver_t::connect_registry (const char *registry_router_endpoint_)
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

std::string receiver_t::resolve_advertise (const char *advertise_endpoint_)
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

int receiver_t::register_service (const char *service_name_,
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
    if (recv_status_ack (_dealer, discovery_protocol::msg_register_ack,
                         &_last_status, &resolved, &error)
        != 0)
        return -1;
    _last_resolved.swap (resolved);
    _last_error.swap (error);

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    ev.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    ev.event_type = _last_status == 0 ? ZLINK_RECEIVER_REGISTER_OK
                                      : ZLINK_RECEIVER_REGISTER_FAILED;
    ev.status = _last_status;
    ev.error_code = _last_status == 0 ? 0 : EINVAL;
    ev.value = _weight;
    ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                      | ZLINK_EVENT_DETAIL_ENDPOINT
                      | ZLINK_EVENT_DETAIL_SUBJECT_RID;
    ev.routing_id = _routing_id;
    strncpy (ev.service_name, _service_name.c_str (),
             sizeof (ev.service_name) - 1);
    strncpy (ev.endpoint, _advertise_endpoint.c_str (),
             sizeof (ev.endpoint) - 1);
    _monitor.emit (ev);

    if (_last_status != 0) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int receiver_t::update_weight (const char *service_name_, uint32_t weight_)
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
    if (send_u16 (_dealer, discovery_protocol::msg_update_weight, ZLINK_SNDMORE)
        != 0)
        return -1;
    if (send_u16 (_dealer, discovery_protocol::service_type_gateway_receiver,
                  ZLINK_SNDMORE)
        != 0)
        return -1;
    if (send_string (_dealer, service_name_, ZLINK_SNDMORE) != 0)
        return -1;
    if (send_string (_dealer, _advertise_endpoint, ZLINK_SNDMORE) != 0)
        return -1;
    if (send_u32 (_dealer, value, 0) != 0)
        return -1;

    int status = -1;
    if (recv_status_ack (_dealer, discovery_protocol::msg_register_ack, &status,
                         NULL, NULL)
        != 0)
        return -1;
    if (status != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int receiver_t::unregister_service (const char *service_name_)
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
        {
            zlink_service_event_t ev;
            memset (&ev, 0, sizeof (ev));
            ev.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
            ev.event_type = ZLINK_RECEIVER_UNREGISTER_FAILED;
            ev.status = -1;
            ev.error_code = errno;
            ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                              | ZLINK_EVENT_DETAIL_SUBJECT_RID;
            ev.routing_id = _routing_id;
            strncpy (ev.service_name, service_name_,
                     sizeof (ev.service_name) - 1);
            _monitor.emit (ev);
            return -1;
        }

    int status = -1;
    std::string error;
    if (recv_status_ack (_dealer, discovery_protocol::msg_unregister_ack,
                         &status, NULL, &error)
        != 0) {
        zlink_service_event_t ev;
        memset (&ev, 0, sizeof (ev));
        ev.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
        ev.event_type = ZLINK_RECEIVER_UNREGISTER_FAILED;
        ev.status = -1;
        ev.error_code = errno;
        ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                          | ZLINK_EVENT_DETAIL_SUBJECT_RID;
        ev.routing_id = _routing_id;
        strncpy (ev.service_name, service_name_, sizeof (ev.service_name) - 1);
        _monitor.emit (ev);
        return -1;
    }

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    ev.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    ev.event_type = status == 0 ? ZLINK_RECEIVER_UNREGISTER_OK
                                : ZLINK_RECEIVER_UNREGISTER_FAILED;
    ev.status = status == 0 ? 0 : -1;
    ev.error_code = status == 0 ? 0 : EINVAL;
    ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                      | ZLINK_EVENT_DETAIL_SUBJECT_RID;
    ev.routing_id = _routing_id;
    strncpy (ev.service_name, service_name_, sizeof (ev.service_name) - 1);
    _monitor.emit (ev);
    if (status != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int receiver_t::register_result (const char *service_name_,
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

int receiver_t::set_tls_server (const char *cert_, const char *key_)
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

int receiver_t::recv (zlink_msg_t **parts_,
                      size_t *part_count_,
                      int flags_,
                      zlink_routing_id_t *routing_id_out_)
{
    if (!parts_ || !part_count_) {
        errno = EINVAL;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    scoped_lock_t lock (_sync);
    lock_routing_id ();
    if (!_router) {
        errno = ENOTSUP;
        return -1;
    }

    if (routing_id_out_)
        routing_id_out_->size = 0;

    zlink_msg_t rid_frame;
    if (zlink_msg_init (&rid_frame) != 0) {
        errno = EFAULT;
        return -1;
    }
    const int rc = zlink_msg_recv (&rid_frame, _router, flags_);
    if (rc < 0) {
        zlink_msg_close (&rid_frame);
        return -1;
    }

    if (routing_id_out_) {
        const size_t rid_size = zlink_msg_size (&rid_frame);
        size_t copy_size = rid_size;
        if (copy_size > sizeof (routing_id_out_->data))
            copy_size = sizeof (routing_id_out_->data);
        if (copy_size > 0) {
            memcpy (routing_id_out_->data, zlink_msg_data (&rid_frame),
                    copy_size);
            routing_id_out_->size = static_cast<uint8_t> (copy_size);
        }
    }

    const int more = zlink_msg_more (&rid_frame);
    zlink_msg_close (&rid_frame);

    if (!more) {
        *parts_ = NULL;
        *part_count_ = 0;
        return 0;
    }

    std::vector<zlink_msg_t> tmp_parts;
    while (true) {
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0) {
            errno = EFAULT;
            close_msg_parts (&tmp_parts);
            return -1;
        }
        const int prc = zlink_msg_recv (&part, _router, flags_);
        if (prc < 0) {
            zlink_msg_close (&part);
            close_msg_parts (&tmp_parts);
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
        close_msg_parts (&tmp_parts);
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < out_count; ++i) {
        if (zlink_msg_init (&out[i]) != 0
            || zlink_msg_move (&out[i], &tmp_parts[i]) != 0) {
            for (size_t j = 0; j <= i && j < out_count; ++j)
                zlink_msg_close (&out[j]);
            free (out);
            close_msg_parts (&tmp_parts);
            errno = EFAULT;
            return -1;
        }
    }

    *parts_ = out;
    *part_count_ = out_count;
    return 0;
}

int receiver_t::last_endpoint (char *endpoint_out_, size_t *size_out_) const
{
    if (!endpoint_out_ || !size_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (!_router) {
        errno = ENOTSUP;
        return -1;
    }

    return zlink_getsockopt (static_cast<void *> (_router), ZLINK_LAST_ENDPOINT,
                             endpoint_out_, size_out_);
}

int receiver_t::peer_info (const zlink_routing_id_t *routing_id_,
                           zlink_peer_info_t *info_out_) const
{
    if (!routing_id_ || !info_out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (!_router) {
        errno = ENOTSUP;
        return -1;
    }

    return zlink_socket_peer_info (static_cast<void *> (_router), routing_id_,
                                   info_out_);
}

int receiver_t::set_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id_locked || !_bind_endpoint.empty ()
        || !_registry_endpoint.empty ()
        || !_service_name.empty ()) {
        errno = EFSM;
        return -1;
    }

    _routing_id_override.assign (static_cast<const char *> (data_), size_);
    memcpy (_routing_id.data, data_, size_);
    _routing_id.size = static_cast<uint8_t> (size_);
    if (_router)
        return _router->setsockopt (ZLINK_ROUTING_ID, data_, size_);
    return 0;
}

int receiver_t::routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_routing_id.size == 0) {
        size_t size = sizeof (out_->data);
        if (!_router
            || zlink_getsockopt (static_cast<void *> (_router), ZLINK_ROUTING_ID,
                                 out_->data, &size)
                 != 0)
            return -1;
        out_->size = static_cast<uint8_t> (size);
        return 0;
    }
    *out_ = _routing_id;
    return 0;
}

int receiver_t::set_option (int option_,
                            const void *optval_,
                            size_t optvallen_)
{
    switch (option_) {
        case ZLINK_RECEIVER_OPT_SNDHWM:
            return set_socket_option (receiver_socket_router, ZLINK_SNDHWM,
                                      optval_, optvallen_);
        case ZLINK_RECEIVER_OPT_RCVHWM:
            return set_socket_option (receiver_socket_router, ZLINK_RCVHWM,
                                      optval_, optvallen_);
        case ZLINK_RECEIVER_OPT_SNDTIMEO:
            return set_socket_option (receiver_socket_router,
                                      ZLINK_SNDTIMEO, optval_, optvallen_);
        case ZLINK_RECEIVER_OPT_RCVTIMEO:
            return set_socket_option (receiver_socket_router,
                                      ZLINK_RCVTIMEO, optval_, optvallen_);
        case ZLINK_RECEIVER_OPT_LINGER:
            return set_socket_option (receiver_socket_router,
                                      ZLINK_LINGER, optval_, optvallen_);
        default:
            errno = EINVAL;
            return -1;
    }
}

void *receiver_t::monitor_open (int events_)
{
    return _monitor.open (events_);
}

int receiver_t::set_socket_option (int socket_role_,
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
    if (socket_role_ == receiver_socket_router)
        target = _router;
    else if (socket_role_ == receiver_socket_dealer)
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

void *receiver_t::router ()
{
    scoped_lock_t lock (_sync);
    lock_routing_id ();
    if (!_router)
        return NULL;
    return static_cast<void *> (_router);
}

void *receiver_t::poller_socket ()
{
    scoped_lock_t lock (_sync);
    lock_routing_id ();
    if (!_router)
        return NULL;
    return static_cast<void *> (_router);
}

void receiver_t::lock_routing_id ()
{
    _routing_id_locked = true;
}

int receiver_t::destroy ()
{
    _stop.set (1);

    zlink_service_event_t terminal;
    memset (&terminal, 0, sizeof (terminal));
    terminal.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
    terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    terminal.routing_id = _routing_id;
    _monitor.close_all (&terminal);

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
