/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_REQUEST_REPLY_HPP_INCLUDED
#define ZLINK_CPP_REQUEST_REPLY_HPP_INCLUDED

#include "services/spot_state.hpp"

namespace zlink
{
namespace service
{

class send_ready_op_t
{
  public:
    send_ready_op_t (send_ready_op_t &&) noexcept = default;
    send_ready_op_t &operator= (send_ready_op_t &&) noexcept = default;

    send_ready_op_t &&message (message_t &part_) &&
    {
        detail::append_send_part (_state, part_);
        return std::move (*this);
    }

    send_ready_op_t &&flags (int flags_) &&
    {
        _state.flags = send_flags_t (flags_);
        return std::move (*this);
    }

    bool submit () &&;

  private:
    explicit send_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class send_op_t;
};

class send_op_t
{
  public:
    send_op_t (send_op_t &&) noexcept = default;
    send_op_t &operator= (send_op_t &&) noexcept = default;

    send_ready_op_t message (message_t &part_) &&
    {
        _state.single_part.emplace (std::move (part_));
        return send_ready_op_t (std::move (_state));
    }

  private:
    explicit send_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class zlink::pair_socket_t;
    friend class zlink::dealer_socket_t;
    friend class zlink::router_socket_t;
    friend class zlink::stream_socket_t;
    friend class zlink::pub_socket_t;
    friend class zlink::xpub_socket_t;
    friend class spot_t;
    friend class spot_node_t;
    friend class zlink::received_t;
    friend class zlink::stream_socket_t;
};

class request_callback_ready_op_t;

class request_ready_op_t
{
  public:
    request_ready_op_t (request_ready_op_t &&) noexcept = default;
    request_ready_op_t &operator= (request_ready_op_t &&) noexcept = default;

    request_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    request_ready_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    request_callback_ready_op_t flags (int flags_) &&;
    async_result_t<std::vector<message_t>> submit_async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    explicit request_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class request_op_t;
    friend class request_callback_ready_op_t;
};

class request_op_t
{
  public:
    request_op_t (request_op_t &&) noexcept = default;
    request_op_t &operator= (request_op_t &&) noexcept = default;

    request_ready_op_t message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return request_ready_op_t (std::move (_state));
    }

  private:
    explicit request_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class spot_t;
    friend class zlink::dealer_socket_t;
    friend class zlink::router_socket_t;
};

class request_callback_ready_op_t
{
  public:
    request_callback_ready_op_t (request_callback_ready_op_t &&) noexcept =
      default;
    request_callback_ready_op_t &
    operator= (request_callback_ready_op_t &&) noexcept = default;

    request_callback_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    request_callback_ready_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    request_callback_ready_op_t &&flags (int flags_) &&
    {
        _state.flags = send_flags_t (flags_);
        return std::move (*this);
    }

    bool submit (request_callback_t callback_) &&;

  private:
    explicit request_callback_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class request_ready_op_t;
};

class reply_ready_op_t
{
  public:
    reply_ready_op_t (reply_ready_op_t &&) noexcept = default;
    reply_ready_op_t &operator= (reply_ready_op_t &&) noexcept = default;

    reply_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    reply_ready_op_t &&flags (int flags_) &&
    {
        _state.flags = send_flags_t (flags_);
        return std::move (*this);
    }

    void submit () &&;

  private:
    explicit reply_ready_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class reply_op_t;
};

class reply_op_t
{
  public:
    reply_op_t (reply_op_t &&) noexcept = default;
    reply_op_t &operator= (reply_op_t &&) noexcept = default;

    reply_ready_op_t message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return reply_ready_op_t (std::move (_state));
    }

  private:
    explicit reply_op_t (detail::spot_op_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::spot_op_state_t _state;
    friend class spot_t;
    friend class spot_node_t;
    friend class zlink::received_t;
    friend class zlink::router_socket_t;
};



} // namespace service
} // namespace zlink

#endif
