/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_HPP_INCLUDED

#include "context.hpp"
#include "message.hpp"
#include "msgv.hpp"
#include "types.hpp"

#include <cerrno>

namespace zlink
{

class registry_t
{
  public:
    explicit registry_t (context_t &ctx_)
        : _reg (zlink_registry_new (ctx_.handle ()))
    {
    }

    ~registry_t () { destroy (); }

    registry_t (registry_t &&other) noexcept : _reg (other._reg)
    {
        other._reg = NULL;
    }

    registry_t &operator= (registry_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy ();
        _reg = other._reg;
        other._reg = NULL;
        return *this;
    }

    registry_t (const registry_t &) = delete;
    registry_t &operator= (const registry_t &) = delete;

    int set_endpoints (const char *pub_, const char *router_)
    {
        return zlink_registry_set_endpoints (_reg, pub_, router_);
    }

    int set_id (uint32_t id_) { return zlink_registry_set_id (_reg, id_); }

    int add_peer (const char *pub_) { return zlink_registry_add_peer (_reg, pub_); }

    int set_heartbeat (uint32_t ivl_ms_, uint32_t timeout_ms_)
    {
        return zlink_registry_set_heartbeat (_reg, ivl_ms_, timeout_ms_);
    }

    int set_broadcast_interval (uint32_t ivl_ms_)
    {
        return zlink_registry_set_broadcast_interval (_reg, ivl_ms_);
    }

    int set_sockopt (registry_socket_role role_,
                     socket_option option_,
                     const void *value_,
                     size_t len_)
    {
        return zlink_registry_setsockopt (
          _reg, static_cast<int> (role_), static_cast<int> (option_), value_, len_);
    }

    int start () { return zlink_registry_start (_reg); }

    int destroy ()
    {
        if (!_reg)
            return 0;

        void *tmp = _reg;
        _reg = NULL;
        return zlink_registry_destroy (&tmp);
    }

    void *handle () const { return _reg; }

  private:
    void *_reg;
};

class discovery_t
{
  public:
    discovery_t (context_t &ctx_, service_type service_type_)
        : _disc (zlink_discovery_new_typed (
            ctx_.handle (), static_cast<uint16_t> (service_type_)))
    {
    }

    ~discovery_t () { destroy (); }

    discovery_t (discovery_t &&other) noexcept : _disc (other._disc)
    {
        other._disc = NULL;
    }

    discovery_t &operator= (discovery_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy ();
        _disc = other._disc;
        other._disc = NULL;
        return *this;
    }

    discovery_t (const discovery_t &) = delete;
    discovery_t &operator= (const discovery_t &) = delete;

    int connect_registry (const char *pub_)
    {
        return zlink_discovery_connect_registry (_disc, pub_);
    }

    int subscribe (const char *service_)
    {
        return zlink_discovery_subscribe (_disc, service_);
    }

    int unsubscribe (const char *service_)
    {
        return zlink_discovery_unsubscribe (_disc, service_);
    }

    int receiver_count (const char *service_)
    {
        return zlink_discovery_receiver_count (_disc, service_);
    }

    int service_available (const char *service_)
    {
        return zlink_discovery_service_available (_disc, service_);
    }

    int set_sockopt (discovery_socket_role role_,
                     socket_option option_,
                     const void *value_,
                     size_t len_)
    {
        return zlink_discovery_setsockopt (
          _disc, static_cast<int> (role_), static_cast<int> (option_), value_, len_);
    }

    int get_receivers (const char *service_,
                       zlink_receiver_info_t *providers_,
                       size_t *count_)
    {
        return zlink_discovery_get_receivers (_disc, service_, providers_, count_);
    }

    int destroy ()
    {
        if (!_disc)
            return 0;

        void *tmp = _disc;
        _disc = NULL;
        return zlink_discovery_destroy (&tmp);
    }

    void *handle () const { return _disc; }

  private:
    void *_disc;
};

class gateway_t
{
  public:
    gateway_t (context_t &ctx_, discovery_t &disc_)
        : _gw (zlink_gateway_new (ctx_.handle (), disc_.handle (), NULL))
    {
    }

    gateway_t (context_t &ctx_, discovery_t &disc_, const char *routing_id_)
        : _gw (zlink_gateway_new (ctx_.handle (), disc_.handle (), routing_id_))
    {
    }

    ~gateway_t () { destroy (); }

    gateway_t (gateway_t &&other) noexcept : _gw (other._gw)
    {
        other._gw = NULL;
    }

    gateway_t &operator= (gateway_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy ();
        _gw = other._gw;
        other._gw = NULL;
        return *this;
    }

