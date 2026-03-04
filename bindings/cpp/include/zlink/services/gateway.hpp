/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_GATEWAY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_GATEWAY_HPP_INCLUDED

#include "../context.hpp"
#include "../message.hpp"
#include "../types.hpp"
#include "discovery.hpp"

#include <cerrno>

namespace zlink
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
        : _gw (zlink_gateway_new (ctx_.handle (), disc_.handle (), NULL))
    {
    }

    /**
     * @brief Create gateway with explicit routing id.
     * @param ctx_ Context wrapper.
     * @param disc_ Discovery instance.
     * @param routing_id_ Routing id string.
     */
    gateway_t (context_t &ctx_, discovery_t &disc_, const char *routing_id_)
        : _gw (zlink_gateway_new (ctx_.handle (), disc_.handle (), routing_id_))
    {
    }

    /**
     * @brief Destroy gateway handle.
     */
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

    /**
     * @brief Send multipart request to a service.
     * @param service_ Service name.
     * @param parts_ Message parts. Ownership is transferred regardless of result.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    int send (const char *service_,
              std::vector<message_t> &parts_,
              send_flag flags_ = send_flag::none)
    {
        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> tmp (parts_.size ());
        size_t moved = 0;
        if (move_parts_transfer (parts_, tmp, moved) != 0)
            return -1;

        const int rc = zlink_gateway_send (
          _gw, service_, tmp.data (), tmp.size (), static_cast<int> (flags_));
        if (rc != 0)
            close_native_parts (tmp, moved);
        return rc;
    }

    /**
     * @brief Receive multipart response.
     * @param out_ Output message frames.
     * @param service_ Output service name.
     * @param flags_ Receive flags.
     * @return 0 on success, -1 on failure.
     */
    int recv (std::vector<message_t> &out_,
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
    int send_bytes (const char *service_,
                    const void *data_,
                    size_t size_,
                    send_flag flags_ = send_flag::none)
    {
        return zlink_gateway_send_bytes (
          _gw, service_, data_, size_, static_cast<int> (flags_));
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
    int send_zero (const char *service_,
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
          _gw, service_, &part, 1, static_cast<int> (flags_));
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
     * @param routing_id_ Target routing id.
     * @param parts_ Message parts. Ownership is transferred regardless of result.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    int send_rid (const char *service_,
                  const zlink_routing_id_t &routing_id_,
                  std::vector<message_t> &parts_,
                  send_flag flags_ = send_flag::none)
    {
        if (parts_.empty ()) {
            errno = EINVAL;
            return -1;
        }

        std::vector<zlink_msg_t> tmp (parts_.size ());
        size_t moved = 0;
        if (move_parts_transfer (parts_, tmp, moved) != 0)
            return -1;

        const int rc = zlink_gateway_send_rid (
          _gw, service_, &routing_id_, tmp.data (), tmp.size (),
          static_cast<int> (flags_));
        if (rc != 0)
            close_native_parts (tmp, moved);
        return rc;
    }

    /**
     * @brief Send multipart request pinned to binary-string routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id bytes.
     * @param parts_ Message parts. Ownership is transferred regardless of result.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    int send_rid (const char *service_,
                  const std::string &routing_id_,
                  std::vector<message_t> &parts_,
                  send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        if (routing_id_from_string (routing_id_, &rid) != 0)
            return -1;
        return send_rid (service_, rid, parts_, flags_);
    }

    /**
     * @brief Send single-frame bytes pinned to a routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id.
     * @param data_ Payload pointer.
     * @param size_ Payload size in bytes.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    int send_rid_bytes (const char *service_,
                        const zlink_routing_id_t &routing_id_,
                        const void *data_,
                        size_t size_,
                        send_flag flags_ = send_flag::none)
    {
        return zlink_gateway_send_rid_bytes (
          _gw, service_, &routing_id_, data_, size_, static_cast<int> (flags_));
    }

    /**
     * @brief Send single-frame external buffer pinned to a routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id.
     * @param data_ External payload buffer. May be `NULL` only when `size_` is 0.
     * @param size_ Payload size in bytes.
     * @param ffn_ Optional release callback for `data_`.
     * @param hint_ Optional callback context pointer.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     * @note Once initialized, ownership of `data_` is consumed regardless of send result.
     */
    int send_rid_zero (const char *service_,
                       const zlink_routing_id_t &routing_id_,
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

        const int rc = zlink_gateway_send_rid (
          _gw, service_, &routing_id_, &part, 1, static_cast<int> (flags_));
        if (rc != 0) {
            const int err = errno;
            zlink_msg_close (&part);
            errno = err;
        }
        return rc;
    }

    /**
     * @brief Send single-frame external buffer pinned to binary-string routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id bytes.
     * @param data_ External payload buffer. May be `NULL` only when `size_` is 0.
     * @param size_ Payload size in bytes.
     * @param ffn_ Optional release callback for `data_`.
     * @param hint_ Optional callback context pointer.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    int send_rid_zero (const char *service_,
                       const std::string &routing_id_,
                       void *data_,
                       size_t size_,
                       zlink_free_fn *ffn_,
                       void *hint_ = NULL,
                       send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        if (routing_id_from_string (routing_id_, &rid) != 0)
            return -1;
        return send_rid_zero (service_, rid, data_, size_, ffn_, hint_, flags_);
    }

    /**
     * @brief Send single-frame bytes pinned to binary-string routing id.
     * @param service_ Service name.
     * @param routing_id_ Target routing id bytes.
     * @param data_ Payload pointer.
     * @param size_ Payload size in bytes.
     * @param flags_ Send flags.
     * @return 0 on success, -1 on failure.
     */
    int send_rid_bytes (const char *service_,
                        const std::string &routing_id_,
                        const void *data_,
                        size_t size_,
                        send_flag flags_ = send_flag::none)
    {
        zlink_routing_id_t rid;
        if (routing_id_from_string (routing_id_, &rid) != 0)
            return -1;
        return send_rid_bytes (service_, rid, data_, size_, flags_);
    }

    /**
     * @brief Set gateway socket option.
     * @param option_ Socket option key.
     * @param value_ Option value buffer.
     * @param len_ Buffer length in bytes.
     * @return 0 on success, -1 on failure.
     */
    int set_sockopt (socket_option option_, const void *value_, size_t len_)
    {
        return zlink_gateway_setsockopt (_gw, static_cast<int> (option_), value_,
                                         len_);
    }

    /**
     * @brief Set load-balancing strategy for a service.
     * @param service_ Service name.
     * @param strategy_ Balancing strategy.
     * @return 0 on success, -1 on failure.
     */
    int set_lb_strategy (const char *service_, gateway_lb_strategy strategy_)
    {
        return zlink_gateway_set_lb_strategy (
          _gw, service_, static_cast<int> (strategy_));
    }

    /**
     * @brief Configure gateway TLS client settings.
     * @param ca_ CA file path.
     * @param hostname_ Expected server hostname.
     * @param trust_ Trust-system flag.
     * @return 0 on success, -1 on failure.
     */
    int set_tls_client (const char *ca_, const char *hostname_, int trust_)
    {
        return zlink_gateway_set_tls_client (_gw, ca_, hostname_, trust_);
    }

    /**
     * @brief Configure gateway TLS client settings.
     * @param ca_ CA file path.
     * @param hostname_ Expected server hostname.
     * @param trust_ Trust-system flag.
     * @return 0 on success, -1 on failure.
     */
    int set_tls_client (const char *ca_, const char *hostname_, bool trust_)
    {
        return set_tls_client (ca_, hostname_, trust_ ? 1 : 0);
    }

    /**
     * @brief Get active connection count for a service.
     * @param service_ Service name.
     * @return Connection count, or -1 on failure.
     */
    int connection_count (const char *service_)
    {
        return zlink_gateway_connection_count (_gw, service_);
    }

    /**
     * @brief Access internal ROUTER socket handle.
     * @return Native socket handle.
     */
    void *router_handle () const
    {
        return zlink_gateway_router_socket_unsafe (_gw);
    }

    /**
     * @brief Enumerate peers on the internal ROUTER socket.
     * @param peers_ Output peer array.
     * @param count_ In/out capacity and written count.
     * @return 0 on success, -1 on failure.
     */
    int router_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_gateway_router_peers (_gw, peers_, count_);
    }

    /**
     * @brief Explicitly destroy gateway handle.
     * @return 0 on success, -1 on failure.
     */
    int destroy ()
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
                close_native_parts (native_parts_, moved_);
                for (size_t j = i; j < parts_.size (); ++j)
                    parts_[j].close ();
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
                zlink_multipart_close (parts_, count_);
            return 0;
        }

        out_.reserve (count_);
        for (size_t i = 0; i < count_; ++i) {
            message_t part;
            if (!part.valid () || zlink_msg_move (part.handle (), &parts_[i]) != 0) {
                zlink_multipart_close (parts_, count_);
                out_.clear ();
                errno = EFAULT;
                return -1;
            }
            out_.push_back (std::move (part));
        }

        zlink_multipart_close (parts_, count_);
        return 0;
    }

    void *_gw;
};

