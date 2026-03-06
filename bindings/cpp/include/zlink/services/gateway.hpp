/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_GATEWAY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_GATEWAY_HPP_INCLUDED

#include "../context.hpp"
#include "../message.hpp"
#include "../types.hpp"
#include "discovery.hpp"

#include <cerrno>
#include <cstdlib>
#include <type_traits>

namespace zlink
{
namespace service
{

/**
 * @brief Gateway client wrapper for request/reply service traffic.
 */
class gateway_t
{
  public:
    /**
     * @brief Create gateway with auto-generated routing id.
     * @param ctx_ Context wrapper.
     * @param disc_ Discovery instance.
     */
    gateway_t (context_t &ctx_, discovery_t &disc_)
        : _gw (zlink_gateway_new (ctx_.handle (), disc_.handle (), NULL)),
          _last_error (0)
    {
        if (!_gw)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    /**
     * @brief Create gateway with explicit routing id.
     * @param ctx_ Context wrapper.
     * @param disc_ Discovery instance.
     * @param routing_id_ Routing id string.
     */
    gateway_t (context_t &ctx_,
               discovery_t &disc_,
               const std::string &routing_id_)
        : _gw (zlink_gateway_new (
            ctx_.handle (), disc_.handle (), routing_id_.c_str ())),
          _last_error (0)
    {
        if (!_gw)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    /**
     * @brief Destroy gateway handle.
     */
    ~gateway_t () { (void) destroy (); }

    gateway_t (gateway_t &&other) noexcept
        : _gw (other._gw), _last_error (other._last_error)
    {
        other._gw = NULL;
        other._last_error = 0;
    }

    gateway_t &operator= (gateway_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) destroy ();
        _gw = other._gw;
        _last_error = other._last_error;
        other._gw = NULL;
        other._last_error = 0;
        return *this;
    }

    gateway_t (const gateway_t &) = delete;
    gateway_t &operator= (const gateway_t &) = delete;

    /**
     * @brief Check whether gateway handle was created.
     * @return `true` when handle is valid.
     */
    bool valid () const noexcept { return _gw != NULL; }

    /**
     * @brief Return constructor-time initialization error.
     * @return Error number, or 0 when initialized successfully.
     */
    int last_error () const noexcept { return _last_error; }

    /**
     * @brief Send multipart request to a service.
     * @param service_ Service name.
     * @param parts_ Message parts. Ownership is transferred regardless of result.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    send (const std::string &service_,
          std::vector<message_t> &parts_,
          send_flag flags_ = send_flag::none)
    {
        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        _send_parts_scratch.resize (parts_.size ());
        size_t moved = 0;
        if (move_parts_transfer (parts_, _send_parts_scratch, moved) != 0) {
            _send_parts_scratch.clear ();
            return -1;
        }

        const int rc = zlink_gateway_send (
          _gw, service_.c_str (), _send_parts_scratch.data (),
          _send_parts_scratch.size (),
          static_cast<int> (flags_));
        if (rc != 0) {
            const int err = errno;
            close_native_parts (_send_parts_scratch, moved);
            errno = err;
        }
        _send_parts_scratch.clear ();
        return rc;
    }

    /**
     * @brief Receive multipart response.
     * @param out_ Output message frames.
     * @param service_ Output service name.
     * @param flags_ Receive flags.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    recv (std::vector<message_t> &out_,
          std::string &service_,
          recv_flag flags_ = recv_flag::none)
    {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char name[256];
        const int rc = zlink_gateway_recv (
          _gw, &parts, &count, static_cast<int> (flags_), name);
        if (rc != 0)
            return rc;

        service_.assign (name);
        return move_received_parts (out_, parts, count);
    }

    /**
     * @brief Send single-frame bytes to a service.
     * @param service_ Service name.
     * @param data_ Payload pointer.
     * @param size_ Payload size in bytes.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    send (const std::string &service_,
          const void *data_,
          size_t size_,
          send_flag flags_ = send_flag::none)
    {
        return zlink_gateway_send_bytes (
          _gw, service_.c_str (), data_, size_, static_cast<int> (flags_));
    }

    /**
     * @brief Send a single-frame external buffer without a pre-send copy.
     * @param service_ Service name.
     * @param data_ External payload buffer. May be `NULL` only when `size_` is 0.
     * @param size_ Payload size in bytes.
     * @param ffn_ Optional release callback for `data_`.
     * @param hint_ Optional callback context pointer.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     * @note Once initialized, ownership of `data_` is consumed regardless of send result.
     */
    ZLINK_CPP_NODISCARD int
    send_zero (const std::string &service_,
               void *data_,
               size_t size_,
               zlink_free_fn *ffn_,
               void *hint_ = NULL,
               send_flag flags_ = send_flag::none)
    {
        if (size_ > 0 && !data_) {
            errno = EINVAL;
            return -1;
        }

        zlink_msg_t part;
        if (zlink_msg_init_data (&part, data_, size_, ffn_, hint_) != 0)
            return -1;

        const int rc = zlink_gateway_send (
          _gw, service_.c_str (), &part, 1, static_cast<int> (flags_));
        if (rc != 0) {
            const int err = errno;
            zlink_msg_close (&part);
            errno = err;
        }
        return rc;
    }

    /**
     * @brief Send multipart request pinned to a routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id bytes.
     * @param parts_ Message parts. Ownership is transferred regardless of result.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    send_rid (const std::string &service_,
              const std::string &routing_id_,
              std::vector<message_t> &parts_,
              send_flag flags_ = send_flag::none)
    {
        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        zlink_routing_id_t rid;
        if (routing_id_from (routing_id_, &rid) != 0)
            return -1;

        _send_parts_scratch.resize (parts_.size ());
        size_t moved = 0;
        if (move_parts_transfer (parts_, _send_parts_scratch, moved) != 0) {
            _send_parts_scratch.clear ();
            return -1;
        }

        const int rc = zlink_gateway_send_rid (
          _gw, service_.c_str (), &rid, _send_parts_scratch.data (),
          _send_parts_scratch.size (),
          static_cast<int> (flags_));
        if (rc != 0) {
            const int err = errno;
            close_native_parts (_send_parts_scratch, moved);
            errno = err;
        }
        _send_parts_scratch.clear ();
        return rc;
    }

    /**
     * @brief Send single-frame bytes pinned to a routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id bytes.
     * @param data_ Payload pointer.
     * @param size_ Payload size in bytes.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    send_rid (const std::string &service_,
              const std::string &routing_id_,
              const void *data_,
              size_t size_,
              send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        if (routing_id_from (routing_id_, &rid) != 0)
            return -1;
        return zlink_gateway_send_rid_bytes (
          _gw, service_.c_str (), &rid, data_, size_, static_cast<int> (flags_));
    }

    /**
     * @brief Send single-frame external buffer pinned to a routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id bytes.
     * @param data_ External payload buffer. May be `NULL` only when `size_` is 0.
     * @param size_ Payload size in bytes.
     * @param ffn_ Optional release callback for `data_`.
     * @param hint_ Optional callback context pointer.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     * @note Once initialized, ownership of `data_` is consumed regardless of send result.
     */
    ZLINK_CPP_NODISCARD int
    send_rid_zero (const std::string &service_,
                   const std::string &routing_id_,
                   void *data_,
                   size_t size_,
                   zlink_free_fn *ffn_,
                   void *hint_ = NULL,
                   send_flag flags_ = send_flag::none)
    {
        if (size_ > 0 && !data_) {
            errno = EINVAL;
            return -1;
        }

        zlink_routing_id_t rid;
        if (routing_id_from (routing_id_, &rid) != 0)
            return -1;

        zlink_msg_t part;
        if (zlink_msg_init_data (&part, data_, size_, ffn_, hint_) != 0)
            return -1;

        const int rc = zlink_gateway_send_rid (
          _gw, service_.c_str (), &rid, &part, 1, static_cast<int> (flags_));
        if (rc != 0) {
            const int err = errno;
            zlink_msg_close (&part);
            errno = err;
        }
        return rc;
    }

    /**
     * @brief Set gateway socket option.
     * @param option_ Socket option key.
     * @param value_ Option value buffer.
     * @param len_ Buffer length in bytes.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (socket_option option_, const void *value_, size_t len_)
    {
        return zlink_gateway_setsockopt (_gw, static_cast<int> (option_), value_,
                                         len_);
    }

    /**
     * @brief Set typed non-string gateway socket option.
     * @param key_ Typed socket option key.
     * @param value_ Typed option value.
     * @return 0 on success, -1 on failure.
     */
    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_sockopt (socket_option_key_t<T> key_, const T &value_)
    {
        return set_sockopt (key_.option, &value_, sizeof (value_));
    }

    /**
     * @brief Set typed string gateway socket option.
     * @param key_ Typed socket option key.
     * @param value_ Option value bytes.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (socket_option_key_t<std::string> key_,
                 const std::string &value_)
    {
        return set_sockopt (key_.option, value_.data (), value_.size ());
    }

    /**
     * @brief Set load-balancing strategy for a service.
     * @param service_ Service name.
     * @param strategy_ Balancing strategy.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_lb_strategy (const std::string &service_,
                     gateway_lb_strategy strategy_)
    {
        return zlink_gateway_set_lb_strategy (
          _gw, service_.c_str (), static_cast<int> (strategy_));
    }

    /**
     * @brief Configure gateway TLS client settings.
     * @param ca_ CA file path.
     * @param hostname_ Expected server hostname.
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
        return zlink_gateway_set_tls_client (
          _gw, ca, hostname, trust_);
    }

    /**
     * @brief Configure gateway TLS client settings.
     * @param ca_ CA file path.
     * @param hostname_ Expected server hostname.
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
     * @brief Get active connection count for a service.
     * @param service_ Service name.
     * @return Connection count, or -1 on failure.
     */
    ZLINK_CPP_NODISCARD int connection_count (const std::string &service_)
    {
        return zlink_gateway_connection_count (_gw, service_.c_str ());
    }

    /**
     * @brief Access internal ROUTER socket handle.
     * @return Native socket handle.
     */
    void *router_handle () const
    {
        return zlink_gateway_router_socket (_gw);
    }

    /**
     * @brief Enumerate peers on the internal ROUTER socket.
     * @param peers_ Output peer array.
     * @param count_ In/out capacity and written count.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    router_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_gateway_router_peers (_gw, peers_, count_);
    }

    /**
     * @brief Explicitly destroy gateway handle.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int destroy ()
    {
        if (!_gw)
            return 0;

        void *tmp = _gw;
        _gw = NULL;
        return zlink_gateway_destroy (&tmp);
    }

    /**
     * @brief Access raw native gateway handle.
     * @return Native handle pointer.
     */
    void *handle () const { return _gw; }

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

    void *_gw;
    int _last_error;
    std::vector<zlink_msg_t> _send_parts_scratch;
};

/**
 * @brief Receiver server wrapper that registers service providers.
 */
class receiver_t
{
  public:
    /**
     * @brief Registration result snapshot.
     */
    struct register_result_t
    {
        int status;
        std::string resolved_endpoint;
        std::string error_message;
        size_t resolved_endpoint_length;
        size_t error_message_length;
        bool resolved_endpoint_possibly_truncated;
        bool error_message_possibly_truncated;
    };