    gateway_t (const gateway_t &) = delete;
    gateway_t &operator= (const gateway_t &) = delete;

    int send (const char *service_, std::vector<message_t> &parts_)
    {
        if (parts_.empty ())
            return -1;

        std::vector<zlink_msg_t> tmp;
        tmp.resize (parts_.size ());
        for (size_t i = 0; i < parts_.size (); ++i) {
            if (parts_[i].move_to (&tmp[i]) != 0)
                return -1;
        }

        return zlink_gateway_send (_gw, service_, tmp.data (), tmp.size (), 0);
    }

    int recv (msgv_t &out_, std::string &service_, recv_flag flags_ = recv_flag::none)
    {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char name[256];
        const int rc = zlink_gateway_recv (
          _gw, &parts, &count, static_cast<int> (flags_), name);
        if (rc != 0)
            return rc;

        out_.adopt (parts, count);
        service_.assign (name);
        return 0;
    }

    int send_bytes (const char *service_,
                    const void *data_,
                    size_t size_,
                    send_flag flags_ = send_flag::none)
    {
        return zlink_gateway_send_bytes (
          _gw, service_, data_, size_, static_cast<int> (flags_));
    }

    int send_rid (const char *service_,
                  const zlink_routing_id_t &routing_id_,
                  std::vector<message_t> &parts_,
                  send_flag flags_ = send_flag::none)
    {
        if (parts_.empty ())
            return -1;

        std::vector<zlink_msg_t> tmp;
        tmp.resize (parts_.size ());
        for (size_t i = 0; i < parts_.size (); ++i) {
            if (parts_[i].move_to (&tmp[i]) != 0)
                return -1;
        }

        return zlink_gateway_send_rid (
          _gw, service_, &routing_id_, tmp.data (), tmp.size (),
          static_cast<int> (flags_));
    }

    int send_rid_bytes (const char *service_,
                        const zlink_routing_id_t &routing_id_,
                        const void *data_,
                        size_t size_,
                        send_flag flags_ = send_flag::none)
    {
        return zlink_gateway_send_rid_bytes (
          _gw, service_, &routing_id_, data_, size_, static_cast<int> (flags_));
    }

    int set_sockopt (socket_option option_, const void *value_, size_t len_)
    {
        return zlink_gateway_setsockopt (_gw, static_cast<int> (option_), value_,
                                         len_);
    }

    int set_lb_strategy (const char *service_, gateway_lb_strategy strategy_)
    {
        return zlink_gateway_set_lb_strategy (
          _gw, service_, static_cast<int> (strategy_));
    }

    int set_tls_client (const char *ca_, const char *hostname_, int trust_)
    {
        return zlink_gateway_set_tls_client (_gw, ca_, hostname_, trust_);
    }

    int connection_count (const char *service_)
    {
        return zlink_gateway_connection_count (_gw, service_);
    }

    void *router_handle () const
    {
        return zlink_gateway_router_socket_unsafe (_gw);
    }

    int router_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_gateway_router_peers (_gw, peers_, count_);
    }

    int destroy ()
    {
        if (!_gw)
            return 0;

        void *tmp = _gw;
        _gw = NULL;
        return zlink_gateway_destroy (&tmp);
    }

    void *handle () const { return _gw; }

  private:
    void *_gw;
};

class receiver_t
{
  public:
    explicit receiver_t (context_t &ctx_)
        : _receiver (zlink_receiver_new (ctx_.handle (), NULL))
    {
    }

    receiver_t (context_t &ctx_, const char *routing_id_)
        : _receiver (zlink_receiver_new (ctx_.handle (), routing_id_))
    {
    }

    ~receiver_t () { destroy (); }

    receiver_t (receiver_t &&other) noexcept : _receiver (other._receiver)
    {
        other._receiver = NULL;
    }

    receiver_t &operator= (receiver_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy ();
        _receiver = other._receiver;
        other._receiver = NULL;
        return *this;
    }

    receiver_t (const receiver_t &) = delete;
    receiver_t &operator= (const receiver_t &) = delete;

    int bind (const char *endpoint_)
    {
        return zlink_receiver_bind (_receiver, endpoint_);
    }

    int connect_registry (const char *endpoint_)
    {
        return zlink_receiver_connect_registry (_receiver, endpoint_);
    }

    int set_sockopt (receiver_socket_role role_,
                     socket_option option_,
                     const void *value_,
                     size_t len_)
    {
        return zlink_receiver_setsockopt (
          _receiver, static_cast<int> (role_), static_cast<int> (option_), value_,
          len_);
    }

