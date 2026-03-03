/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_HPP_INCLUDED

#include "context.hpp"
#include "message.hpp"
#include "types.hpp"

namespace zlink
{

class socket_t
{
  public:
    socket_t () : _socket (NULL), _own (false) {}

    socket_t (context_t &ctx_, socket_type type_)
        : _socket (zlink_socket (ctx_.handle (), static_cast<int> (type_))),
          _own (true)
    {
    }

    ~socket_t () { close (); }

    socket_t (socket_t &&other) noexcept
        : _socket (other._socket), _own (other._own)
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
    const void *handle () const noexcept { return _socket; }

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

    int send (const void *buf_, size_t len_, send_flag flags_ = send_flag::none)
    {
        return zlink_send (_socket, buf_, len_, static_cast<int> (flags_));
    }

    int send (const std::string &s_, send_flag flags_ = send_flag::none)
    {
        return zlink_send (_socket, s_.data (), s_.size (),
                           static_cast<int> (flags_));
    }

    int recv (void *buf_, size_t len_, recv_flag flags_ = recv_flag::none)
    {
        return zlink_recv (_socket, buf_, len_, static_cast<int> (flags_));
    }

    int send (message_t &msg_, send_flag flags_ = send_flag::none)
    {
        const int rc = zlink_msg_send (msg_.handle (), _socket,
                                       static_cast<int> (flags_));
        if (rc >= 0)
            msg_.close ();
        return rc;
    }

    int send_const (const void *buf_,
                    size_t len_,
                    send_flag flags_ = send_flag::none)
    {
        return zlink_send_const (_socket, buf_, len_, static_cast<int> (flags_));
    }

    int recv (message_t &msg_, recv_flag flags_ = recv_flag::none)
    {
        if (!msg_.valid () && msg_.init () != 0)
            return -1;
        return zlink_msg_recv (msg_.handle (), _socket,
                               static_cast<int> (flags_));
    }

    int stream_attach (zlink_stream_on_packets_fn on_packets_, int flags_ = 0)
    {
        return zlink_stream_attach (_socket, on_packets_, flags_);
    }

    int stream_attach_raw (zlink_stream_on_raw_fn on_raw_)
    {
        return zlink_stream_attach_raw (_socket, on_raw_);
    }

    int stream_attach_len32be (zlink_stream_on_packets_fn on_packets_)
    {
        return zlink_stream_attach_len32be (_socket, on_packets_);
    }

    int stream_attach (zlink_stream_on_packets_fn on_packets_,
                       stream_dispatch_mode mode_)
    {
        return stream_attach (on_packets_, static_cast<int> (mode_));
    }

    int stream_detach () { return zlink_stream_detach (_socket); }

    int stream_peer_routing_id (int index_, zlink_routing_id_t *out_)
    {
        return zlink_socket_peer_routing_id (_socket, index_, out_);
    }

    int stream_send (const zlink_routing_id_t &routing_id_,
                     const void *buf_,
                     size_t len_,
                     send_flag flags_ = send_flag::none)
    {
        return zlink_stream_send (
          _socket, &routing_id_, buf_, len_, static_cast<int> (flags_));
    }

    int stream_send (const std::vector<unsigned char> &routing_id_,
                     const void *buf_,
                     size_t len_,
                     send_flag flags_ = send_flag::none)
    {
        if (routing_id_.empty () || routing_id_.size () > 255)
            return -1;

        zlink_routing_id_t rid;
        memset (&rid, 0, sizeof (rid));
        rid.size = static_cast<uint8_t> (routing_id_.size ());
        memcpy (rid.data, routing_id_.data (), routing_id_.size ());
        return stream_send (rid, buf_, len_, flags_);
    }

    int stream_send (const std::vector<unsigned char> &routing_id_,
                     const std::string &payload_,
                     send_flag flags_ = send_flag::none)
    {
        return stream_send (routing_id_, payload_.data (), payload_.size (), flags_);
    }

    int stream_send_msg (const zlink_routing_id_t &routing_id_,
                         message_t &msg_,
                         send_flag flags_ = send_flag::none)
    {
        return zlink_stream_send_msg (
          _socket, &routing_id_, msg_.handle (), static_cast<int> (flags_));
    }

    int stream_send_msg (const std::vector<unsigned char> &routing_id_,
                         message_t &msg_,
                         send_flag flags_ = send_flag::none)
    {
        if (routing_id_.empty () || routing_id_.size () > 255)
            return -1;

        zlink_routing_id_t rid;
        memset (&rid, 0, sizeof (rid));
        rid.size = static_cast<uint8_t> (routing_id_.size ());
        memcpy (rid.data, routing_id_.data (), routing_id_.size ());
        return stream_send_msg (rid, msg_, flags_);
    }

    int set (socket_option option_, const void *optval_, size_t optlen_)
    {
        return zlink_setsockopt (_socket, static_cast<int> (option_), optval_,
                                 optlen_);
    }

    int get (socket_option option_, void *optval_, size_t *optlen_) const
    {
        return zlink_getsockopt (_socket, static_cast<int> (option_), optval_,
                                 optlen_);
    }

    int set (socket_option option_, int value_)
    {
        return zlink_setsockopt (_socket, static_cast<int> (option_), &value_,
                                 sizeof (value_));
    }

    int get (socket_option option_, int *value_) const
    {
        if (!value_)
            return -1;

        size_t len = sizeof (*value_);
        return zlink_getsockopt (_socket, static_cast<int> (option_), value_,
                                 &len);
    }

    int set (socket_option option_, const std::string &value_)
    {
        return zlink_setsockopt (_socket, static_cast<int> (option_),
                                 value_.data (), value_.size ());
    }

    int get (socket_option option_, std::string &value_) const
    {
        size_t len = 256;
        std::vector<char> buf (len);
        if (zlink_getsockopt (
              _socket, static_cast<int> (option_), buf.data (), &len)
            != 0)
            return -1;

        if (len > buf.size ()) {
            buf.resize (len);
            if (zlink_getsockopt (
                  _socket, static_cast<int> (option_), buf.data (), &len)
                != 0)
                return -1;
        }

        value_.assign (buf.data (), len);
        return 0;
    }

    int monitor (const char *addr_, monitor_event events_)
    {
        return zlink_socket_monitor (_socket, addr_, static_cast<int> (events_));
    }

    socket_t monitor_open (monitor_event events_)
    {
        void *m = zlink_socket_monitor_open (_socket, static_cast<int> (events_));
        return socket_t::adopt (m);
    }

    int peer_info (const zlink_routing_id_t &routing_id_,
                   zlink_peer_info_t *info_) const
    {
        return zlink_socket_peer_info (_socket, &routing_id_, info_);
    }

    int peer_count () const
    {
        return _socket ? zlink_socket_peer_count (_socket) : -1;
    }

    int peers (zlink_peer_info_t *peers_, size_t *count_) const
    {
        return zlink_socket_peers (_socket, peers_, count_);
    }

  private:
    void *_socket;
    bool _own;
};

} // namespace zlink

#endif
