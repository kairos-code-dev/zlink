/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_HPP_INCLUDED

#include "../context.hpp"
#include "../message.hpp"
#include "../types.hpp"

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <type_traits>

namespace zlink
{
namespace service
{

namespace detail
{

inline int spot_pub_option_from_socket_option (socket_option option_)
{
    switch (option_) {
    case socket_option::sndhwm:
        return ZLINK_SPOT_PUB_OPT_SNDHWM;
    case socket_option::sndtimeo:
        return ZLINK_SPOT_PUB_OPT_SNDTIMEO;
    case socket_option::linger:
        return ZLINK_SPOT_PUB_OPT_LINGER;
    case socket_option::xpub_nodrop:
        return ZLINK_SPOT_PUB_OPT_NODROP;
    case socket_option::sndbuf:
        return ZLINK_SPOT_PUB_OPT_SNDBUF;
    case socket_option::rcvbuf:
        return ZLINK_SPOT_PUB_OPT_RCVBUF;
    default:
        errno = EINVAL;
        return -1;
    }
}

inline int spot_sub_option_from_socket_option (socket_option option_)
{
    switch (option_) {
    case socket_option::rcvhwm:
        return ZLINK_SPOT_SUB_OPT_RCVHWM;
    case socket_option::rcvtimeo:
        return ZLINK_SPOT_SUB_OPT_RCVTIMEO;
    case socket_option::linger:
        return ZLINK_SPOT_SUB_OPT_LINGER;
    case socket_option::sndbuf:
        return ZLINK_SPOT_SUB_OPT_SNDBUF;
    case socket_option::rcvbuf:
        return ZLINK_SPOT_SUB_OPT_RCVBUF;
    default:
        errno = EINVAL;
        return -1;
    }
}

} // namespace detail

/**
 * @brief Spot node wrapper that manages publish/subscribe service sockets.
 */
class spot_node_t
{
  public:
    /**
     * @brief Create a spot node.
     * @param ctx_ Context wrapper.
     */
    explicit spot_node_t (context_t &ctx_)
        : _node (zlink_spot_node_new (ctx_.handle (), NULL, NULL, NULL))
    {
    }

    /**
     * @brief Destroy node handle.
     */
    ~spot_node_t () { (void) destroy (); }

    spot_node_t (spot_node_t &&other) noexcept : _node (other._node)
    {
        other._node = NULL;
    }

    spot_node_t &operator= (spot_node_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) destroy ();
        _node = other._node;
        other._node = NULL;
        return *this;
    }

    spot_node_t (const spot_node_t &) = delete;
    spot_node_t &operator= (const spot_node_t &) = delete;

