/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/detail/message_payload.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink/framework/contracts/streams/stream.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class actor_gateway_state_t;
class actor_gateway_runtime_t;
} // namespace detail

class actor_ref_t
{
  public:
    actor_ref_t () = default;
    actor_ref_t (node_rid_t node_rid, std::string actor_type, std::string actor_id, std::uint64_t generation = 1);

    const node_rid_t &node_rid () const noexcept;
    std::string_view actor_type () const noexcept;
    std::string_view actor_id () const noexcept;
    std::uint64_t generation () const noexcept;
    bool empty () const noexcept;

  private:
    node_rid_t _node_rid;
    std::string _actor_type;
    std::string _actor_id;
    std::uint64_t _generation = 0;
};

template <typename TReply> struct actor_join_result_t
{
    int result_code = 0;
    actor_ref_t actor;
    TReply reply;
};

namespace detail
{
struct actor_join_reply_t
{
    int result_code = 0;
    actor_ref_t actor;
    zlink::message_t reply;
};
} // namespace detail

template <typename TReply>
class actor_join_spot_call_t
    : private detail::call_facade_t<actor_join_spot_call_t<TReply>, actor_join_result_t<TReply>>
{
  private:
    using base_t = detail::call_facade_t<actor_join_spot_call_t<TReply>, actor_join_result_t<TReply>>;

  public:
    explicit actor_join_spot_call_t (result_t<actor_join_result_t<TReply>> result) : base_t (std::move (result)) {}

    using base_t::submit;
    using base_t::timeout;
};

class actor_join_entry_spot_call_t : private detail::call_facade_t<actor_join_entry_spot_call_t, actor_ref_t>
{
  private:
    using base_t = detail::call_facade_t<actor_join_entry_spot_call_t, actor_ref_t>;

  public:
    explicit actor_join_entry_spot_call_t (result_t<actor_ref_t> result) : base_t (std::move (result)) {}

    using base_t::submit;
    using base_t::timeout;
};

class bound_session_t
{
  public:
    bound_session_t ();
    ~bound_session_t ();

    bound_session_t (bound_session_t &&) noexcept;
    bound_session_t &operator= (bound_session_t &&) noexcept;
    bound_session_t (const bound_session_t &) = default;
    bound_session_t &operator= (const bound_session_t &) = default;

    template <typename TMessage> send_call_t send (const TMessage &message)
    {
        return send_erased (detail::to_message_payload (message, 0));
    }

    send_call_t send_raw (const zlink::message_t &payload);
    send_call_t disconnect ();

  private:
    friend class actor_context_t;
    friend class session_actor_t;
    friend class session_actor_manager_t;
    friend class detail::actor_gateway_runtime_t;

    explicit bound_session_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                              std::string actor_id,
                              std::uint64_t generation);

    send_call_t send_erased (const zlink::message_t &payload);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    std::string _actor_id;
    std::uint64_t _generation = 0;
};

class actor_context_t
{
  public:
    actor_context_t ();
    ~actor_context_t ();

    actor_context_t (actor_context_t &&) noexcept;
    actor_context_t &operator= (actor_context_t &&) noexcept;
    actor_context_t (const actor_context_t &) = default;
    actor_context_t &operator= (const actor_context_t &) = default;

    const actor_ref_t &actor_ref () const noexcept;
    bool is_joined () const noexcept;
    bound_session_t bound_session () const;

    template <typename TRequest, typename TReply>
    actor_join_spot_call_t<TReply> join_spot (spot_rid_t spot_rid, const TRequest &request)
    {
        try {
            const auto erased = join_spot_erased (std::move (spot_rid), zlink::message_t::from_json (request));
            if (!erased) {
                const auto *error = erased.error ();
                return actor_join_spot_call_t<TReply> (result_t<actor_join_result_t<TReply>>::failure (
                  erased.error_kind (), error != nullptr ? error->what () : "actor join spot failed"));
            }
            const auto &reply = erased.value ();
            return actor_join_spot_call_t<TReply> (result_t<actor_join_result_t<TReply>>::success (
              actor_join_result_t<TReply>{reply.result_code, reply.actor, reply.reply.template parse_json<TReply> ()}));
        }
        catch (const framework_exception_t &error) {
            return actor_join_spot_call_t<TReply> (
              result_t<actor_join_result_t<TReply>>::failure (error.kind (), error.what (), error.is_retriable ()));
        }
        catch (...) {
            return actor_join_spot_call_t<TReply> (result_t<actor_join_result_t<TReply>>::failure (
              framework_error_kind_t::payload_decode_failed, "actor join spot reply decode failed"));
        }
    }

    actor_join_entry_spot_call_t join_entry_spot (node_rid_t spot_node_rid);

  private:
    friend class session_actor_t;
    friend class session_actor_manager_t;
    explicit actor_context_t (std::shared_ptr<detail::actor_gateway_state_t> state, actor_ref_t actor_ref);

    result_t<detail::actor_join_reply_t> join_spot_erased (spot_rid_t spot_rid, const zlink::message_t &request);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    actor_ref_t _actor_ref;
};

class session_actor_t
{
  public:
    session_actor_t ();
    ~session_actor_t ();

    session_actor_t (session_actor_t &&) noexcept;
    session_actor_t &operator= (session_actor_t &&) noexcept;
    session_actor_t (const session_actor_t &) = default;
    session_actor_t &operator= (const session_actor_t &) = default;

    const actor_ref_t &ref () const noexcept;
    std::string_view actor_id () const noexcept;
    actor_context_t context () const;
    bound_session_t bound_session () const;
    relay_call_t relay (const stream_header_t &header, const zlink::message_t &payload);
    relay_call_t notify_disconnected ();

  private:
    friend class session_actor_manager_t;
    friend class detail::actor_gateway_runtime_t;

    explicit session_actor_t (std::shared_ptr<detail::actor_gateway_state_t> state, actor_ref_t ref);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    actor_ref_t _ref;
};

class session_actor_manager_t
{
  public:
    session_actor_manager_t ();
    ~session_actor_manager_t ();

    session_actor_manager_t (session_actor_manager_t &&) noexcept;
    session_actor_manager_t &operator= (session_actor_manager_t &&) noexcept;
    session_actor_manager_t (const session_actor_manager_t &) = default;
    session_actor_manager_t &operator= (const session_actor_manager_t &) = default;

    result_t<session_actor_t> create (std::string actor_type, std::string actor_id);
    std::optional<session_actor_t> find (std::string actor_id) const;
    result_t<session_actor_t> get_or_create (std::string actor_type, std::string actor_id);
    request_call_t<session_actor_t> bind (actor_ref_t actor_ref);
    void unbind_session (std::string actor_id) noexcept;

  private:
    friend class detail::actor_gateway_runtime_t;
    explicit session_actor_manager_t (std::shared_ptr<detail::actor_gateway_state_t> state);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
};

} // namespace zlink::framework