    int register_service (const char *service_,
                          const char *advertise_,
                          uint32_t weight_)
    {
        return zlink_receiver_register (_receiver, service_, advertise_, weight_);
    }

    int update_weight (const char *service_, uint32_t weight_)
    {
        return zlink_receiver_update_weight (_receiver, service_, weight_);
    }

    int unregister_service (const char *service_)
    {
        return zlink_receiver_unregister (_receiver, service_);
    }

    int register_result (const char *service_,
                         int *status_,
                         char *resolved_,
                         char *err_)
    {
        return zlink_receiver_register_result (
          _receiver, service_, status_, resolved_, err_);
    }

    int set_tls_server (const char *cert_, const char *key_)
    {
        return zlink_receiver_set_tls_server (_receiver, cert_, key_);
    }

    void *router_handle () const
    {
        return zlink_receiver_router_socket_unsafe (_receiver);
    }

    int router_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_receiver_router_peers (_receiver, peers_, count_);
    }

    int destroy ()
    {
        if (!_receiver)
            return 0;

        void *tmp = _receiver;
        _receiver = NULL;
        return zlink_receiver_destroy (&tmp);
    }

    void *handle () const { return _receiver; }

  private:
    void *_receiver;
};

class spot_node_t
{
  public:
    explicit spot_node_t (context_t &ctx_)
        : _node (zlink_spot_node_new (ctx_.handle ()))
    {
    }

    ~spot_node_t () { destroy (); }

    spot_node_t (spot_node_t &&other) noexcept : _node (other._node)
    {
        other._node = NULL;
    }

    spot_node_t &operator= (spot_node_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy ();
        _node = other._node;
        other._node = NULL;
        return *this;
    }

    spot_node_t (const spot_node_t &) = delete;
    spot_node_t &operator= (const spot_node_t &) = delete;

    int bind (const char *endpoint_)
    {
        return zlink_spot_node_bind (_node, endpoint_);
    }

    int connect_registry (const char *endpoint_)
    {
        return zlink_spot_node_connect_registry (_node, endpoint_);
    }

    int connect_peer_pub (const char *endpoint_)
    {
        return zlink_spot_node_connect_peer_pub (_node, endpoint_);
    }

    int disconnect_peer_pub (const char *endpoint_)
    {
        return zlink_spot_node_disconnect_peer_pub (_node, endpoint_);
    }

    int register_service (const char *service_, const char *advertise_)
    {
        return zlink_spot_node_register (_node, service_, advertise_);
    }

    int unregister_service (const char *service_)
    {
        return zlink_spot_node_unregister (_node, service_);
    }

    int set_discovery (void *discovery_, const char *service_)
    {
        return zlink_spot_node_set_discovery (_node, discovery_, service_);
    }

    int set_tls_server (const char *cert_, const char *key_)
    {
        return zlink_spot_node_set_tls_server (_node, cert_, key_);
    }

    int set_tls_client (const char *ca_, const char *hostname_, int trust_)
    {
        return zlink_spot_node_set_tls_client (_node, ca_, hostname_, trust_);
    }

    int set_sockopt (spot_node_socket_role role_,
                     socket_option option_,
                     const void *value_,
                     size_t len_)
    {
        return zlink_spot_node_setsockopt (
          _node, static_cast<int> (role_), static_cast<int> (option_), value_,
          len_);
    }

    int set_sockopt (spot_node_socket_role role_,
                     socket_option option_,
                     int value_)
    {
        return set_sockopt (role_, option_, &value_, sizeof (value_));
    }

    int set_sockopt (spot_node_option option_, int value_)
    {
        return zlink_spot_node_setsockopt (
          _node, static_cast<int> (spot_node_socket_role::node),
          static_cast<int> (option_), &value_, sizeof (value_));
    }

    void *pub_socket_handle () const
    {
        return zlink_spot_node_pub_socket_unsafe (_node);
    }

    void *sub_socket_handle () const
    {
        return zlink_spot_node_sub_socket_unsafe (_node);
    }

    int pub_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_spot_node_pub_peers (_node, peers_, count_);
    }

    int sub_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_spot_node_sub_peers (_node, peers_, count_);
    }

    int destroy ()
    {
        if (!_node)
            return 0;

        void *tmp = _node;
        _node = NULL;
        return zlink_spot_node_destroy (&tmp);
    }

    void *handle () const { return _node; }

  private:
    void *_node;
};

class spot_t
{
  public:
    typedef std::function<void (const std::string &, const zlink_msg_t *, size_t)>
      handler_fn;