/**
 * @brief Receiver server wrapper that registers service providers.
 */
class receiver_t
{
  public:
    /**
     * @brief Create receiver with auto routing id.
     * @param ctx_ Context wrapper.
     */
    explicit receiver_t (context_t &ctx_)
        : _receiver (zlink_receiver_new (ctx_.handle (), NULL))
    {
    }

    /**
     * @brief Create receiver with explicit routing id.
     * @param ctx_ Context wrapper.
     * @param routing_id_ Routing id string.
     */
    receiver_t (context_t &ctx_, const char *routing_id_)
        : _receiver (zlink_receiver_new (ctx_.handle (), routing_id_))
    {
    }

    /**
     * @brief Destroy receiver handle.
     */
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

    /**
     * @brief Bind receiver endpoint.
     * @param endpoint_ Bind endpoint.
     * @return 0 on success, -1 on failure.
     */
    int bind (const char *endpoint_)
    {
        return zlink_receiver_bind (_receiver, endpoint_);
    }

    /**
     * @brief Connect receiver to registry ROUTER endpoint.
     * @param endpoint_ Registry endpoint.
     * @return 0 on success, -1 on failure.
     */
    int connect_registry (const char *endpoint_)
    {
        return zlink_receiver_connect_registry (_receiver, endpoint_);
    }