    /**
     * @brief Bind spot node endpoint.
     * @param endpoint_ Bind endpoint.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int bind (const std::string &endpoint_)
    {
        return zlink_spot_node_bind (_node, endpoint_.c_str ());
    }

    /**
     * @brief Connect to another spot node's PUB endpoint.
     * @param endpoint_ Peer PUB endpoint.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int connect_peer_pub (const std::string &endpoint_)
    {
        return zlink_spot_node_connect_peer_pub (_node, endpoint_.c_str ());
    }

    /**
     * @brief Disconnect from a peer PUB endpoint.
     * @param endpoint_ Peer PUB endpoint.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int disconnect_peer_pub (const std::string &endpoint_)
    {
        return zlink_spot_node_disconnect_peer_pub (_node, endpoint_.c_str ());
    }

    /**
     * @brief Register spot service endpoint.
     * @param service_ Service name.
     * @param advertise_ Advertised endpoint.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    register_service (const std::string &service_,
                      const std::string &advertise_)
    {
        return zlink_spot_node_register (
          _node, service_.c_str (), advertise_.c_str ());
    }

    /**
     * @brief Unregister a spot service.
     * @param service_ Service name.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int unregister_service (const std::string &service_)
    {
        return zlink_spot_node_unregister (_node, service_.c_str ());
    }

    /**
     * @brief Inject external discovery handle for a service.
     * @param discovery_ Native discovery handle.
     * @param service_ Service name.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_discovery (void *discovery_, const std::string &service_)
    {
        return zlink_spot_node_set_discovery (
          _node, discovery_, service_.c_str ());
    }

    /**
     * @brief Configure TLS server certificate for node sockets.
     * @param cert_ Certificate file path.
     * @param key_ Private key file path.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_tls_server (const std::string &cert_, const std::string &key_)
    {
        return zlink_spot_node_set_tls_server (
          _node, cert_.c_str (), key_.c_str ());
    }

    /**
     * @brief Configure TLS client settings for node sockets.
     * @param ca_ CA file path.
     * @param hostname_ Expected peer hostname.
     * @param trust_ Trust-system flag.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_tls_client (const std::string &ca_,
                    const std::string &hostname_,
                    int trust_)
    {
        const char *ca = ca_.empty () ? NULL : ca_.c_str ();
        const char *hostname = hostname_.empty () ? NULL : hostname_.c_str ();
        return zlink_spot_node_set_tls_client (
          _node, ca, hostname, trust_);
    }

    /**
     * @brief Configure TLS client settings for node sockets.
     * @param ca_ CA file path.
     * @param hostname_ Expected peer hostname.
     * @param trust_ Trust-system flag.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_tls_client (const std::string &ca_,
                    const std::string &hostname_,
                    bool trust_)
    {
        return set_tls_client (ca_, hostname_, trust_ ? 1 : 0);
    }

    /**
     * @brief Set socket option by internal role.
     * @param role_ Internal socket role.
     * @param option_ Socket option key.
     * @param value_ Option value buffer.
     * @param len_ Buffer length in bytes.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (spot_node_socket_role role_,
                 socket_option option_,
                 const void *value_,
                 size_t len_)
    {
        int mapped = -1;

        switch (role_) {
        case spot_node_socket_role::pub:
            mapped = detail::spot_pub_option_from_socket_option (option_);
            if (mapped < 0)
                return -1;
            return zlink_spot_node_set_pub_option (_node, mapped, value_, len_);
        case spot_node_socket_role::sub:
            mapped = detail::spot_sub_option_from_socket_option (option_);
            if (mapped < 0)
                return -1;
            return zlink_spot_node_set_sub_option (_node, mapped, value_, len_);
        case spot_node_socket_role::node:
            errno = ENOTSUP;
            return -1;
        default:
            errno = EINVAL;
            return -1;
        }
    }

    /**
     * @brief Set typed non-string socket option by internal role.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Typed option value.
     * @return 0 on success, -1 on failure.
     */
    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_sockopt (spot_node_socket_role role_,
                 socket_option_key_t<T> key_,
                 const T &value_)
    {
        return set_sockopt (role_, key_.option, &value_, sizeof (value_));
    }

    /**
     * @brief Set typed string socket option by internal role.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Option value bytes.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (spot_node_socket_role role_,
                 socket_option_key_t<std::string> key_,
                 const std::string &value_)
    {
        return set_sockopt (role_, key_.option, value_.data (), value_.size ());
    }

    /**
     * @brief Set integer socket option by internal role.
     * @param role_ Internal socket role.
     * @param option_ Socket option key.
     * @param value_ Integer option value.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (spot_node_socket_role role_,
                 socket_option option_,
                 int value_)
    {
        return set_sockopt (role_, option_, &value_, sizeof (value_));
    }

    /**
     * @brief Set node-level integer option.
     * @param option_ Node option key.
     * @param value_ Integer option value.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int set_sockopt (spot_node_option option_, int value_)
    {
        return zlink_spot_node_set_pub_option (
          _node, static_cast<int> (option_), &value_, sizeof (value_));
    }

    /**
     * @brief Access internal PUB socket handle.
     * @return Native socket handle.
     */
    void *pub_socket_handle () const
    {
        return zlink_spot_node_default_pub (_node);
    }

    /**
     * @brief Access internal SUB socket handle.
     * @return Native socket handle.
     */
    void *sub_socket_handle () const
    {
        return zlink_spot_node_default_sub (_node);
    }