    /**
     * @brief Create receiver with auto routing id.
     * @param ctx_ Context wrapper.
     */
    explicit receiver_t (context_t &ctx_)
        : _receiver (zlink_receiver_new (ctx_.handle (), NULL)), _last_error (0)
    {
        if (!_receiver)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    /**
     * @brief Create receiver with explicit routing id.
     * @param ctx_ Context wrapper.
     * @param routing_id_ Routing id string.
     */
    receiver_t (context_t &ctx_, const std::string &routing_id_)
        : _receiver (zlink_receiver_new (ctx_.handle (), routing_id_.c_str ())),
          _last_error (0)
    {
        if (!_receiver)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    /**
     * @brief Destroy receiver handle.
     */
    ~receiver_t () { (void) destroy (); }

    receiver_t (receiver_t &&other) noexcept
        : _receiver (other._receiver), _last_error (other._last_error)
    {
        other._receiver = NULL;
        other._last_error = 0;
    }

    receiver_t &operator= (receiver_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) destroy ();
        _receiver = other._receiver;
        _last_error = other._last_error;
        other._receiver = NULL;
        other._last_error = 0;
        return *this;
    }

    receiver_t (const receiver_t &) = delete;
    receiver_t &operator= (const receiver_t &) = delete;

    /**
     * @brief Check whether receiver handle was created.
     * @return `true` when handle is valid.
     */
    bool valid () const noexcept { return _receiver != NULL; }

    /**
     * @brief Return constructor-time initialization error.
     * @return Error number, or 0 when initialized successfully.
     */
    int last_error () const noexcept { return _last_error; }

    /**
     * @brief Bind receiver endpoint.
     * @param endpoint_ Bind endpoint.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int bind (const std::string &endpoint_)
    {
        return zlink_receiver_bind (_receiver, endpoint_.c_str ());
    }

    /**
     * @brief Connect receiver to registry ROUTER endpoint.
     * @param endpoint_ Registry endpoint.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int connect_registry (const std::string &endpoint_)
    {
        return zlink_receiver_connect_registry (_receiver, endpoint_.c_str ());
    }

    /**
     * @brief Set internal receiver socket option.
     * @param role_ Internal socket role.
     * @param option_ Socket option key.
     * @param value_ Option value buffer.
     * @param len_ Buffer length.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (receiver_socket_role role_,
                 socket_option option_,
                 const void *value_,
                 size_t len_)
    {
        return zlink_receiver_setsockopt (
          _receiver, static_cast<int> (role_), static_cast<int> (option_), value_,
          len_);
    }

    /**
     * @brief Set typed non-string socket option on internal receiver socket.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Typed option value.
     * @return 0 on success, -1 on failure.
     */
    template<typename T>
    ZLINK_CPP_NODISCARD
    typename std::enable_if<!std::is_same<T, std::string>::value, int>::type
    set_sockopt (receiver_socket_role role_,
                 socket_option_key_t<T> key_,
                 const T &value_)
    {
        return set_sockopt (role_, key_.option, &value_, sizeof (value_));
    }

    /**
     * @brief Set typed string socket option on internal receiver socket.
     * @param role_ Internal socket role.
     * @param key_ Typed socket option key.
     * @param value_ Option value bytes.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_sockopt (receiver_socket_role role_,
                 socket_option_key_t<std::string> key_,
                 const std::string &value_)
    {
        return set_sockopt (role_, key_.option, value_.data (), value_.size ());
    }

    /**
     * @brief Register service endpoint with a weight.
     * @param service_ Service name.
     * @param advertise_ Advertised endpoint.
     * @param weight_ Selection weight.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    register_service (const std::string &service_,
                      const std::string &advertise_,
                      uint32_t weight_)
    {
        return zlink_receiver_register (
          _receiver, service_.c_str (), advertise_.c_str (), weight_);
    }

    /**
     * @brief Update service weight.
     * @param service_ Service name.
     * @param weight_ New weight.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    update_weight (const std::string &service_, uint32_t weight_)
    {
        return zlink_receiver_update_weight (_receiver, service_.c_str (), weight_);
    }

    /**
     * @brief Unregister a service.
     * @param service_ Service name.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int unregister_service (const std::string &service_)
    {
        return zlink_receiver_unregister (_receiver, service_.c_str ());
    }

    /**
     * @brief Query latest register result details.
     * @param service_ Service name.
     * @param out_ Output result snapshot.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    register_result (const std::string &service_, register_result_t *out_)
    {
        if (!out_) {
            errno = EINVAL;
            return -1;
        }

        char resolved[256];
        char err[256];
        int status = 0;
        std::memset (resolved, 0, sizeof (resolved));
        std::memset (err, 0, sizeof (err));

        const int rc = zlink_receiver_register_result (
          _receiver, service_.c_str (), &status, resolved, err);
        if (rc != 0)
            return rc;

        const size_t resolved_len = std::strlen (resolved);
        const size_t err_len = std::strlen (err);
        out_->status = status;
        out_->resolved_endpoint.assign (resolved);
        out_->error_message.assign (err);
        out_->resolved_endpoint_length = resolved_len;
        out_->error_message_length = err_len;
        out_->resolved_endpoint_possibly_truncated =
          resolved_len == (sizeof (resolved) - 1);
        out_->error_message_possibly_truncated =
          err_len == (sizeof (err) - 1);
        return 0;
    }

    /**
     * @brief Configure receiver TLS server certificate.
     * @param cert_ Certificate file path.
     * @param key_ Private key file path.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    set_tls_server (const std::string &cert_, const std::string &key_)
    {
        return zlink_receiver_set_tls_server (
          _receiver, cert_.c_str (), key_.c_str ());
    }

    /**
     * @brief Access internal ROUTER socket handle.
     * @return Native socket handle.
     */
    void *router_handle () const
    {
        return zlink_receiver_router_socket_unsafe (_receiver);
    }

    /**
     * @brief Enumerate peers on internal ROUTER socket.
     * @param peers_ Output peer array.
     * @param count_ In/out capacity and written count.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int
    router_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_receiver_router_peers (_receiver, peers_, count_);
    }

    /**
     * @brief Explicitly destroy receiver handle.
     * @return 0 on success, -1 on failure.
     */
    ZLINK_CPP_NODISCARD int destroy ()
    {
        if (!_receiver)
            return 0;

        void *tmp = _receiver;
        _receiver = NULL;
        return zlink_receiver_destroy (&tmp);
    }

    /**
     * @brief Access raw native receiver handle.
     * @return Native handle pointer.
     */
    void *handle () const { return _receiver; }

  private:
    void *_receiver;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
