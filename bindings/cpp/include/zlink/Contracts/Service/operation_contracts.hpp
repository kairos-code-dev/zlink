/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#define CPP_BINDING_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "spot_node.hpp"
#undef CPP_BINDING_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "../Messaging/operation_contracts.hpp"
#include <memory>

namespace zlink
{
namespace service
{

namespace detail
{
struct actor_join_state_t;
struct actor_join_reply_state_t;
struct actor_payloadless_state_t;
struct actor_bind_state_t;
} // namespace detail

// --- Actor operation builders ---

class actor_join_callback_ready_op_t;
class actor_join_ready_op_t;
class actor_bind_op_t;
class actor_unbind_op_t;

actor_bind_op_t detail_make_actor_bind_op (detail::actor_bind_state_t &&state_);
actor_unbind_op_t detail_make_actor_unbind_op (
  detail::actor_bind_state_t &&state_);

class actor_join_op_t
{
  public:
    ~actor_join_op_t ();
    actor_join_op_t (actor_join_op_t &&) noexcept;
    actor_join_op_t &operator= (actor_join_op_t &&) noexcept;

    actor_join_ready_op_t message (message_t &part_) &&;

  private:
    explicit actor_join_op_t (detail::actor_join_state_t &&state_);

    detail::actor_join_state_t &state () noexcept;
    const detail::actor_join_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_join_state_t> _state;
    friend class spot_node_t;
    friend class actor_t;
};

class actor_join_ready_op_t
{
  public:
    ~actor_join_ready_op_t ();
    actor_join_ready_op_t (actor_join_ready_op_t &&) noexcept;
    actor_join_ready_op_t &operator= (actor_join_ready_op_t &&) noexcept;

    actor_join_ready_op_t &&message (message_t &part_) &&;
    actor_join_ready_op_t &&timeout (std::chrono::milliseconds timeout_) &&;
    actor_join_callback_ready_op_t flags (int flags_) &&;
    async_result_t<actor_join_result_t> submit_async () &&;
    bool submit (actor_join_callback_t callback_) &&;

  private:
    explicit actor_join_ready_op_t (detail::actor_join_state_t &&state_);

    detail::actor_join_state_t &state () noexcept;
    const detail::actor_join_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_join_state_t> _state;
    friend class actor_join_op_t;
    friend class actor_join_callback_ready_op_t;
};

class actor_join_callback_ready_op_t
{
  public:
    ~actor_join_callback_ready_op_t ();
    actor_join_callback_ready_op_t (actor_join_callback_ready_op_t &&) noexcept;
    actor_join_callback_ready_op_t &
    operator= (actor_join_callback_ready_op_t &&) noexcept;

    actor_join_callback_ready_op_t &&message (message_t &part_) &&;
    actor_join_callback_ready_op_t &&
    timeout (std::chrono::milliseconds timeout_) &&;
    actor_join_callback_ready_op_t &&flags (int flags_) &&;
    bool submit (actor_join_callback_t callback_) &&;

  private:
    explicit actor_join_callback_ready_op_t (
      detail::actor_join_state_t &&state_);

    detail::actor_join_state_t &state () noexcept;
    const detail::actor_join_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_join_state_t> _state;
    friend class actor_join_ready_op_t;
};

class actor_join_entry_spot_op_t
{
  public:
    ~actor_join_entry_spot_op_t ();
    actor_join_entry_spot_op_t (actor_join_entry_spot_op_t &&) noexcept;
    actor_join_entry_spot_op_t &
    operator= (actor_join_entry_spot_op_t &&) noexcept;

    actor_join_entry_spot_op_t &&
    timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<actor_join_entry_spot_result_t> submit_async () &&;
    bool submit (actor_join_entry_spot_callback_t callback_) &&;

  private:
    explicit actor_join_entry_spot_op_t (
      detail::actor_payloadless_state_t &&state_);

    detail::actor_payloadless_state_t &state () noexcept;
    const detail::actor_payloadless_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_payloadless_state_t> _state;
    friend class spot_node_t;
};

class actor_join_reply_op_t
{
  public:
    ~actor_join_reply_op_t ();
    actor_join_reply_op_t (actor_join_reply_op_t &&) noexcept;
    actor_join_reply_op_t &operator= (actor_join_reply_op_t &&) noexcept;

    actor_join_reply_op_t &&message (message_t &part_) &&;
    void submit () &&;

  private:
    explicit actor_join_reply_op_t (detail::actor_join_reply_state_t &&state_);

    detail::actor_join_reply_state_t &state () noexcept;
    const detail::actor_join_reply_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_join_reply_state_t> _state;
    friend class spot_t;
};

class actor_leave_op_t
{
  public:
    ~actor_leave_op_t ();
    actor_leave_op_t (actor_leave_op_t &&) noexcept;
    actor_leave_op_t &operator= (actor_leave_op_t &&) noexcept;

    actor_leave_op_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> submit_async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    explicit actor_leave_op_t (detail::actor_payloadless_state_t &&state_);

    detail::actor_payloadless_state_t &state () noexcept;
    const detail::actor_payloadless_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_payloadless_state_t> _state;
    friend class spot_node_t;
    friend class actor_t;
};

class actor_destroy_op_t
{
  public:
    ~actor_destroy_op_t ();
    actor_destroy_op_t (actor_destroy_op_t &&) noexcept;
    actor_destroy_op_t &operator= (actor_destroy_op_t &&) noexcept;

    actor_destroy_op_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> submit_async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    explicit actor_destroy_op_t (detail::actor_payloadless_state_t &&state_);

    detail::actor_payloadless_state_t &state () noexcept;
    const detail::actor_payloadless_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_payloadless_state_t> _state;
    friend class spot_node_t;
};

class actor_lookup_op_t
{
  public:
    ~actor_lookup_op_t ();
    actor_lookup_op_t (actor_lookup_op_t &&) noexcept;
    actor_lookup_op_t &operator= (actor_lookup_op_t &&) noexcept;

    actor_lookup_op_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<actor_lookup_result_t> submit_async () &&;
    bool submit (actor_lookup_callback_t callback_) &&;

  private:
    explicit actor_lookup_op_t (detail::actor_payloadless_state_t &&state_);

    detail::actor_payloadless_state_t &state () noexcept;
    const detail::actor_payloadless_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_payloadless_state_t> _state;
    friend class spot_node_t;
};

class actor_bind_op_t
{
  public:
    ~actor_bind_op_t ();
    actor_bind_op_t (actor_bind_op_t &&) noexcept;
    actor_bind_op_t &operator= (actor_bind_op_t &&) noexcept;

    actor_bind_op_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> submit_async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    explicit actor_bind_op_t (detail::actor_bind_state_t &&state_);

    detail::actor_bind_state_t &state () noexcept;
    const detail::actor_bind_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_bind_state_t> _state;
    friend actor_bind_op_t
    detail_make_actor_bind_op (detail::actor_bind_state_t &&state_);
};

class actor_unbind_op_t
{
  public:
    ~actor_unbind_op_t ();
    actor_unbind_op_t (actor_unbind_op_t &&) noexcept;
    actor_unbind_op_t &operator= (actor_unbind_op_t &&) noexcept;

    actor_unbind_op_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> submit_async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    explicit actor_unbind_op_t (detail::actor_bind_state_t &&state_);

    detail::actor_bind_state_t &state () noexcept;
    const detail::actor_bind_state_t &state () const noexcept;

    std::unique_ptr<detail::actor_bind_state_t> _state;
    friend actor_unbind_op_t
    detail_make_actor_unbind_op (detail::actor_bind_state_t &&state_);
};

} // namespace service
} // namespace zlink
