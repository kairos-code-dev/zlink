/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_HPP_INCLUDED
#define ZLINK_CPP_HPP_INCLUDED

#include <zlink.h>

#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <utility>

#if defined(ZLINK_CPP_EXCEPTIONS)
#include <stdexcept>
#include <exception>
#endif

namespace zlink
{

#if __cplusplus >= 201703L
#define ZLINK_CPP_NODISCARD [[nodiscard]]
#else
#define ZLINK_CPP_NODISCARD
#endif

class error_t
#if defined(ZLINK_CPP_EXCEPTIONS)
  : public std::exception
#endif
{
  public:
    explicit error_t (int code_) : _code (code_) {}
    int code () const noexcept { return _code; }
    const char *what () const noexcept
#if defined(ZLINK_CPP_EXCEPTIONS)
      override
#endif
    {
        return zlink_strerror (_code);
    }

  private:
    int _code;
};

inline error_t last_error () { return error_t (zlink_errno ()); }

#if defined(ZLINK_CPP_EXCEPTIONS)
inline void throw_on_error (int rc)
{
    if (rc < 0)
        throw std::runtime_error (zlink_strerror (zlink_errno ()));
}
#endif

class message_t
{
  public:
    message_t () : _valid (false)
    {
        if (zlink_msg_init (&_msg) == 0)
            _valid = true;
    }

    explicit message_t (size_t size_) : _valid (false)
    {
        if (zlink_msg_init_size (&_msg, size_) == 0)
            _valid = true;
    }

    ~message_t () { close (); }

    message_t (message_t &&other) noexcept : _valid (false)
    {
        if (other._valid) {
            if (zlink_msg_init (&_msg) == 0) {
                if (zlink_msg_move (&_msg, &other._msg) == 0) {
                    _valid = true;
                    other._valid = false;
                } else {
                    zlink_msg_close (&_msg);
                }
            }
        }
    }

    message_t &operator= (message_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        close ();
        if (other._valid) {
            if (zlink_msg_init (&_msg) == 0) {
                if (zlink_msg_move (&_msg, &other._msg) == 0) {
                    _valid = true;
                    other._valid = false;
                } else {
                    zlink_msg_close (&_msg);
                }
            }
        }
        return *this;
    }

    message_t (const message_t &) = delete;
    message_t &operator= (const message_t &) = delete;

    bool valid () const noexcept { return _valid; }

    int init ()
    {
        if (_valid)
            return 0;
        if (zlink_msg_init (&_msg) != 0)
            return -1;
        _valid = true;
        return 0;
    }

    void *data () noexcept { return _valid ? zlink_msg_data (&_msg) : NULL; }
    const void *data () const noexcept
    {
        return _valid ? zlink_msg_data (const_cast<zlink_msg_t *> (&_msg)) : NULL;
    }
    size_t size () const noexcept
    {
        return _valid ? zlink_msg_size (&_msg) : 0;
    }
    bool more () const noexcept { return _valid && zlink_msg_more (&_msg) != 0; }

    int close () noexcept
    {
        if (!_valid)
            return 0;
        const int rc = zlink_msg_close (&_msg);
        _valid = false;
        return rc;
    }

    zlink_msg_t *handle () noexcept { return &_msg; }
    const zlink_msg_t *handle () const noexcept { return &_msg; }

    int move_to (zlink_msg_t *dest_)
    {
        if (!dest_ || !_valid)
            return -1;
        if (zlink_msg_init (dest_) != 0)
            return -1;
        const int rc = zlink_msg_move (dest_, &_msg);
        if (rc == 0)
            _valid = false;
        return rc;
    }

    int copy_to (zlink_msg_t *dest_) const
    {
        if (!dest_ || !_valid)
            return -1;
        if (zlink_msg_init (dest_) != 0)
            return -1;
        return zlink_msg_copy (dest_, const_cast<zlink_msg_t *> (&_msg));
    }

  private:
    zlink_msg_t _msg;
    bool _valid;
};

class context_t
{
  public:
    context_t () : _ctx (zlink_ctx_new ()) {}