    /**
     * @brief Enumerate PUB socket peers.
     * @param peers_ Output peer array.
     * @param count_ In/out capacity and written count.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    pub_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        void *pub = zlink_spot_node_default_pub (_node);
        if (!pub)
            return -1;

        return zlink_spot_pub_peers (pub, peers_, count_);
    }

    /**
     * @brief Enumerate SUB socket peers.
     * @param peers_ Output peer array.
     * @param count_ In/out capacity and written count.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    sub_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        void *sub = zlink_spot_node_default_sub (_node);
        if (!sub)
            return -1;

        return zlink_spot_sub_peers (sub, peers_, count_);
    }

    /**
     * @brief Explicitly destroy node handle.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int destroy ()
    {
        if (!_node)
            return 0;

        void *tmp = _node;
        _node = NULL;
        return zlink_spot_node_destroy (&tmp);
    }

    /**
     * @brief Access raw native node handle.
     * @return Native handle pointer.
     */
    void *handle () const { return _node; }

  private:
    void *_node;
};

/**
 * @brief Spot pub/sub convenience wrapper for one node.
 */
class spot_t
{
  public:
    /**
     * @brief Subscription callback type.
     */
    typedef std::function<void (const std::string &, const zlink_msg_t *, size_t)>
      handler_fn;

    /**
     * @brief Create spot pub/sub handles from a node.
     * @param node_ Spot node wrapper.
     */
    explicit spot_t (spot_node_t &node_)
        : _node (node_.handle ()),
          _pub (zlink_spot_pub_new (node_.handle ())),
          _sub (zlink_spot_sub_new (node_.handle ())),
          _last_error (0)
    {
        if (_pub && _sub)
            return;

        const int err = errno;
        if (_pub)
            zlink_spot_pub_destroy (&_pub);
        if (_sub)
            zlink_spot_sub_destroy (&_sub);
        _pub = NULL;
        _sub = NULL;
        _last_error = err != 0 ? err : EFAULT;
    }

    /**
     * @brief Destroy pub/sub handles.
     */
    ~spot_t () { (void) destroy (); }

    spot_t (spot_t &&other) noexcept
        : _node (NULL), _pub (NULL), _sub (NULL), _last_error (0)
    {
        (void) other.set_handler (handler_fn ());
        _node = other._node;
        _pub = other._pub;
        _sub = other._sub;
        _last_error = other._last_error;
        other._node = NULL;
        other._pub = NULL;
        other._sub = NULL;
        other._last_error = 0;
    }

    spot_t &operator= (spot_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) set_handler (handler_fn ());
        (void) destroy ();
        (void) other.set_handler (handler_fn ());
        _node = other._node;
        _pub = other._pub;
        _sub = other._sub;
        _last_error = other._last_error;
        other._node = NULL;
        other._pub = NULL;
        other._sub = NULL;
        other._last_error = 0;
        return *this;
    }

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    /**
     * @brief Check whether publisher/subscriber handles were created.
     * @return `true` when both handles are valid.
     */
    bool valid () const noexcept { return _pub != NULL && _sub != NULL; }

    /**
     * @brief Return the last constructor-time initialization error.
     * @return Error number, or 0 when initialized successfully.
     */
    int last_error () const noexcept { return _last_error; }

    /**
     * @brief Publish multipart payload on a topic.
     * @param topic_ Topic string.
     * @param parts_ Message parts. Ownership is transferred regardless of result.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    publish (const std::string &topic_,
             std::vector<message_t> &parts_,
             send_flag flags_ = send_flag::none)
    {
        if (!_pub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        _publish_parts_scratch.resize (parts_.size ());
        size_t moved = 0;
        if (move_parts_transfer (parts_, _publish_parts_scratch, moved) != 0) {
            _publish_parts_scratch.clear ();
            return -1;
        }

        const int rc = zlink_spot_pub_publish (
          _pub, topic_.c_str (), _publish_parts_scratch.data (),
          _publish_parts_scratch.size (),
          static_cast<int> (flags_));
        if (rc != 0) {
            const int err = errno;
            close_native_parts (_publish_parts_scratch, moved);
            errno = err;
        }
        _publish_parts_scratch.clear ();
        return rc;
    }

    /**
     * @brief Publish single-frame bytes on a topic.
     * @param topic_ Topic string.
     * @param data_ Payload pointer.
     * @param size_ Payload size in bytes.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    publish (const std::string &topic_,
             const void *data_,
             size_t size_,
             send_flag flags_ = send_flag::none)
    {
        if (!_pub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }
        return zlink_spot_pub_publish_bytes (
          _pub, topic_.c_str (), data_, size_, static_cast<int> (flags_));
    }

    /**
     * @brief Publish single-frame external buffer without a pre-send copy.
     * @param topic_ Topic string.
     * @param data_ External payload buffer. May be `NULL` only when `size_` is 0.
     * @param size_ Payload size in bytes.
     * @param ffn_ Optional release callback for `data_`.
     * @param hint_ Optional callback context pointer.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     * @note Once initialized, ownership of `data_` is consumed regardless of publish result.
     */
    ZLINK_CPP_NODISCARD int
    publish_zero (const std::string &topic_,
                  void *data_,
                  size_t size_,
                  zlink_free_fn *ffn_,
                  void *hint_ = NULL,
                  send_flag flags_ = send_flag::none)
    {
        if (!_pub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }
        if (size_ > 0 && !data_) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t part;
        if (zlink_msg_init_data (&part, data_, size_, ffn_, hint_) != 0)
            return -1;

        const int rc = zlink_spot_pub_publish (
          _pub, topic_.c_str (), &part, 1, static_cast<int> (flags_));
        if (rc != 0) {
            const int err = errno;
            zlink_msg_close (&part);
            errno = err;
        }
        return rc;
    }