    explicit spot_t (spot_node_t &node_)
        : _node (node_.handle ()),
          _pub (zlink_spot_pub_new (node_.handle ())),
          _sub (zlink_spot_sub_new (node_.handle ()))
    {
        if (_pub && _sub)
            return;

        if (_pub)
            zlink_spot_pub_destroy (&_pub);
        if (_sub)
            zlink_spot_sub_destroy (&_sub);
        _pub = NULL;
        _sub = NULL;
    }

    ~spot_t () { destroy (); }

    spot_t (spot_t &&other) noexcept : _node (NULL), _pub (NULL), _sub (NULL)
    {
        other.set_handler (handler_fn ());
        _node = other._node;
        _pub = other._pub;
        _sub = other._sub;
        other._node = NULL;
        other._pub = NULL;
        other._sub = NULL;
    }

    spot_t &operator= (spot_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        set_handler (handler_fn ());
        destroy ();
        other.set_handler (handler_fn ());
        _node = other._node;
        _pub = other._pub;
        _sub = other._sub;
        other._node = NULL;
        other._pub = NULL;
        other._sub = NULL;
        return *this;
    }

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    int publish (const char *topic_, std::vector<message_t> &parts_)
    {
        if (parts_.empty ())
            return -1;

        std::vector<zlink_msg_t> tmp;
        tmp.resize (parts_.size ());
        for (size_t i = 0; i < parts_.size (); ++i) {
            if (parts_[i].move_to (&tmp[i]) != 0)
                return -1;
        }

        return zlink_spot_pub_publish (_pub, topic_, tmp.data (), tmp.size (), 0);
    }

    int publish_bytes (const char *topic_,
                       const void *data_,
                       size_t size_,
                       send_flag flags_ = send_flag::none)
    {
        return zlink_spot_pub_publish_bytes (
          _pub, topic_, data_, size_, static_cast<int> (flags_));
    }

    int subscribe (const char *topic_)
    {
        return zlink_spot_sub_subscribe (_sub, topic_);
    }

    int subscribe_pattern (const char *pattern_)
    {
        return zlink_spot_sub_subscribe_pattern (_sub, pattern_);
    }

    int unsubscribe (const char *topic_or_pattern_)
    {
        return zlink_spot_sub_unsubscribe (_sub, topic_or_pattern_);
    }

    int set_handler (handler_fn fn_)
    {
        const bool enable = static_cast<bool> (fn_);
        {
            std::lock_guard<std::mutex> lock (_handler_sync);
            _handler_fn = std::move (fn_);
        }

        if (!_sub) {
            if (enable) {
                errno = EFAULT;
                return -1;
            }
            return 0;
        }

        return zlink_spot_sub_set_handler (
          _sub, enable ? &spot_t::handler_trampoline : NULL, this);
    }

    int recv (msgv_t &out_, std::string &topic_, recv_flag flags_ = recv_flag::none)
    {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char topic_buf[256];
        size_t topic_len = sizeof (topic_buf);
        const int rc = zlink_spot_sub_recv (
          _sub,
          &parts,
          &count,
          static_cast<int> (flags_),
          topic_buf,
          &topic_len);
        if (rc != 0)
            return rc;

        out_.adopt (parts, count);
        topic_.assign (topic_buf);
        return 0;
    }

    int destroy ()
    {
        int rc = 0;
        if (_sub)
            set_handler (handler_fn ());

        if (_pub) {
            void *tmp = _pub;
            _pub = NULL;
            if (zlink_spot_pub_destroy (&tmp) != 0)
                rc = -1;
        }

        if (_sub) {
            void *tmp = _sub;
            _sub = NULL;
            if (zlink_spot_sub_destroy (&tmp) != 0)
                rc = -1;
        }

        _node = NULL;
        return rc;
    }

    void *handle () const { return _pub; }

  private:
    static void handler_trampoline (const char *topic_,
                                    size_t topic_len_,
                                    const zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_) noexcept
    {
        spot_t *self = static_cast<spot_t *> (userdata_);
        if (!self)
            return;

        handler_fn fn;
        {
            std::lock_guard<std::mutex> lock (self->_handler_sync);
            fn = self->_handler_fn;
        }
        if (!fn)
            return;

        try {
            fn (std::string (topic_, topic_len_), parts_, part_count_);
        }
        catch (...) {
        }
    }

    void *_node;
    void *_pub;
    void *_sub;
    handler_fn _handler_fn;
    std::mutex _handler_sync;
};

} // namespace zlink

#endif