    explicit context_t (int io_threads) : _ctx (zlink_ctx_new ())
    {
        if (_ctx)
            zlink_ctx_set (_ctx, ZLINK_IO_THREADS, io_threads);
    }

    ~context_t ()
    {
        if (_ctx)
            zlink_ctx_term (_ctx);
        _ctx = NULL;
    }

    context_t (context_t &&other) noexcept : _ctx (other._ctx)
    {
        other._ctx = NULL;
    }

    context_t &operator= (context_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        if (_ctx)
            zlink_ctx_term (_ctx);
        _ctx = other._ctx;
        other._ctx = NULL;
        return *this;
    }

    context_t (const context_t &) = delete;
    context_t &operator= (const context_t &) = delete;

    void *handle () noexcept { return _ctx; }

    int set (int option_, int value_)
    {
        return _ctx ? zlink_ctx_set (_ctx, option_, value_) : -1;
    }

    int get (int option_, int *value_) const
    {
        if (!_ctx || !value_)
            return -1;
        *value_ = zlink_ctx_get (_ctx, option_);
        return 0;
    }

  private:
    void *_ctx;
};

class socket_t
{
  public:
    socket_t () : _socket (NULL), _own (false) {}

    socket_t (context_t &ctx_, int type_)
        : _socket (zlink_socket (ctx_.handle (), type_)), _own (true)
    {
    }

    ~socket_t () { close (); }

    socket_t (socket_t &&other) noexcept : _socket (other._socket), _own (other._own)
    {
        other._socket = NULL;
        other._own = false;
    }

    socket_t &operator= (socket_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        close ();
        _socket = other._socket;
        _own = other._own;
        other._socket = NULL;
        other._own = false;
        return *this;
    }

    socket_t (const socket_t &) = delete;
    socket_t &operator= (const socket_t &) = delete;

    static socket_t adopt (void *socket_)
    {
        socket_t s;
        s._socket = socket_;
        s._own = true;
        return s;
    }

    static socket_t wrap (void *socket_)
    {
        socket_t s;
        s._socket = socket_;
        s._own = false;
        return s;
    }

    void *handle () noexcept { return _socket; }

    int bind (const char *endpoint_) { return zlink_bind (_socket, endpoint_); }
    int bind (const std::string &endpoint_)
    {
        return zlink_bind (_socket, endpoint_.c_str ());
    }

    int connect (const char *endpoint_)
    {
        return zlink_connect (_socket, endpoint_);
    }
    int connect (const std::string &endpoint_)
    {
        return zlink_connect (_socket, endpoint_.c_str ());
    }

    int unbind (const char *endpoint_)
    {
        return zlink_unbind (_socket, endpoint_);
    }
    int disconnect (const char *endpoint_)
    {
        return zlink_disconnect (_socket, endpoint_);
    }

    int close () noexcept
    {
        int rc = 0;
        if (_socket && _own)
            rc = zlink_close (_socket);
        _socket = NULL;
        _own = false;
        return rc;
    }

    int send (const void *buf_, size_t len_, int flags_ = 0)
    {
        return zlink_send (_socket, buf_, len_, flags_);
    }

    int send (const std::string &s_, int flags_ = 0)
    {
        return zlink_send (_socket, s_.data (), s_.size (), flags_);
    }

    int recv (void *buf_, size_t len_, int flags_ = 0)
    {
        return zlink_recv (_socket, buf_, len_, flags_);
    }

    int send (message_t &msg_, int flags_ = 0)
    {
        const int rc = zlink_msg_send (msg_.handle (), _socket, flags_);
        if (rc >= 0)
            msg_.close ();
        return rc;
    }

    int send_const (const void *buf_, size_t len_, int flags_ = 0)
    {
        return zlink_send_const (_socket, buf_, len_, flags_);
    }

    int recv (message_t &msg_, int flags_ = 0)
    {
        if (!msg_.valid () && msg_.init () != 0)
            return -1;
        return zlink_msg_recv (msg_.handle (), _socket, flags_);
    }

