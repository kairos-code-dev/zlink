/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Sockets/results.hpp"
#include "message.hpp"

namespace zlink
{

class received_t
{
  public:
    received_t () = default;

    received_t (const received_t &other)
        : _routing_id (other._routing_id),
          _spot_rid (other._spot_rid),
          _request_seq (other._request_seq),
          _single_part (other._single_part),
          _parts (other._parts),
          _runtime (clone_runtime_state (other._runtime))
    {
    }

    received_t &operator= (const received_t &other)
    {
        if (this == &other)
            return *this;

        _routing_id = other._routing_id;
        _spot_rid = other._spot_rid;
        _request_seq = other._request_seq;
        _single_part = other._single_part;
        _parts = other._parts;
        _runtime = clone_runtime_state (other._runtime);
        return *this;
    }

    received_t (received_t &&) noexcept = default;
    received_t &operator= (received_t &&) noexcept = default;

    received_t (std::optional<routing_id_t> routing_id_,
                std::optional<routing_id_t> spot_rid_,
                std::optional<uint64_t> request_seq_,
                std::vector<message_t> parts_,
                std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn_ =
                  std::function<void(std::vector<message_t> &, send_flags_t)> (),
                std::function<bool(std::vector<message_t> &, send_flags_t)> send_fn_ =
                  std::function<bool(std::vector<message_t> &, send_flags_t)> ())
        : _routing_id (std::move (routing_id_)),
          _spot_rid (std::move (spot_rid_)),
          _request_seq (std::move (request_seq_)),
          _single_part (),
          _parts (std::move (parts_)),
          _runtime ()
    {
        if (reply_fn_ || send_fn_) {
            ensure_runtime_state ();
            _runtime->_reply_fn = std::move (reply_fn_);
            _runtime->_send_fn = std::move (send_fn_);
        }
    }

    received_t (std::optional<routing_id_t> routing_id_,
                std::optional<routing_id_t> spot_rid_,
                std::optional<uint64_t> request_seq_,
                message_t part_,
                std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn_ =
                  std::function<void(std::vector<message_t> &, send_flags_t)> (),
                std::function<bool(std::vector<message_t> &, send_flags_t)> send_fn_ =
                  std::function<bool(std::vector<message_t> &, send_flags_t)> ())
        : _routing_id (std::move (routing_id_)),
          _spot_rid (std::move (spot_rid_)),
          _request_seq (std::move (request_seq_)),
          _single_part (std::move (part_)),
          _parts (),
          _runtime ()
    {
        if (reply_fn_ || send_fn_) {
            ensure_runtime_state ();
            _runtime->_reply_fn = std::move (reply_fn_);
            _runtime->_send_fn = std::move (send_fn_);
        }
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return _routing_id;
    }

    const std::optional<routing_id_t> &spot_rid () const noexcept
    {
        return _spot_rid;
    }

    const std::optional<uint64_t> &request_seq () const noexcept
    {
        return _request_seq;
    }

    const std::vector<message_t> &parts () const;
    std::vector<message_t> &parts ();

    bool is_single_part () const noexcept
    {
        return _single_part.has_value () || _parts.size () == 1u;
    }
    message_t &first_part ();
    message_t single_part_or_throw ();
    /// Send context (routing_id / spot_rid) is encapsulated. Returns an
    /// operation builder; accumulate payload via `.message(...)`.
    service::send_operation_t send ();
    /// Reply context (routing_id, spot_rid, request_seq) is encapsulated.
    /// Returns an operation builder; accumulate reply payload via
    /// `.message(...)`. Submit throws if there is no valid reply context.
    service::reply_operation_t reply ();
    void close ();

  private:
    friend class socket_t;
    friend class router_socket_t;
    friend class service::send_operation_t;
    friend class service::send_submit_operation_t;
    friend class service::reply_operation_t;
    friend class service::reply_submit_operation_t;

    enum class send_context_kind_t
    {
        none,
        socket_rid,
        router_spot
    };

    void materialize_parts () const;
    void set_send_context (std::uintptr_t handle_, send_context_kind_t kind_)
    {
        ensure_runtime_state ();
        _runtime->_send_context_handle = handle_;
        _runtime->_send_context_kind = kind_;
    }

    void set_reply_fn (
      std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn_)
    {
        ensure_runtime_state ();
        _runtime->_reply_fn = std::move (reply_fn_);
    }

    void set_send_fn (
      std::function<bool(std::vector<message_t> &, send_flags_t)> send_fn_)
    {
        ensure_runtime_state ();
        _runtime->_send_fn = std::move (send_fn_);
    }

    bool has_reply_fn () const noexcept
    {
        return _runtime && static_cast<bool> (_runtime->_reply_fn);
    }

    bool has_send_fn () const noexcept
    {
        return _runtime && static_cast<bool> (_runtime->_send_fn);
    }

    void invoke_reply_fn (std::vector<message_t> &parts_, send_flags_t flags_)
    {
        _runtime->_reply_fn (parts_, flags_);
    }

    bool invoke_send_fn (std::vector<message_t> &parts_, send_flags_t flags_)
    {
        return _runtime->_send_fn (parts_, flags_);
    }

    struct runtime_state_t
    {
        std::function<void(std::vector<message_t> &, send_flags_t)> _reply_fn;
        std::function<bool(std::vector<message_t> &, send_flags_t)> _send_fn;
        std::uintptr_t _send_context_handle = 0;
        send_context_kind_t _send_context_kind = send_context_kind_t::none;
    };

    static std::unique_ptr<runtime_state_t> clone_runtime_state (
      const std::unique_ptr<runtime_state_t> &state_)
    {
        if (!state_)
            return std::unique_ptr<runtime_state_t> ();
        return std::unique_ptr<runtime_state_t> (new runtime_state_t (*state_));
    }

    void ensure_runtime_state ()
    {
        if (!_runtime)
            _runtime.reset (new runtime_state_t ());
    }

    std::optional<routing_id_t> _routing_id;
    std::optional<routing_id_t> _spot_rid;
    std::optional<uint64_t> _request_seq;
    mutable std::optional<message_t> _single_part;
    mutable std::vector<message_t> _parts;
    std::unique_ptr<runtime_state_t> _runtime;
};

} // namespace zlink