    /**
     * @brief Set internal receiver socket option.
     * @param role_ Internal socket role.
     * @param option_ Socket option key.
     * @param value_ Option value buffer.
     * @param len_ Buffer length.
     * @return 0 on success, -1 on failure.
     */
    int set_sockopt (receiver_socket_role role_,
                     socket_option option_,
                     const void *value_,
                     size_t len_)
    {
        return zlink_receiver_setsockopt (
          _receiver, static_cast<int> (role_), static_cast<int> (option_), value_,
          len_);
    }

    /**
     * @brief Register service endpoint with a weight.
     * @param service_ Service name.
     * @param advertise_ Advertised endpoint.
     * @param weight_ Selection weight.
     * @return 0 on success, -1 on failure.
     */
    int register_service (const char *service_,
                          const char *advertise_,
                          uint32_t weight_)
    {
        return zlink_receiver_register (_receiver, service_, advertise_, weight_);
    }

    /**
     * @brief Update service weight.
     * @param service_ Service name.
     * @param weight_ New weight.
     * @return 0 on success, -1 on failure.
     */
    int update_weight (const char *service_, uint32_t weight_)
    {
        return zlink_receiver_update_weight (_receiver, service_, weight_);
    }

    /**
     * @brief Unregister a service.
     * @param service_ Service name.
     * @return 0 on success, -1 on failure.
     */
    int unregister_service (const char *service_)
    {
        return zlink_receiver_unregister (_receiver, service_);
    }

    /**
     * @brief Query latest register result details.
     * @param service_ Service name.
     * @param status_ Output status code.
     * @param resolved_ Output resolved endpoint buffer.
     * @param err_ Output error text buffer.
     * @return 0 on success, -1 on failure.
     */
    int register_result (const char *service_,
                         int *status_,
                         char *resolved_,
                         char *err_)
    {
        return zlink_receiver_register_result (
          _receiver, service_, status_, resolved_, err_);
    }

    /**
     * @brief Configure receiver TLS server certificate.
     * @param cert_ Certificate file path.
     * @param key_ Private key file path.
     * @return 0 on success, -1 on failure.
     */
    int set_tls_server (const char *cert_, const char *key_)
    {
        return zlink_receiver_set_tls_server (_receiver, cert_, key_);
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
    int router_peers (zlink_peer_info_t *peers_, size_t *count_)
    {
        return zlink_receiver_router_peers (_receiver, peers_, count_);
    }

    /**
     * @brief Explicitly destroy receiver handle.
     * @return 0 on success, -1 on failure.
     */
    int destroy ()
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
};

} // namespace zlink

#endif
