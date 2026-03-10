/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/gateway/receiver.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"

#include "utils/clock.hpp"
#include "utils/err.hpp"
#include "services/gateway/routing_id_utils.hpp"

#include <string.h>
#include <vector>
#include <stdlib.h>

namespace zlink
{
static const uint32_t receiver_tag_value = 0x1e6700d8;

static void close_msg_parts (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

static void build_receiver_topology_entry (
  zlink_registry_topology_entry_t *entry_,
  const zlink_routing_id_t &routing_id_,
  const std::string &service_name_,
  const std::string &endpoint_,
  uint16_t state_,
  int32_t error_code_)
{
    memset (entry_, 0, sizeof (*entry_));
    entry_->routing_id = routing_id_;
    entry_->service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    strncpy (entry_->service_name, service_name_.c_str (),
             sizeof (entry_->service_name) - 1);
    if (!endpoint_.empty ())
        strncpy (entry_->endpoint, endpoint_.c_str (),
                 sizeof (entry_->endpoint) - 1);
    entry_->source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry_->state = state_;
    entry_->desired_count = 1;
    entry_->ready_count = state_ == ZLINK_TOPOLOGY_STATE_READY ? 1u : 0u;
    entry_->error_code = static_cast<uint32_t> (error_code_);
    entry_->last_reported_ms = clock_t ().now_ms ();
}

receiver_t::receiver_t (ctx_t *ctx_, const char *routing_id_) :
    _ctx (ctx_),
    _tag (receiver_tag_value),
    _router (NULL),
    _discovery (NULL),
    _owns_discovery (false),
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
    if (!_router) {
        if (_router) {
            _router->close ();
            _router = NULL;
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

discovery_t *receiver_t::ensure_owned_discovery ()
{
    if (_discovery)
        return _discovery;

    discovery_t *discovery =
      new (std::nothrow) discovery_t (_ctx,
                                      discovery_protocol::service_type_gateway_receiver);
    if (!discovery) {
        errno = ENOMEM;
        return NULL;
    }
    discovery->set_discovery_summary_enabled (false);
    _discovery = discovery;
    _owns_discovery = true;
    return _discovery;
}

int receiver_t::connect_registry (const char *registry_router_endpoint_)
{
    if (!registry_router_endpoint_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    discovery_t *discovery = ensure_owned_discovery ();
    if (!discovery)
        return -1;
    return discovery->connect_registry (registry_router_endpoint_);
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

void receiver_t::report_topology (const std::string &service_name_,
                                  const std::string &endpoint_,
                                  uint16_t state_,
                                  int32_t error_code_)
{
    if (!_discovery || _routing_id.size == 0 || service_name_.empty ())
        return;

    zlink_registry_topology_entry_t entry;
    build_receiver_topology_entry (&entry, _routing_id, service_name_, endpoint_,
                                   state_, error_code_);
    _discovery->upsert_service_summary (entry);
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
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }
    if (!ensure_routing_id ()) {
        errno = EINVAL;
        return -1;
    }

    _service_name = service_name_;
    _advertise_endpoint = resolve_advertise (advertise_endpoint_);
    if (_advertise_endpoint.empty ()) {
        errno = EINVAL;
        return -1;
    }
    _weight = weight_ == 0 ? 1 : weight_;

    std::string resolved;
    if (_discovery->register_service (
          discovery_protocol::service_type_gateway_receiver,
          _service_name.c_str (), _advertise_endpoint.c_str (), _weight,
          &resolved, &_routing_id)
        != 0) {
        _last_status = -1;
        _last_resolved.clear ();
        _last_error = strerror (errno);
        report_topology (_service_name, _advertise_endpoint,
                         ZLINK_TOPOLOGY_STATE_ERROR, errno);
        return -1;
    }
    _last_status = 0;
    _last_resolved.swap (resolved);
    _last_error.clear ();
    if (!_last_resolved.empty ())
        _advertise_endpoint = _last_resolved;
    report_topology (_service_name, _advertise_endpoint,
                     ZLINK_TOPOLOGY_STATE_READY, 0);

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    ev.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    ev.event_type = ZLINK_RECEIVER_REGISTER_OK;
    ev.status = 0;
    ev.error_code = 0;
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

    return 0;
}

int receiver_t::update_weight (const char *service_name_, uint32_t weight_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }

    const uint32_t value = weight_ == 0 ? 1 : weight_;
    if (_advertise_endpoint.empty ()) {
        errno = EFSM;
        return -1;
    }
    if (_discovery->update_service_weight (
          discovery_protocol::service_type_gateway_receiver, service_name_,
          _advertise_endpoint.c_str (), value)
        != 0)
        return -1;
    _weight = value;
    return 0;
}

int receiver_t::unregister_service (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }

    if (_service_name.empty () || _advertise_endpoint.empty ()
        || _service_name != service_name_) {
        errno = EINVAL;
    }

    if (_service_name.empty () || _advertise_endpoint.empty ()
        || _service_name != service_name_
        || _discovery->unregister_service (
             discovery_protocol::service_type_gateway_receiver, service_name_,
             _advertise_endpoint.c_str ())
             != 0) {
        const int saved_errno = errno;
        zlink_service_event_t ev;
        memset (&ev, 0, sizeof (ev));
        ev.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
        ev.event_type = ZLINK_RECEIVER_UNREGISTER_FAILED;
        ev.status = -1;
        ev.error_code = saved_errno;
        ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                          | ZLINK_EVENT_DETAIL_SUBJECT_RID;
        ev.routing_id = _routing_id;
        strncpy (ev.service_name, service_name_, sizeof (ev.service_name) - 1);
        _monitor.emit (ev);
        errno = saved_errno;
        return -1;
    }

    const std::string service_name = _service_name;
    const std::string endpoint = _advertise_endpoint;
    report_topology (service_name, endpoint, ZLINK_TOPOLOGY_STATE_STOPPED, 0);

    _last_status = 0;
    _last_error.clear ();
    _last_resolved.clear ();
    _service_name.clear ();
    _advertise_endpoint.clear ();

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    ev.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    ev.event_type = ZLINK_RECEIVER_UNREGISTER_OK;
    ev.status = 0;
    ev.error_code = 0;
    ev.detail_flags = ZLINK_EVENT_DETAIL_SERVICE_NAME
                      | ZLINK_EVENT_DETAIL_SUBJECT_RID;
    ev.routing_id = _routing_id;
    strncpy (ev.service_name, service_name_, sizeof (ev.service_name) - 1);
    _monitor.emit (ev);
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
        || _discovery != NULL
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
        case ZLINK_RECEIVER_OPT_SNDBUF:
            return set_socket_option (receiver_socket_router,
                                      ZLINK_SNDBUF, optval_, optvallen_);
        case ZLINK_RECEIVER_OPT_RCVBUF:
            return set_socket_option (receiver_socket_router,
                                      ZLINK_RCVBUF, optval_, optvallen_);
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
    else if (socket_role_ == receiver_socket_dealer) {
        errno = ENOTSUP;
        return -1;
    }
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
    std::string service_name;
    std::string endpoint;
    discovery_t *discovery = NULL;
    bool owns_discovery = false;
    zlink_routing_id_t routing_id;
    routing_id.size = 0;
    {
        scoped_lock_t lock (_sync);
        service_name = _service_name;
        endpoint = _advertise_endpoint;
        routing_id = _routing_id;
        discovery = _discovery;
        owns_discovery = _owns_discovery;
        _discovery = NULL;
        _owns_discovery = false;
        if (_router) {
            _router->close ();
            _router = NULL;
        }
    }

    if (discovery && routing_id.size != 0 && !service_name.empty ()) {
        zlink_registry_topology_entry_t entry;
        build_receiver_topology_entry (&entry, routing_id, service_name, endpoint,
                                       ZLINK_TOPOLOGY_STATE_STOPPED, 0);
        discovery->upsert_service_summary (entry);
    }

    zlink_service_event_t terminal;
    memset (&terminal, 0, sizeof (terminal));
    terminal.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
    terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    terminal.routing_id = routing_id;
    _monitor.close_all (&terminal);

    if (owns_discovery && discovery) {
        discovery->destroy ();
        delete discovery;
    }
    return 0;
}
}