    int set (int option_, const void *optval_, size_t optlen_)
    {
        return zlink_setsockopt (_socket, option_, optval_, optlen_);
    }

    int get (int option_, void *optval_, size_t *optlen_) const
    {
        return zlink_getsockopt (_socket, option_, optval_, optlen_);
    }

    int set (int option_, int value_)
    {
        return zlink_setsockopt (_socket, option_, &value_, sizeof (value_));
    }

    int get (int option_, int *value_) const
    {
        if (!value_)
            return -1;
        size_t len = sizeof (*value_);
        return zlink_getsockopt (_socket, option_, value_, &len);
    }

    int set (int option_, const std::string &value_)
    {
        return zlink_setsockopt (_socket, option_, value_.data (), value_.size ());
    }

    int get (int option_, std::string &value_) const
    {
        size_t len = 256;
        std::vector<char> buf (len);
        if (zlink_getsockopt (_socket, option_, buf.data (), &len) != 0)
            return -1;
        if (len > buf.size ()) {
            buf.resize (len);
            if (zlink_getsockopt (_socket, option_, buf.data (), &len) != 0)
                return -1;
        }
        value_.assign (buf.data (), len);
        return 0;
    }

    int monitor (const char *addr_, int events_)
    {
        return zlink_socket_monitor (_socket, addr_, events_);
    }

    socket_t monitor_open (int events_)
    {
        void *m = zlink_socket_monitor_open (_socket, events_);
        return socket_t::adopt (m);
    }

  private:
    void *_socket;
    bool _own;
};

struct poll_event_t
{
    socket_t *socket;
    void *user;
    short events;
    short revents;
};

class poller_t
{
  public:
    int add (socket_t &socket_, short events_, void *user_ = NULL)
    {
        item_t item;
        item.socket = &socket_;
        item.fd = 0;
        item.events = events_;
        item.user = user_;
        _items.push_back (item);
        return 0;
    }

    int add (zlink_fd_t fd_, short events_, void *user_ = NULL)
    {
        item_t item;
        item.socket = NULL;
        item.fd = fd_;
        item.events = events_;
        item.user = user_;
        _items.push_back (item);
        return 0;
    }

    int remove (socket_t &socket_)
    {
        for (std::vector<item_t>::iterator it = _items.begin ();
             it != _items.end (); ++it) {
            if (it->socket == &socket_) {
                _items.erase (it);
                return 0;
            }
        }
        return -1;
    }

    int remove (zlink_fd_t fd_)
    {
        for (std::vector<item_t>::iterator it = _items.begin ();
             it != _items.end (); ++it) {
            if (it->socket == NULL && it->fd == fd_) {
                _items.erase (it);
                return 0;
            }
        }
        return -1;
    }

    int wait (std::vector<poll_event_t> &events_, long timeout_ms_)
    {
        std::vector<zlink_pollitem_t> pollitems;
        pollitems.resize (_items.size ());
        for (size_t i = 0; i < _items.size (); ++i) {
            pollitems[i].socket = _items[i].socket
                                   ? _items[i].socket->handle ()
                                   : NULL;
            pollitems[i].fd = _items[i].fd;
            pollitems[i].events = _items[i].events;
            pollitems[i].revents = 0;
        }
        const int rc =
          zlink_poll (pollitems.data (), static_cast<int> (pollitems.size ()),
                      timeout_ms_);
        if (rc <= 0)
            return rc;
        events_.clear ();
        for (size_t i = 0; i < pollitems.size (); ++i) {
            if (pollitems[i].revents == 0)
                continue;
            poll_event_t ev;
            ev.socket = _items[i].socket;
            ev.user = _items[i].user;
            ev.events = pollitems[i].events;
            ev.revents = pollitems[i].revents;
            events_.push_back (ev);
        }
        return static_cast<int> (events_.size ());
    }

    int wait (std::vector<poll_event_t> &events_,
              std::chrono::milliseconds timeout_)
    {
        return wait (events_, static_cast<long> (timeout_.count ()));
    }

  private:
    struct item_t
    {
        socket_t *socket;
        zlink_fd_t fd;
        short events;
        void *user;
    };