    /**
     * @brief Subscribe to an exact topic.
     * @param topic_ Topic string.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int subscribe (const std::string &topic_)
    {
        if (!_sub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }
        return zlink_spot_sub_subscribe (_sub, topic_.c_str ());
    }

    /**
     * @brief Subscribe using a topic pattern.
     * @param pattern_ Pattern string.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int subscribe_pattern (const std::string &pattern_)
    {
        if (!_sub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }
        return zlink_spot_sub_subscribe_pattern (_sub, pattern_.c_str ());
    }

    /**
     * @brief Remove topic or pattern subscription.
     * @param topic_or_pattern_ Topic or pattern string.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int unsubscribe (const std::string &topic_or_pattern_)
    {
        if (!_sub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }
        return zlink_spot_sub_unsubscribe (_sub, topic_or_pattern_.c_str ());
    }

    /**
     * @brief Register or clear async message handler.
     * @param fn_ Handler function. Empty function disables callbacks.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int set_handler (handler_fn fn_)
    {
        const bool enable = static_cast<bool> (fn_);
        if (!_sub) {
            if (enable) {
                errno = _last_error != 0 ? _last_error : EFAULT;
                return -1;
            }

            std::lock_guard<std::mutex> lock (_handler_sync);
            _handler_fn = handler_fn ();
            return 0;
        }

        const int rc = zlink_spot_sub_set_handler (
          _sub, enable ? &spot_t::handler_trampoline : NULL, this);
        if (rc != 0)
            return -1;

        std::lock_guard<std::mutex> lock (_handler_sync);
        _handler_fn = std::move (fn_);
        return 0;
    }

    /**
     * @brief Receive one multipart publication.
     * @param out_ Output message frames.
     * @param topic_ Output topic.
     * @param flags_ Receive flags.
     * @param topic_len_out_ Optional output for full topic byte length.
     * @param truncated_out_ Optional output for topic truncation flag.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    recv (std::vector<message_t> &out_,
          std::string &topic_,
          recv_flag flags_ = recv_flag::none,
          size_t *topic_len_out_ = NULL,
          bool *truncated_out_ = NULL)
    {
        if (!_sub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }

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

        const bool truncated = topic_len > (sizeof (topic_buf) - 1);
        if (topic_len_out_)
            *topic_len_out_ = topic_len;
        if (truncated_out_)
            *truncated_out_ = truncated;

        const size_t topic_size =
          topic_len < sizeof (topic_buf) ? topic_len : sizeof (topic_buf) - 1;
        topic_.assign (topic_buf, topic_size);
        return move_received_parts (out_, parts, count);
    }

    /**
     * @brief Receive one single-frame payload into a caller buffer.
     * @param topic_ Output topic string.
     * @param data_ Destination payload buffer.
     * @param size_ Destination buffer size in bytes.
     * @param received_size_ Output payload size copied.
     * @param flags_ Receive flags.
     * @param topic_len_out_ Optional output for full topic byte length.
     * @param truncated_out_ Optional output for topic truncation flag.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    recv (std::string &topic_,
          void *data_,
          size_t size_,
          size_t *received_size_,
          recv_flag flags_ = recv_flag::none,
          size_t *topic_len_out_ = NULL,
          bool *truncated_out_ = NULL)
    {
        if (!_sub) {
            errno = _last_error != 0 ? _last_error : EFAULT;
            return -1;
        }
        if (!received_size_ || (size_ > 0 && !data_)) {
            errno = EINVAL;
            return -1;
        }

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

        const bool truncated = topic_len > (sizeof (topic_buf) - 1);
        if (topic_len_out_)
            *topic_len_out_ = topic_len;
        if (truncated_out_)
            *truncated_out_ = truncated;

        const size_t topic_size =
          topic_len < sizeof (topic_buf) ? topic_len : sizeof (topic_buf) - 1;
        topic_.assign (topic_buf, topic_size);

        *received_size_ = 0;
        if (!parts || count != 1) {
            close_received_parts (parts, count);
            errno = EPROTO;
            return -1;
        }

        const size_t payload_size = zlink_msg_size (&parts[0]);
        if (payload_size > size_) {
            close_received_parts (parts, count);
            errno = EMSGSIZE;
            return -1;
        }

        if (payload_size > 0) {
            void *payload_data = zlink_msg_data (&parts[0]);
            if (payload_data && data_)
                memcpy (data_, payload_data, payload_size);
        }
        *received_size_ = payload_size;
        close_received_parts (parts, count);
        return 0;
    }

    /**
     * @brief Explicitly destroy pub/sub handles.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int destroy ()
    {
        int rc = 0;
        if (_sub)
            (void) set_handler (handler_fn ());

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

    /**
     * @brief Access native publisher handle.
     * @return Native handle pointer.
     */
    void *pub_handle () const { return _pub; }
    /**
     * @brief Access native publisher handle.
     * @return Native handle pointer.
     */
    void *publisher_handle () const { return _pub; }
    /**
     * @brief Access native subscriber handle.
     * @return Native handle pointer.
     */
    void *sub_handle () const { return _sub; }
    /**
     * @brief Access native subscriber handle.
     * @return Native handle pointer.
     */
    void *subscriber_handle () const { return _sub; }
    /**
     * @brief Access native publisher handle (legacy alias).
     * @return Native handle pointer.
     */
    void *handle () const { return pub_handle (); }

