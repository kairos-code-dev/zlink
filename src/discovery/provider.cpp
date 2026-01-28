/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "discovery/provider.hpp"
#include "discovery/protocol.hpp"

#include "utils/err.hpp"
#include "utils/random.hpp"
#include "sockets/socket_base.hpp"

#if defined ZMQ_HAVE_WINDOWS
#include "utils/windows.hpp"
#else
#include <unistd.h>
#endif

#include <string.h>
#include <vector>

namespace zmq
{
static const uint32_t provider_tag_value = 0x1e6700d8;

static void sleep_ms (int ms_)
{
#if defined ZMQ_HAVE_WINDOWS
    Sleep (ms_);
#else
    usleep (static_cast<useconds_t> (ms_) * 1000);
#endif
}

provider_t::provider_t (ctx_t *ctx_) :
    _ctx (ctx_),
    _tag (provider_tag_value),
    _router (NULL),
    _router_threadsafe (NULL),
    _dealer (NULL),
    _dealer_threadsafe (NULL),
    _weight (1),
    _last_status (-1),
    _heartbeat_interval_ms (5000),
    _stop (0)
{
    zmq_assert (_ctx);
}

provider_t::~provider_t ()
{
    _tag = 0xdeadbeef;
}

bool provider_t::check_tag () const
{
    return _tag == provider_tag_value;
}

static int create_threadsafe_socket (ctx_t *ctx_, int type_,
                                     socket_base_t **socket_,
                                     thread_safe_socket_t **threadsafe_)
{
    *socket_ = ctx_->create_socket (type_);
    if (!*socket_)
        return -1;

    *threadsafe_ = new (std::nothrow) thread_safe_socket_t (ctx_, *socket_);
    if (!*threadsafe_) {
        (*socket_)->close ();
        *socket_ = NULL;
        errno = ENOMEM;
        return -1;
    }

    (*socket_)->set_threadsafe_proxy (*threadsafe_);
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
        if (create_threadsafe_socket (_ctx, ZMQ_ROUTER, &_router,
                                      &_router_threadsafe)
            != 0)
            return -1;
    }

    _bind_endpoint = endpoint_;
    return _router_threadsafe->bind (endpoint_);
}

bool provider_t::ensure_routing_id ()
{
    if (!_router)
        return false;

    zmq_routing_id_t rid;
    size_t size = sizeof (rid.data);
    if (zmq_getsockopt (static_cast<void *> (_router), ZMQ_ROUTING_ID, rid.data,
                        &size)
        == 0) {
        rid.size = static_cast<uint8_t> (size);
        if (rid.size > 0)
            return true;
    }

    uint32_t random_id = zmq::generate_random ();
    if (random_id == 0)
        random_id = 1;
    rid.size = sizeof (random_id);
    memcpy (rid.data, &random_id, sizeof (random_id));
    return zmq_setsockopt (static_cast<void *> (_router), ZMQ_ROUTING_ID,
                           rid.data, rid.size)
           == 0;
}