    std::vector<item_t> _items;
};

class msgv_t
{
  public:
    msgv_t () : _parts (NULL), _count (0) {}
    ~msgv_t () { reset (); }

    msgv_t (msgv_t &&other) noexcept : _parts (other._parts), _count (other._count)
    {
        other._parts = NULL;
        other._count = 0;
    }

    msgv_t &operator= (msgv_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        reset ();
        _parts = other._parts;
        _count = other._count;
        other._parts = NULL;
        other._count = 0;
        return *this;
    }

    msgv_t (const msgv_t &) = delete;
    msgv_t &operator= (const msgv_t &) = delete;

    void reset ()
    {
        if (_parts) {
            zlink_msgv_close (_parts, _count);
            _parts = NULL;
            _count = 0;
        }
    }

    zlink_msg_t *data () { return _parts; }
    const zlink_msg_t *data () const { return _parts; }
    size_t size () const { return _count; }

    zlink_msg_t &operator[] (size_t idx_) { return _parts[idx_]; }
    const zlink_msg_t &operator[] (size_t idx_) const { return _parts[idx_]; }

    void adopt (zlink_msg_t *parts_, size_t count_)
    {
        reset ();
        _parts = parts_;
        _count = count_;
    }

  private:
    zlink_msg_t *_parts;
    size_t _count;
};

class monitor_socket_t
{
  public:
    explicit monitor_socket_t (socket_t &&sock_) : _sock (std::move (sock_)) {}

    int recv (zlink_monitor_event_t &event_, int flags_ = 0)
    {
        return zlink_monitor_recv (_sock.handle (), &event_, flags_);
    }

  private:
    socket_t _sock;
};

class registry_t
{
  public:
    explicit registry_t (context_t &ctx_) : _reg (zlink_registry_new (ctx_.handle ())) {}
    ~registry_t () { destroy (); }