  private:
    static void close_received_parts (zlink_msg_t *parts_, size_t count_) noexcept
    {
        if (!parts_)
            return;
        for (size_t i = 0; i < count_; ++i)
            zlink_msg_close (&parts_[i]);
        std::free (parts_);
    }

    static void close_native_parts (std::vector<zlink_msg_t> &parts_,
                                    size_t count_) noexcept
    {
        for (size_t i = 0; i < count_; ++i)
            zlink_msg_close (&parts_[i]);
    }

    static int move_parts_transfer (std::vector<message_t> &parts_,
                                    std::vector<zlink_msg_t> &native_parts_,
                                    size_t &moved_)
    {
        moved_ = 0;
        for (size_t i = 0; i < parts_.size (); ++i) {
            if (parts_[i].move_to (&native_parts_[i]) != 0) {
                const int err = errno;
                close_native_parts (native_parts_, moved_);
                for (size_t j = i; j < parts_.size (); ++j)
                    parts_[j].close ();
                errno = err;
                return -1;
            }
            ++moved_;
        }
        return 0;
    }

    static int move_received_parts (std::vector<message_t> &out_,
                                    zlink_msg_t *parts_,
                                    size_t count_)
    {
        out_.clear ();
        if (!parts_ || count_ == 0) {
            if (parts_)
                close_received_parts (parts_, count_);
            return 0;
        }

        out_.reserve (count_);
        for (size_t i = 0; i < count_; ++i) {
            message_t part;
            if (!part.valid () || zlink_msg_move (part.handle (), &parts_[i]) != 0) {
                close_received_parts (parts_, count_);
                out_.clear ();
                errno = EFAULT;
                return -1;
            }
            out_.push_back (std::move (part));
        }

        close_received_parts (parts_, count_);
        return 0;
    }

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
    int _last_error;
    std::vector<zlink_msg_t> _publish_parts_scratch;
    handler_fn _handler_fn;
    std::mutex _handler_sync;
};

} // namespace service
} // namespace zlink

#endif
