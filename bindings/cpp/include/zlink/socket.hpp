/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_HPP_INCLUDED

#include "context.hpp"
#include "message.hpp"
#include "types.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace zlink
{

/**
 * @brief RAII wrapper for a zlink socket handle.
 */
class socket_t
{
  public:
    /**
     * @brief Construct an empty socket wrapper.
     */
    socket_t () : _socket (NULL), _own (false) {}

    /**
     * @brief Create a new socket from a context.
     * @param ctx_ Context wrapper.
     * @param type_ Socket type.
     */
    socket_t (context_t &ctx_, socket_type type_)
        : _socket (zlink_socket (ctx_.handle (), static_cast<int> (type_))),
          _own (true)
    {
    }

    /**
     * @brief Close owned socket on destruction.
     */
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

    /**
     * @brief Adopt ownership of an existing native socket.
     * @param socket_ Native socket handle.
     * @return Owning wrapper.
     */
    static socket_t adopt (void *socket_)
    {
        socket_t s;
        s._socket = socket_;
        s._own = true;
        return s;
    }

    /**
     * @brief Wrap a native socket without taking ownership.
     * @param socket_ Native socket handle.
     * @return Non-owning wrapper.
     */
    static socket_t wrap (void *socket_)
    {
        socket_t s;
        s._socket = socket_;
        s._own = false;
        return s;
    }

    /**
     * @brief Access mutable native socket handle.
     * @return Native handle pointer.
     */
    void *handle () noexcept { return _socket; }
    /**
     * @brief Access const native socket handle.
     * @return Native handle pointer.
     */
    const void *handle () const noexcept { return _socket; }

    /**
     * @brief Bind socket to an endpoint.
     * @param endpoint_ Endpoint string.
     * @return 0 on success, -1 on failure.
     */
    int bind (const char *endpoint_) { return zlink_bind (_socket, endpoint_); }

    /**
     * @brief Bind socket to an endpoint.
     * @param endpoint_ Endpoint string.
     * @return 0 on success, -1 on failure.
     */
    int bind (const std::string &endpoint_)
    {
        return zlink_bind (_socket, endpoint_.c_str ());
    }

    /**
     * @brief Connect socket to an endpoint.
     * @param endpoint_ Endpoint string.
     * @return 0 on success, -1 on failure.
     */
    int connect (const char *endpoint_)
    {
        return zlink_connect (_socket, endpoint_);
    }

    /**
     * @brief Connect socket to an endpoint.
     * @param endpoint_ Endpoint string.
     * @return 0 on success, -1 on failure.
     */
    int connect (const std::string &endpoint_)
    {
        return zlink_connect (_socket, endpoint_.c_str ());
    }

    /**
     * @brief Unbind socket from an endpoint.
     * @param endpoint_ Endpoint string.
     * @return 0 on success, -1 on failure.
     */
    int unbind (const char *endpoint_)
    {
        return zlink_unbind (_socket, endpoint_);
    }

    /**
     * @brief Disconnect socket from an endpoint.
     * @param endpoint_ Endpoint string.
     * @return 0 on success, -1 on failure.
     */
    int disconnect (const char *endpoint_)
    {
        return zlink_disconnect (_socket, endpoint_);
    }

    /**
     * @brief Close the socket when this wrapper owns it.
     * @return 0 on success, -1 on failure.
     */
    int close () noexcept
    {
        int rc = 0;
        if (_socket && _own)
            rc = zlink_close (_socket);
        _socket = NULL;
        _own = false;
        return rc;
    }

    /**
     * @brief Send raw bytes.
     * @param buf_ Payload buffer.
     * @param len_ Payload length.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     */
    int send (const void *buf_, size_t len_, send_flag flags_ = send_flag::none)
    {
        return zlink_send (_socket, buf_, len_, static_cast<int> (flags_));
    }

    /**
     * @brief Send a string payload.
     * @param s_ Payload string.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     */
    int send (const std::string &s_, send_flag flags_ = send_flag::none)
    {
        return zlink_send (_socket, s_.data (), s_.size (),
                           static_cast<int> (flags_));
    }

    /**
     * @brief Send an external payload buffer without a pre-send copy.
     * @param data_ External payload buffer. May be `NULL` only when `len_` is 0.
     * @param len_ Payload length in bytes.
     * @param ffn_ Optional release callback for `data_`.
     * @param hint_ Optional callback context pointer.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     * @note Ownership of `data_` is consumed regardless of send result.
     */
    int send_zero (void *data_,
                   size_t len_,
                   zlink_free_fn *ffn_,
                   void *hint_ = NULL,
                   send_flag flags_ = send_flag::none)
    {
        if (len_ > 0 && !data_) {
            errno = EINVAL;
            return -1;
        }

        message_t msg;
        if (msg.init_data (data_, len_, ffn_, hint_) != 0)
            return -1;

        const int rc =
          zlink_msg_send (msg.handle (), _socket, static_cast<int> (flags_));
        (void) msg.close ();
        return rc;
    }

    /**
     * @brief Receive raw bytes.
     * @param buf_ Destination buffer.
     * @param len_ Destination capacity.
     * @param flags_ Receive flags.
     * @return Received byte count or -1 on failure.
     */
    int recv (void *buf_, size_t len_, recv_flag flags_ = recv_flag::none)
    {
        return zlink_recv (_socket, buf_, len_, static_cast<int> (flags_));
    }

    /**
     * @brief Send a message frame.
     * @param msg_ Message to send.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     * @note `msg_` is closed only on success. On failure it remains reusable.
     */
    int send (message_t &msg_, send_flag flags_ = send_flag::none)
    {
        const int rc = zlink_msg_send (msg_.handle (), _socket,
                                       static_cast<int> (flags_));
        if (rc >= 0)
            msg_.close ();
        return rc;
    }

    /**
     * @brief Receive into a message frame.
     * @param msg_ Destination message.
     * @param flags_ Receive flags.
     * @return Received byte count or -1 on failure.
     */
    int recv (message_t &msg_, recv_flag flags_ = recv_flag::none)
    {
        if (!msg_.valid () && msg_.init () != 0)
            return -1;
        return zlink_msg_recv (msg_.handle (), _socket,
                               static_cast<int> (flags_));
    }

    /**
     * @brief Attach framed stream callback.
     * @param on_packets_ Callback for packetized data.
     * @param flags_ Dispatch flags.
     * @return 0 on success, -1 on failure.
     */
    int stream_attach (zlink_stream_on_packets_fn on_packets_, int flags_ = 0)
    {
        return zlink_stream_attach (_socket, on_packets_, flags_);
    }

    /**
     * @brief Attach raw stream callback.
     * @param on_raw_ Callback for raw bytes.
     * @return 0 on success, -1 on failure.
     */
    int stream_attach_raw (zlink_stream_on_raw_fn on_raw_)
    {
        return zlink_stream_attach_raw (_socket, on_raw_);
    }

    /**
     * @brief Attach LEN32-BE framed callback parser.
     * @param on_packets_ Callback for decoded packets.
     * @return 0 on success, -1 on failure.
     */
    int stream_attach_len32be (zlink_stream_on_packets_fn on_packets_)
    {
        return zlink_stream_attach_len32be (_socket, on_packets_);
    }

    /**
     * @brief Attach framed stream callback with dispatch mode.
     * @param on_packets_ Callback for packetized data.
     * @param mode_ Stream dispatch mode.
     * @return 0 on success, -1 on failure.
     */
    int stream_attach (zlink_stream_on_packets_fn on_packets_,
                       stream_dispatch_mode mode_)
    {
        return stream_attach (on_packets_, static_cast<int> (mode_));
    }

    /**
     * @brief Detach stream callback.
     * @return 0 on success, -1 on failure.
     */
    int stream_detach () { return zlink_stream_detach (_socket); }

    /**
     * @brief Get routing id for a connected stream peer by index.
     * @param index_ Peer index.
     * @param out_ Output routing id as a 32-bit value.
     * @return 0 on success, -1 on failure.
     */
    int stream_peer_routing_id (int index_, uint32_t *out_)
    {
        if (!out_) {
            errno = EINVAL;
            return -1;
        }

        zlink_routing_id_t rid;
        std::memset (&rid, 0, sizeof (rid));
        if (zlink_socket_peer_routing_id (_socket, index_, &rid) != 0)
            return -1;

        if (rid.size != 4) {
            errno = EPROTO;
            return -1;
        }

        *out_ = decode_stream_routing_id (rid);
        return 0;
    }

    /**
     * @brief Send bytes to a stream peer by routing id value.
     * @param routing_id_ Peer routing id as a 32-bit value.
     * @param buf_ Payload buffer.
     * @param len_ Payload length.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     */
    int stream_send (uint32_t routing_id_,
                     const void *buf_,
                     size_t len_,
                     send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        encode_stream_routing_id (routing_id_, &rid);
        return zlink_stream_send (_socket, &rid, buf_, len_,
                                  static_cast<int> (flags_));
    }

    /**
     * @brief Send bytes to a stream peer by binary routing id.
     * @param routing_id_ Target peer routing id bytes.
     * @param buf_ Payload buffer.
     * @param len_ Payload length.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     */
    int stream_send (const zlink_routing_id_t &routing_id_,
                     const void *buf_,
                     size_t len_,
                     send_flag flags_ = send_flag::none)
    {
        return zlink_stream_send (_socket, &routing_id_, buf_, len_,
                                  static_cast<int> (flags_));
    }

    /**
     * @brief Send bytes to a stream peer by binary-string routing id.
     * @param routing_id_ Target peer routing id bytes.
     * @param buf_ Payload buffer.
     * @param len_ Payload length.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     */
    int stream_send (const std::string &routing_id_,
                     const void *buf_,
                     size_t len_,
                     send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        if (routing_id_from_string (routing_id_, &rid) != 0)
            return -1;
        return stream_send (rid, buf_, len_, flags_);
    }

    /**
     * @brief Send string to a stream peer by routing id value.
     * @param routing_id_ Peer routing id as a 32-bit value.
     * @param payload_ Payload string.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     */
    int stream_send (uint32_t routing_id_,
                     const std::string &payload_,
                     send_flag flags_ = send_flag::none)
    {
        return stream_send (routing_id_, payload_.data (), payload_.size (),
                            flags_);
    }

    /**
     * @brief Send string payload to a stream peer by binary routing id.
     * @param routing_id_ Target peer routing id bytes.
     * @param payload_ Payload string.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     */
    int stream_send (const std::string &routing_id_,
                     const std::string &payload_,
                     send_flag flags_ = send_flag::none)
    {
        return stream_send (routing_id_, payload_.data (), payload_.size (),
                            flags_);
    }

    /**
     * @brief Send a message frame to a stream peer.
     * @param routing_id_ Peer routing id as a 32-bit value.
     * @param msg_ Message frame.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     * @note `msg_` ownership is transferred and it is closed regardless of result.
     */
    int stream_send_msg (uint32_t routing_id_,
                         message_t &msg_,
                         send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        encode_stream_routing_id (routing_id_, &rid);
        const int rc = zlink_stream_send_msg (
          _socket, &rid, msg_.handle (), static_cast<int> (flags_));
        msg_.close ();
        return rc;
    }

    /**
     * @brief Send a message frame to a stream peer by binary routing id.
     * @param routing_id_ Target peer routing id bytes.
     * @param msg_ Message frame.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     * @note `msg_` ownership is transferred and it is closed regardless of result.
     */
    int stream_send_msg (const zlink_routing_id_t &routing_id_,
                         message_t &msg_,
                         send_flag flags_ = send_flag::none)
    {
        const int rc = zlink_stream_send_msg (
          _socket, &routing_id_, msg_.handle (), static_cast<int> (flags_));
        msg_.close ();
        return rc;
    }

    /**
     * @brief Send a message frame to a stream peer by binary-string routing id.
     * @param routing_id_ Target peer routing id bytes.
     * @param msg_ Message frame.
     * @param flags_ Send flags.
     * @return Sent byte count or -1 on failure.
     * @note `msg_` ownership is transferred and it is closed regardless of result.
     */
    int stream_send_msg (const std::string &routing_id_,
                         message_t &msg_,
                         send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        if (routing_id_from_string (routing_id_, &rid) != 0)
            return -1;
        return stream_send_msg (rid, msg_, flags_);
    }

    /**
     * @brief Set a raw socket option.
     * @param option_ Option key.
     * @param optval_ Option value buffer.
     * @param optlen_ Buffer size.
     * @return 0 on success, -1 on failure.
     */
    int set (socket_option option_, const void *optval_, size_t optlen_)
    {
        return zlink_setsockopt (_socket, static_cast<int> (option_), optval_,
                                 optlen_);
    }

    /**
     * @brief Get a raw socket option.
     * @param option_ Option key.
     * @param optval_ Output value buffer.
     * @param optlen_ Input/output buffer size.
     * @return 0 on success, -1 on failure.
     */
    int get (socket_option option_, void *optval_, size_t *optlen_) const
    {
        return zlink_getsockopt (_socket, static_cast<int> (option_), optval_,
                                 optlen_);
    }

    /**
     * @brief Set an integer socket option.
     * @param option_ Option key.
     * @param value_ Integer value.
     * @return 0 on success, -1 on failure.
     */
    int set (socket_option option_, int value_)
    {
        return zlink_setsockopt (_socket, static_cast<int> (option_), &value_,
                                 sizeof (value_));
    }

    /**
     * @brief Get an integer socket option.
     * @param option_ Option key.
     * @param value_ Output value pointer.
     * @return 0 on success, -1 on failure.
     */
    int get (socket_option option_, int *value_) const
    {
        if (!value_) {
            errno = EINVAL;
            return -1;
        }

        size_t len = sizeof (*value_);
        return zlink_getsockopt (_socket, static_cast<int> (option_), value_,
                                 &len);
    }

    /**
     * @brief Set a string socket option.
     * @param option_ Option key.
     * @param value_ Option value string.
     * @return 0 on success, -1 on failure.
     */
    int set (socket_option option_, const std::string &value_)
    {
        return zlink_setsockopt (_socket, static_cast<int> (option_),
                                 value_.data (), value_.size ());
    }

    /**
     * @brief Get a string socket option.
     * @param option_ Option key.
     * @param value_ Output string.
     * @return 0 on success, -1 on failure.
     */
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

        const size_t bounded_len = len <= buf.size () ? len : buf.size ();
        size_t out_len = bounded_len;
        if (out_len > 0 && buf[out_len - 1] == '\0')
            --out_len;
        value_.assign (buf.data (), out_len);
        return 0;
    }

    /**
     * @brief Configure socket event monitoring endpoint.
     * @param addr_ Monitor endpoint.
     * @param events_ Event mask.
     * @return 0 on success, -1 on failure.
     */
    int monitor (const char *addr_, monitor_event events_)
    {
        return zlink_socket_monitor (_socket, addr_, static_cast<int> (events_));
    }

    /**
     * @brief Open an inproc monitor socket for this socket.
     * @param events_ Event mask.
     * @return Owning monitor socket wrapper.
     */
    socket_t monitor_open (monitor_event events_)
    {
        void *m = zlink_socket_monitor_open (_socket, static_cast<int> (events_));
        return socket_t::adopt (m);
    }

    /**
     * @brief Query metadata for a peer routing id.
     * @param routing_id_ Peer routing id.
     * @param info_ Output peer info.
     * @return 0 on success, -1 on failure.
     */
    int peer_info (const zlink_routing_id_t &routing_id_,
                   zlink_peer_info_t *info_) const
    {
        return zlink_socket_peer_info (_socket, &routing_id_, info_);
    }

    /**
     * @brief Query metadata for a peer routing id byte sequence.
     * @param routing_id_bytes_ Peer routing id byte buffer.
     * @param routing_id_size_ Byte size of routing id.
     * @param info_ Output peer info.
     * @return 0 on success, -1 on failure.
     */
    int peer_info (const void *routing_id_bytes_,
                   size_t routing_id_size_,
                   zlink_peer_info_t *info_) const
    {
        zlink_routing_id_t routing_id;
        if (routing_id_from_bytes (
              routing_id_bytes_, routing_id_size_, &routing_id)
            != 0)
            return -1;
        return peer_info (routing_id, info_);
    }

    /**
     * @brief Query metadata for a peer routing id binary string.
     * @param routing_id_ Peer routing id bytes.
     * @param info_ Output peer info.
     * @return 0 on success, -1 on failure.
     */
    int peer_info (const std::string &routing_id_, zlink_peer_info_t *info_) const
    {
        return peer_info (routing_id_.data (), routing_id_.size (), info_);
    }

    /**
     * @brief Fetch peer routing id by index as a binary string.
     * @param index_ Peer index.
     * @param out_ Output routing id bytes.
     * @return 0 on success, -1 on failure.
     */
    int peer_routing_id (int index_, std::string &out_) const
    {
        zlink_routing_id_t rid;
        std::memset (&rid, 0, sizeof (rid));
        if (zlink_socket_peer_routing_id (_socket, index_, &rid) != 0)
            return -1;
        out_ = routing_id_to_string (rid);
        return 0;
    }

    /**
     * @brief Count currently known peers.
     * @return Peer count, or -1 on failure.
     */
    int peer_count () const
    {
        return _socket ? zlink_socket_peer_count (_socket) : -1;
    }

    /**
     * @brief Enumerate currently known peers.
     * @param peers_ Output peer array.
     * @param count_ In/out capacity and written count.
     * @return 0 on success, -1 on failure.
     */
    int peers (zlink_peer_info_t *peers_, size_t *count_) const
    {
        return zlink_socket_peers (_socket, peers_, count_);
    }

  private:
    static void encode_stream_routing_id (uint32_t routing_id_,
                                          zlink_routing_id_t *out_) noexcept
    {
        std::memset (out_, 0, sizeof (*out_));
        out_->size = 4;
        out_->data[0] = static_cast<uint8_t> ((routing_id_ >> 24) & 0xFF);
        out_->data[1] = static_cast<uint8_t> ((routing_id_ >> 16) & 0xFF);
        out_->data[2] = static_cast<uint8_t> ((routing_id_ >> 8) & 0xFF);
        out_->data[3] = static_cast<uint8_t> (routing_id_ & 0xFF);
    }

    static uint32_t decode_stream_routing_id (
      const zlink_routing_id_t &routing_id_) noexcept
    {
        return (static_cast<uint32_t> (routing_id_.data[0]) << 24)
               | (static_cast<uint32_t> (routing_id_.data[1]) << 16)
               | (static_cast<uint32_t> (routing_id_.data[2]) << 8)
               | static_cast<uint32_t> (routing_id_.data[3]);
    }

    void *_socket;
    bool _own;
};

} // namespace zlink

#endif