    registry_t (registry_t &&other) noexcept : _reg (other._reg) { other._reg = NULL; }
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
    int set_sockopt (int role_, int option_, const void *value_, size_t len_)
    {
        return zlink_registry_setsockopt (_reg, role_, option_, value_, len_);
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
    discovery_t (context_t &ctx_, uint16_t service_type_)
        : _disc (zlink_discovery_new_typed (ctx_.handle (), service_type_))
    {
    }
    ~discovery_t () { destroy (); }

    discovery_t (discovery_t &&other) noexcept : _disc (other._disc) { other._disc = NULL; }
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

    int connect_registry (const char *pub_) { return zlink_discovery_connect_registry (_disc, pub_); }
    int subscribe (const char *service_) { return zlink_discovery_subscribe (_disc, service_); }
    int unsubscribe (const char *service_) { return zlink_discovery_unsubscribe (_disc, service_); }
    int receiver_count (const char *service_) { return zlink_discovery_receiver_count (_disc, service_); }
    int service_available (const char *service_) { return zlink_discovery_service_available (_disc, service_); }
    int set_sockopt (int role_, int option_, const void *value_, size_t len_)
    {
        return zlink_discovery_setsockopt (_disc, role_, option_, value_, len_);
    }

    int get_receivers (const char *service_, zlink_receiver_info_t *providers_, size_t *count_)
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

    gateway_t (gateway_t &&other) noexcept : _gw (other._gw) { other._gw = NULL; }
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

    int recv (msgv_t &out_, std::string &service_, int flags_ = 0)
    {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char name[256];
        const int rc = zlink_gateway_recv (_gw, &parts, &count, flags_, name);
        if (rc != 0)
            return rc;
        out_.adopt (parts, count);
        service_.assign (name);
        return 0;
    }
    int set_sockopt (int option_, const void *value_, size_t len_)
    {
        return zlink_gateway_setsockopt (_gw, option_, value_, len_);
    }

    int set_lb_strategy (const char *service_, int strategy_)
    {
        return zlink_gateway_set_lb_strategy (_gw, service_, strategy_);
    }

    int set_tls_client (const char *ca_, const char *hostname_, int trust_)
    {
        return zlink_gateway_set_tls_client (_gw, ca_, hostname_, trust_);
    }

    int connection_count (const char *service_)
    {
        return zlink_gateway_connection_count (_gw, service_);
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
    explicit receiver_t (context_t &ctx_) : _prov (zlink_receiver_new (ctx_.handle (), NULL)) {}
    receiver_t (context_t &ctx_, const char *routing_id_)
        : _prov (zlink_receiver_new (ctx_.handle (), routing_id_))
    {
    }
    ~receiver_t () { destroy (); }

    receiver_t (receiver_t &&other) noexcept : _prov (other._prov) { other._prov = NULL; }
    receiver_t &operator= (receiver_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy ();
        _prov = other._prov;
        other._prov = NULL;
        return *this;
    }

    receiver_t (const receiver_t &) = delete;
    receiver_t &operator= (const receiver_t &) = delete;

    int bind (const char *endpoint_) { return zlink_receiver_bind (_prov, endpoint_); }
    int connect_registry (const char *endpoint_)
    {
        return zlink_receiver_connect_registry (_prov, endpoint_);
    }
    int set_sockopt (int role_, int option_, const void *value_, size_t len_)
    {
        return zlink_receiver_setsockopt (_prov, role_, option_, value_, len_);
    }
    int register_service (const char *service_, const char *advertise_, uint32_t weight_)
    {
        return zlink_receiver_register (_prov, service_, advertise_, weight_);
    }
    int update_weight (const char *service_, uint32_t weight_)
    {
        return zlink_receiver_update_weight (_prov, service_, weight_);
    }
    int unregister_service (const char *service_)
    {
        return zlink_receiver_unregister (_prov, service_);
    }
    int register_result (const char *service_, int *status_, char *resolved_, char *err_)
    {
        return zlink_receiver_register_result (_prov, service_, status_, resolved_, err_);
    }
    int set_tls_server (const char *cert_, const char *key_)
    {
        return zlink_receiver_set_tls_server (_prov, cert_, key_);
    }

    void *router_handle () const { return zlink_receiver_router (_prov); }

    int destroy ()
    {
        if (!_prov)
            return 0;
        void *tmp = _prov;
        _prov = NULL;
        return zlink_receiver_destroy (&tmp);
    }

    void *handle () const { return _prov; }

  private:
    void *_prov;
};

class spot_node_t
{
  public:
    explicit spot_node_t (context_t &ctx_) : _node (zlink_spot_node_new (ctx_.handle ())) {}
    ~spot_node_t () { destroy (); }

    spot_node_t (spot_node_t &&other) noexcept : _node (other._node) { other._node = NULL; }
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

    int bind (const char *endpoint_) { return zlink_spot_node_bind (_node, endpoint_); }
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

    void *pub_handle () const { return zlink_spot_node_pub_socket (_node); }
    void *sub_handle () const { return zlink_spot_node_sub_socket (_node); }

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
    explicit spot_t (spot_node_t &node_) : _spot (zlink_spot_new (node_.handle ())) {}
    ~spot_t () { destroy (); }

    spot_t (spot_t &&other) noexcept : _spot (other._spot) { other._spot = NULL; }
    spot_t &operator= (spot_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy ();
        _spot = other._spot;
        other._spot = NULL;
        return *this;
    }

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    int topic_create (const char *topic_, int mode_)
    {
        return zlink_spot_topic_create (_spot, topic_, mode_);
    }
    int topic_destroy (const char *topic_) { return zlink_spot_topic_destroy (_spot, topic_); }

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
        return zlink_spot_publish (_spot, topic_, tmp.data (), tmp.size (), 0);
    }

    int subscribe (const char *topic_) { return zlink_spot_subscribe (_spot, topic_); }
    int subscribe_pattern (const char *pattern_)
    {
        return zlink_spot_subscribe_pattern (_spot, pattern_);
    }
    int unsubscribe (const char *topic_or_pattern_)
    {
        return zlink_spot_unsubscribe (_spot, topic_or_pattern_);
    }

    int recv (msgv_t &out_, std::string &topic_, int flags_ = 0)
    {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char topic_buf[256];
        size_t topic_len = sizeof (topic_buf);
        const int rc =
          zlink_spot_recv (_spot, &parts, &count, flags_, topic_buf, &topic_len);
        if (rc != 0)
            return rc;
        out_.adopt (parts, count);
        topic_.assign (topic_buf);
        return 0;
    }

    void *pub_handle () const { return zlink_spot_pub_socket (_spot); }
    void *sub_handle () const { return zlink_spot_sub_socket (_spot); }

    int destroy ()
    {
        if (!_spot)
            return 0;
        void *tmp = _spot;
        _spot = NULL;
        return zlink_spot_destroy (&tmp);
    }

    void *handle () const { return _spot; }

  private:
    void *_spot;
};

class atomic_counter_t
{
  public:
    atomic_counter_t () : _counter (zlink_atomic_counter_new ()) {}
    ~atomic_counter_t () { destroy (); }

