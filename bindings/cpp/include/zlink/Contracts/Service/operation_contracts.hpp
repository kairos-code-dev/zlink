/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Messaging/message.hpp"
#include "../Messaging/operation_contracts.hpp"
#include "actor_models.hpp"
#include "operation_builder_base.hpp"
#include <memory>

namespace zlink
{
namespace service
{

class actor_t;
class spot_node_t;
class spot_t;

namespace detail
{
struct actor_join_state_t;
struct actor_join_reply_state_t;
struct actor_payloadless_state_t;
struct actor_bind_state_t;
} // namespace detail

// --- Actor operation builders ---

class actor_join_callback_submit_operation_t;
class actor_join_submit_operation_t;
class actor_bind_operation_t;
class actor_unbind_operation_t;

actor_bind_operation_t detail_make_actor_bind_operation (detail::actor_bind_state_t &&state_);
actor_unbind_operation_t detail_make_actor_unbind_operation (detail::actor_bind_state_t &&state_);

/// @brief Builds an actor join: add parts, then submit and await the result.
class actor_join_operation_t : private detail::operation_builder_base_t<
                                 detail::actor_join_state_t,
                                 detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_join_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_join_operation_t ();
    actor_join_operation_t (actor_join_operation_t &&) noexcept;
    actor_join_operation_t &operator= (actor_join_operation_t &&) noexcept;

    actor_join_submit_operation_t message (message_t &part_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class spot_node_t;
    friend class actor_t;
};

/// @brief Accepts further parts, timeout, flags, and the terminal submit of an actor join.
class actor_join_submit_operation_t : private detail::operation_builder_base_t<
                                        detail::actor_join_state_t,
                                        detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_join_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_join_submit_operation_t ();
    actor_join_submit_operation_t (actor_join_submit_operation_t &&) noexcept;
    actor_join_submit_operation_t &operator= (actor_join_submit_operation_t &&) noexcept;

    actor_join_submit_operation_t &&message (message_t &part_) &&;
    actor_join_submit_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    actor_join_callback_submit_operation_t flags (int flags_) &&;
    async_result_t<actor_join_result_t> async () &&;
    bool submit (actor_join_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class actor_join_operation_t;
    friend class actor_join_callback_submit_operation_t;
};

/// @brief Callback-submission stage of an actor join builder.
class actor_join_callback_submit_operation_t : private detail::operation_builder_base_t<
                                                 detail::actor_join_state_t,
                                                 detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_join_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_join_callback_submit_operation_t ();
    actor_join_callback_submit_operation_t (actor_join_callback_submit_operation_t &&) noexcept;
    actor_join_callback_submit_operation_t &
    operator= (actor_join_callback_submit_operation_t &&) noexcept;

    actor_join_callback_submit_operation_t &&message (message_t &part_) &&;
    actor_join_callback_submit_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    actor_join_callback_submit_operation_t &&flags (int flags_) &&;
    bool submit (actor_join_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class actor_join_submit_operation_t;
};

/// @brief Builds an actor join to an entry spot.
class actor_join_entry_spot_operation_t : private detail::operation_builder_base_t<
                                            detail::actor_join_state_t,
                                            detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_join_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_join_entry_spot_operation_t ();
    actor_join_entry_spot_operation_t (actor_join_entry_spot_operation_t &&) noexcept;
    actor_join_entry_spot_operation_t &operator= (actor_join_entry_spot_operation_t &&) noexcept;

    actor_join_entry_spot_operation_t &&message (message_t &part_) &&;
    actor_join_entry_spot_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    actor_join_entry_spot_operation_t &&flags (int flags_) &&;
    async_result_t<actor_join_entry_spot_result_t> async () &&;
    bool submit (actor_join_entry_spot_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class spot_node_t;
};

/// @brief Builds a reply to an actor-join request.
class actor_join_reply_operation_t : private detail::operation_builder_base_t<
                                       detail::actor_join_reply_state_t,
                                       detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_join_reply_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_join_reply_operation_t ();
    actor_join_reply_operation_t (actor_join_reply_operation_t &&) noexcept;
    actor_join_reply_operation_t &operator= (actor_join_reply_operation_t &&) noexcept;

    actor_join_reply_operation_t &&message (message_t &part_) &&;
    void submit () &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class spot_t;
};

/// @brief Builds an actor leave operation.
class actor_leave_operation_t : private detail::operation_builder_base_t<
                                  detail::actor_payloadless_state_t,
                                  detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_payloadless_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_leave_operation_t ();
    actor_leave_operation_t (actor_leave_operation_t &&) noexcept;
    actor_leave_operation_t &operator= (actor_leave_operation_t &&) noexcept;

    actor_leave_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class spot_node_t;
    friend class actor_t;
};

/// @brief Builds an actor destroy operation.
class actor_destroy_operation_t : private detail::operation_builder_base_t<
                                    detail::actor_payloadless_state_t,
                                    detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_payloadless_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_destroy_operation_t ();
    actor_destroy_operation_t (actor_destroy_operation_t &&) noexcept;
    actor_destroy_operation_t &operator= (actor_destroy_operation_t &&) noexcept;

    actor_destroy_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class spot_node_t;
};

/// @brief Builds a remote actor lookup operation.
class actor_lookup_operation_t : private detail::operation_builder_base_t<
                                   detail::actor_payloadless_state_t,
                                   detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_payloadless_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_lookup_operation_t ();
    actor_lookup_operation_t (actor_lookup_operation_t &&) noexcept;
    actor_lookup_operation_t &operator= (actor_lookup_operation_t &&) noexcept;

    actor_lookup_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<actor_lookup_result_t> async () &&;
    bool submit (actor_lookup_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend class spot_node_t;
};

/// @brief Builds an actor-to-session bind operation.
class actor_bind_operation_t : private detail::operation_builder_base_t<
                                 detail::actor_bind_state_t,
                                 detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_bind_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_bind_operation_t ();
    actor_bind_operation_t (actor_bind_operation_t &&) noexcept;
    actor_bind_operation_t &operator= (actor_bind_operation_t &&) noexcept;

    actor_bind_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend actor_bind_operation_t
    detail_make_actor_bind_operation (detail::actor_bind_state_t &&state_);
};

/// @brief Builds an actor-from-session unbind operation.
class actor_unbind_operation_t : private detail::operation_builder_base_t<
                                   detail::actor_bind_state_t,
                                   detail::unique_operation_state_policy_t>
{
    using base_t = detail::operation_builder_base_t<detail::actor_bind_state_t,
                                                    detail::unique_operation_state_policy_t>;

  public:
    ~actor_unbind_operation_t ();
    actor_unbind_operation_t (actor_unbind_operation_t &&) noexcept;
    actor_unbind_operation_t &operator= (actor_unbind_operation_t &&) noexcept;

    actor_unbind_operation_t &&timeout (std::chrono::milliseconds timeout_) &&;
    async_result_t<std::vector<message_t>> async () &&;
    bool submit (request_callback_t callback_) &&;

  private:
    using base_t::base_t;
    using base_t::state;

    friend actor_unbind_operation_t
    detail_make_actor_unbind_operation (detail::actor_bind_state_t &&state_);
};

} // namespace service
} // namespace zlink