int provider_t::connect_registry (const char *registry_router_endpoint_)
{
    if (!registry_router_endpoint_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_dealer) {
        if (create_threadsafe_socket (_ctx, ZMQ_DEALER, &_dealer,
                                      &_dealer_threadsafe)
            != 0)
            return -1;
    }

    if (!ensure_routing_id ()) {
        errno = EINVAL;
        return -1;
    }

    zmq_routing_id_t rid;
    size_t size = sizeof (rid.data);
    if (zmq_getsockopt (static_cast<void *> (_router), ZMQ_ROUTING_ID, rid.data,
                        &size)
        == 0) {
        rid.size = static_cast<uint8_t> (size);
        if (rid.size > 0) {
            zmq_setsockopt (static_cast<void *> (_dealer), ZMQ_ROUTING_ID,
                            rid.data, rid.size);
        }
    }

    _registry_endpoint = registry_router_endpoint_;
    return _dealer_threadsafe->connect (registry_router_endpoint_);
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
    if (!_dealer_threadsafe) {
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

    discovery_protocol::send_u16 (static_cast<void *> (_dealer),
                                  discovery_protocol::msg_register,
                                  ZMQ_SNDMORE);
    discovery_protocol::send_string (static_cast<void *> (_dealer),
                                     _service_name, ZMQ_SNDMORE);
    discovery_protocol::send_string (static_cast<void *> (_dealer),
                                     _advertise_endpoint, ZMQ_SNDMORE);
    discovery_protocol::send_u32 (static_cast<void *> (_dealer), _weight, 0);

    zmq_msg_t reply;
    zmq_msg_init (&reply);
    if (zmq_msg_recv (&reply, static_cast<void *> (_dealer), 0) == -1) {
        zmq_msg_close (&reply);
        return -1;
    }

    std::vector<zmq_msg_t> frames;
    frames.push_back (reply);
    while (zmq_msg_more (&frames.back ())) {
        zmq_msg_t frame;
        zmq_msg_init (&frame);
        if (zmq_msg_recv (&frame, static_cast<void *> (_dealer), 0) == -1) {
            zmq_msg_close (&frame);
            break;
        }
        frames.push_back (frame);
    }

    uint16_t msg_id = 0;
    if (frames.size () >= 2
        && discovery_protocol::read_u16 (frames[0], &msg_id)
        && msg_id == discovery_protocol::msg_register_ack) {
        uint8_t status = 0xFF;
        if (zmq_msg_size (&frames[1]) == sizeof (uint8_t)) {
            memcpy (&status, zmq_msg_data (&frames[1]), sizeof (uint8_t));
        }
        _last_status = status;
        if (frames.size () >= 3)
            _last_resolved = discovery_protocol::read_string (frames[2]);
        if (frames.size () >= 4)
            _last_error = discovery_protocol::read_string (frames[3]);
    } else {
        _last_status = -1;
    }

    for (size_t i = 0; i < frames.size (); ++i)
        zmq_msg_close (&frames[i]);

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
    if (!_dealer_threadsafe) {
        errno = ENOTSUP;
        return -1;
    }

    const uint32_t value = weight_ == 0 ? 1 : weight_;
    discovery_protocol::send_u16 (static_cast<void *> (_dealer),
                                  discovery_protocol::msg_update_weight,
                                  ZMQ_SNDMORE);
    discovery_protocol::send_string (static_cast<void *> (_dealer),
                                     service_name_, ZMQ_SNDMORE);
    discovery_protocol::send_string (static_cast<void *> (_dealer),
                                     _advertise_endpoint, ZMQ_SNDMORE);
    discovery_protocol::send_u32 (static_cast<void *> (_dealer), value, 0);
    return 0;
}

int provider_t::unregister_service (const char *service_name_)
{
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (!_dealer_threadsafe) {
        errno = ENOTSUP;
        return -1;
    }

    discovery_protocol::send_u16 (static_cast<void *> (_dealer),
                                  discovery_protocol::msg_unregister,
                                  ZMQ_SNDMORE);
    discovery_protocol::send_string (static_cast<void *> (_dealer),
                                     service_name_, ZMQ_SNDMORE);
    discovery_protocol::send_string (static_cast<void *> (_dealer),
                                     _advertise_endpoint, 0);
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

void *provider_t::threadsafe_router ()
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
    while (_stop.get () == 0) {
        {
            scoped_lock_t lock (_sync);
            if (_dealer_threadsafe && !_service_name.empty ()
                && !_advertise_endpoint.empty ()) {
                discovery_protocol::send_u16 (
                  static_cast<void *> (_dealer),
                  discovery_protocol::msg_heartbeat, ZMQ_SNDMORE);
                discovery_protocol::send_string (static_cast<void *> (_dealer),
                                                 _service_name, ZMQ_SNDMORE);
                discovery_protocol::send_string (static_cast<void *> (_dealer),
                                                 _advertise_endpoint, 0);
            }
        }
        uint32_t remaining = _heartbeat_interval_ms;
        while (remaining > 0 && _stop.get () == 0) {
            const uint32_t chunk = remaining > 100 ? 100 : remaining;
            sleep_ms (static_cast<int> (chunk));
            remaining -= chunk;
        }
    }
}

int provider_t::destroy ()
{
    _stop.set (1);
    if (_heartbeat_thread.get_started ())
        _heartbeat_thread.stop ();

    scoped_lock_t lock (_sync);
    if (_dealer_threadsafe) {
        _dealer_threadsafe->close ();
        if (_dealer)
            _dealer->set_threadsafe_proxy (NULL);
        delete _dealer_threadsafe;
        _dealer_threadsafe = NULL;
        _dealer = NULL;
    } else if (_dealer) {
        _dealer->close ();
        _dealer = NULL;
    }
    if (_router_threadsafe) {
        _router_threadsafe->close ();
        if (_router)
            _router->set_threadsafe_proxy (NULL);
        delete _router_threadsafe;
        _router_threadsafe = NULL;
        _router = NULL;
    } else if (_router) {
        _router->close ();
        _router = NULL;
    }
    return 0;
}
}