    atomic_counter_t (atomic_counter_t &&other) noexcept
        : _counter (other._counter)
    {
        other._counter = NULL;
    }
    atomic_counter_t &operator= (atomic_counter_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        destroy ();
        _counter = other._counter;
        other._counter = NULL;
        return *this;
    }

    atomic_counter_t (const atomic_counter_t &) = delete;
    atomic_counter_t &operator= (const atomic_counter_t &) = delete;

    void set (int value_) { zlink_atomic_counter_set (_counter, value_); }
    int inc () { return zlink_atomic_counter_inc (_counter); }
    int dec () { return zlink_atomic_counter_dec (_counter); }
    int value () const { return zlink_atomic_counter_value (_counter); }

    void destroy ()
    {
        if (!_counter)
            return;
        void *tmp = _counter;
        _counter = NULL;
        zlink_atomic_counter_destroy (&tmp);
    }

  private:
    void *_counter;
};

class stopwatch_t
{
  public:
    stopwatch_t () : _watch (zlink_stopwatch_start ()) {}
    ~stopwatch_t () { _watch = NULL; }

    stopwatch_t (stopwatch_t &&other) noexcept : _watch (other._watch)
    {
        other._watch = NULL;
    }
    stopwatch_t &operator= (stopwatch_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        _watch = other._watch;
        other._watch = NULL;
        return *this;
    }

    stopwatch_t (const stopwatch_t &) = delete;
    stopwatch_t &operator= (const stopwatch_t &) = delete;

    unsigned long intermediate () { return zlink_stopwatch_intermediate (_watch); }
    unsigned long stop () { return zlink_stopwatch_stop (_watch); }

  private:
    void *_watch;
};

class thread_t
{
  public:
    thread_t () : _thread (NULL) {}
    explicit thread_t (zlink_thread_fn *fn_, void *arg_)
        : _thread (zlink_threadstart (fn_, arg_))
    {
    }
    ~thread_t () { close (); }

    thread_t (thread_t &&other) noexcept : _thread (other._thread)
    {
        other._thread = NULL;
    }
    thread_t &operator= (thread_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        close ();
        _thread = other._thread;
        other._thread = NULL;
        return *this;
    }

    thread_t (const thread_t &) = delete;
    thread_t &operator= (const thread_t &) = delete;

    void close ()
    {
        if (_thread) {
            zlink_threadclose (_thread);
            _thread = NULL;
        }
    }

  private:
    void *_thread;
};

inline int proxy (socket_t &frontend_, socket_t &backend_, socket_t *capture_ = NULL)
{
    return zlink_proxy (frontend_.handle (), backend_.handle (),
                        capture_ ? capture_->handle () : NULL);
}

inline int proxy_steerable (socket_t &frontend_,
                            socket_t &backend_,
                            socket_t *capture_,
                            socket_t &control_)
{
    return zlink_proxy_steerable (
      frontend_.handle (), backend_.handle (),
      capture_ ? capture_->handle () : NULL, control_.handle ());
}

inline bool has (const char *capability_) { return zlink_has (capability_) != 0; }

} // namespace zlink

#endif
