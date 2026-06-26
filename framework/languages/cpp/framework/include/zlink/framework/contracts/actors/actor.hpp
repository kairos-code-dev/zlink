/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/detail/message_payload.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/messaging/message.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink/framework/contracts/streams/stream.hpp>

#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class actor_gateway_state_t;
class actor_gateway_runtime_t;
class spot_node_runtime_t;
} // namespace detail

class actor_ref_t
{
  public:
    actor_ref_t () = default;
    actor_ref_t (node_rid_t node_rid,
                 std::string actor_type,
                 std::string actor_id,
                 std::uint64_t generation = 1);

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

class actor_gateway_t;
class route_client_t;

struct actor_join_result_t
{
    int result_code = 0;
    actor_ref_t actor;
    message_t reply;
};

template <typename TReply> struct typed_actor_join_result_t
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

class actor_join_spot_call_t
{
  public:
    using submit_fn_t = std::function<result_t<actor_join_result_t> ()>;

    explicit actor_join_spot_call_t (result_t<actor_join_result_t> result) :
        _result (std::move (result))
    {
    }

    actor_join_spot_call_t (result_t<actor_join_result_t> result,
                            serializer_registry_t *serializers) :
        _result (std::move (result)), _serializers (serializers)
    {
    }

    actor_join_spot_call_t (submit_fn_t submit, serializer_registry_t *serializers) :
        _submit (std::move (submit)), _serializers (serializers)
    {
    }

    actor_join_spot_call_t &timeout (std::chrono::milliseconds timeout)
    {
        (void) timeout;
        return *this;
    }

    task_t<actor_join_result_t> async ()
    {
        return task_t<actor_join_result_t> (submit ());
    }

    template <typename TReply> task_t<typed_actor_join_result_t<TReply>> async ()
    {
        auto result = submit ();
        if (!result) {
            const auto *error = result.error ();
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              result.error_kind (),
              error != nullptr ? error->what () : "actor join spot failed");
        }
        if (_serializers == nullptr) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join result has no serializer registry");
        }
        try {
            const auto &joined = result.value ();
            co_return typed_actor_join_result_t<TReply>{
              joined.result_code, joined.actor, joined.reply.template decode<TReply> (*_serializers)};
        }
        catch (const framework_exception_t &error) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
    }

    task_t<actor_join_result_t> yield ()
    {
        if (!_submit) {
            return task_t<actor_join_result_t> (submit ());
        }
        auto yield_turn = detail::capture_current_serial_yield_turn ();
        if (!yield_turn) {
            return task_t<actor_join_result_t> (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield requires a framework Spot handler turn"));
        }
        if (!yield_turn->release ()) {
            return task_t<actor_join_result_t> (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield could not release the current Spot handler turn"));
        }
        auto source = std::make_shared<detail::task_completion_source_t<actor_join_result_t>> (
          yield_turn->resume_scheduler ());
        auto task = source->task ();
        auto submit = _submit;
        std::thread ([source, submit = std::move (submit)] () mutable {
            try {
                source->complete (submit ());
            }
            catch (const framework_exception_t &error) {
                source->complete (result_t<actor_join_result_t>::failure (
                  error.kind (), error.what (), error.is_retriable ()));
            }
            catch (...) {
                source->complete (result_t<actor_join_result_t>::failure (
                  framework_error_kind_t::request_failed, "actor join spot failed"));
            }
        }).detach ();
        return task;
    }

    template <typename TReply> task_t<typed_actor_join_result_t<TReply>> yield ()
    {
        auto result = co_await yield ();
        if (_serializers == nullptr) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join result has no serializer registry");
        }
        try {
            co_return typed_actor_join_result_t<TReply>{
              result.result_code, result.actor, result.reply.template decode<TReply> (*_serializers)};
        }
        catch (const framework_exception_t &error) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
    }

  private:
    result_t<actor_join_result_t> submit ()
    {
        if (_submit) {
            return _submit ();
        }
        return *_result;
    }

    std::optional<result_t<actor_join_result_t>> _result;
    submit_fn_t _submit;
    serializer_registry_t *_serializers = nullptr;
};

class actor_join_entry_spot_call_t
{
  public:
    using submit_fn_t = std::function<result_t<actor_join_result_t> ()>;

    explicit actor_join_entry_spot_call_t (result_t<actor_join_result_t> result) :
        _result (std::move (result))
    {
    }

    actor_join_entry_spot_call_t (result_t<actor_join_result_t> result,
                                  serializer_registry_t *serializers) :
        _result (std::move (result)), _serializers (serializers)
    {
    }

    actor_join_entry_spot_call_t (submit_fn_t submit, serializer_registry_t *serializers) :
        _submit (std::move (submit)), _serializers (serializers)
    {
    }

    actor_join_entry_spot_call_t &timeout (std::chrono::milliseconds timeout)
    {
        (void) timeout;
        return *this;
    }

    task_t<actor_join_result_t> async ()
    {
        return task_t<actor_join_result_t> (submit ());
    }

    template <typename TReply> task_t<typed_actor_join_result_t<TReply>> async ()
    {
        auto result = submit ();
        if (!result) {
            const auto *error = result.error ();
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              result.error_kind (),
              error != nullptr ? error->what () : "actor join entry spot failed");
        }
        if (_serializers == nullptr) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot result has no serializer registry");
        }
        try {
            const auto &joined = result.value ();
            co_return typed_actor_join_result_t<TReply>{
              joined.result_code, joined.actor, joined.reply.template decode<TReply> (*_serializers)};
        }
        catch (const framework_exception_t &error) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
    }

    task_t<actor_join_result_t> yield ()
    {
        if (!_submit) {
            return task_t<actor_join_result_t> (submit ());
        }
        auto yield_turn = detail::capture_current_serial_yield_turn ();
        if (!yield_turn) {
            return task_t<actor_join_result_t> (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield requires a framework Spot handler turn"));
        }
        if (!yield_turn->release ()) {
            return task_t<actor_join_result_t> (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield could not release the current Spot handler turn"));
        }
        auto source = std::make_shared<detail::task_completion_source_t<actor_join_result_t>> (
          yield_turn->resume_scheduler ());
        auto task = source->task ();
        auto submit = _submit;
        std::thread ([source, submit = std::move (submit)] () mutable {
            try {
                source->complete (submit ());
            }
            catch (const framework_exception_t &error) {
                source->complete (result_t<actor_join_result_t>::failure (
                  error.kind (), error.what (), error.is_retriable ()));
            }
            catch (...) {
                source->complete (result_t<actor_join_result_t>::failure (
                  framework_error_kind_t::request_failed, "actor join entry spot failed"));
            }
        }).detach ();
        return task;
    }

    template <typename TReply> task_t<typed_actor_join_result_t<TReply>> yield ()
    {
        auto result = co_await yield ();
        if (_serializers == nullptr) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot result has no serializer registry");
        }
        try {
            co_return typed_actor_join_result_t<TReply>{
              result.result_code, result.actor, result.reply.template decode<TReply> (*_serializers)};
        }
        catch (const framework_exception_t &error) {
            co_return result_t<typed_actor_join_result_t<TReply>>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
    }

  private:
    result_t<actor_join_result_t> submit ()
    {
        if (_submit) {
            return _submit ();
        }
        return *_result;
    }

    std::optional<result_t<actor_join_result_t>> _result;
    submit_fn_t _submit;
    serializer_registry_t *_serializers = nullptr;
};

class relay_request_call_t : private detail::call_facade_t<relay_request_call_t, zlink::message_t>
{
  private:
    using base_t = detail::call_facade_t<relay_request_call_t, zlink::message_t>;

  public:
    explicit relay_request_call_t (result_t<zlink::message_t> result) : base_t (std::move (result))
    {
    }

    using base_t::async;
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

    bound_session_send_call_t send (const message_t &payload);

    template <typename TMessage>
      requires (!std::is_same_v<std::remove_cvref_t<TMessage>, message_t>
                && !std::is_same_v<std::remove_cvref_t<TMessage>, zlink::message_t>)
    bound_session_send_call_t send (const TMessage &message)
    {
        return send_typed (detail::message_name<TMessage> (), std::type_index (typeid (TMessage)),
                           &message);
    }
    bound_session_send_call_t disconnect ();

  private:
    friend class actor_context_t;
    friend class actor_gateway_t;
    friend class session_actor_t;
    friend class session_actor_manager_t;
    friend class detail::actor_gateway_runtime_t;

    explicit bound_session_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                              actor_ref_t actor_ref);

    bound_session_send_call_t
    send_typed (std::string packet_name, std::type_index message_type, const void *message);
    bound_session_send_call_t send_erased (std::string packet_name, const zlink::message_t &payload);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    actor_ref_t _actor_ref;
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

  private:
    actor_join_spot_call_t join_spot_raw (spot_rid_t spot_rid, const zlink::message_t &request)
    {
        return actor_join_spot_call_t (
          [context = *this, spot_rid = std::move (spot_rid), request] () mutable {
          try {
            const auto erased = context.join_spot_erased (std::move (spot_rid), request);
            if (!erased) {
                const auto *error = erased.error ();
                return result_t<actor_join_result_t>::failure (
                  erased.error_kind (),
                  error != nullptr ? error->what () : "actor join spot failed");
            }
            const auto &reply = erased.value ();
            return result_t<actor_join_result_t>::success (
              actor_join_result_t{reply.result_code, reply.actor,
                                  message_t::from_raw (reply.reply,
                                                       context.serializer_registry ())});
        }
        catch (const framework_exception_t &error) {
            return result_t<actor_join_result_t>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
        catch (...) {
            return result_t<actor_join_result_t>::failure (
              framework_error_kind_t::payload_decode_failed,
              "actor join spot reply decode failed");
        }
          },
          serializer_registry ());
    }

  public:
    actor_join_spot_call_t join_spot (spot_rid_t spot_rid, const message_t &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_spot_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join spot requires a serializer registry"));
        }
        try {
            return join_spot_raw (std::move (spot_rid), request.to_raw (*serializers));
        }
        catch (const framework_exception_t &error) {
            return actor_join_spot_call_t (result_t<actor_join_result_t>::failure (
              error.kind (), error.what (), error.is_retriable ()));
        }
    }

    template <typename TRequest>
      requires (!std::is_same_v<std::remove_cvref_t<TRequest>, message_t>)
    actor_join_spot_call_t join_spot (spot_rid_t spot_rid, const TRequest &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_spot_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join spot requires a serializer registry"));
        }
        try {
            return join_spot_raw (std::move (spot_rid),
                                  detail::encoded_payload_to_raw (
                                    serializers->get<TRequest> ().serialize (request)));
        }
        catch (const framework_exception_t &error) {
            return actor_join_spot_call_t (result_t<actor_join_result_t>::failure (
              error.kind (), error.what (), error.is_retriable ()));
        }
    }

  private:
    actor_join_entry_spot_call_t join_entry_spot_raw (node_rid_t spot_node_rid,
                                                      const zlink::message_t &request);

  public:
    actor_join_entry_spot_call_t join_entry_spot (node_rid_t spot_node_rid,
                                                  const message_t &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_entry_spot_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot requires a serializer registry"));
        }
        try {
            return join_entry_spot_raw (std::move (spot_node_rid), request.to_raw (*serializers));
        }
        catch (const framework_exception_t &error) {
            return actor_join_entry_spot_call_t (result_t<actor_join_result_t>::failure (
              error.kind (), error.what (), error.is_retriable ()));
        }
    }

    template <typename TRequest>
      requires (!std::is_same_v<std::remove_cvref_t<TRequest>, message_t>)
    actor_join_entry_spot_call_t join_entry_spot (node_rid_t spot_node_rid,
                                                  const TRequest &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_entry_spot_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot requires a serializer registry"));
        }
        try {
            return join_entry_spot_raw (std::move (spot_node_rid),
                                        detail::encoded_payload_to_raw (
                                          serializers->get<TRequest> ().serialize (request)));
        }
        catch (const framework_exception_t &error) {
            return actor_join_entry_spot_call_t (result_t<actor_join_result_t>::failure (
              error.kind (), error.what (), error.is_retriable ()));
        }
    }

  private:
    friend class spot_node_builder_t;
    friend class actor_gateway_t;
    friend class detail::spot_node_runtime_t;
    friend class session_actor_t;
    friend class session_actor_manager_t;
    friend class detail::actor_gateway_runtime_t;
    explicit actor_context_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                              actor_ref_t actor_ref);

    result_t<detail::actor_join_reply_t> join_spot_erased (spot_rid_t spot_rid,
                                                           const zlink::message_t &request);
    serializer_registry_t *serializer_registry () const noexcept;
    std::optional<zlink::message_t> create_payload () const;

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    std::shared_ptr<actor_ref_t> _actor_ref;
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
    relay_call_t relay (const zlink::message_t &payload);
    relay_request_call_t relay_request (const zlink::message_t &payload);
    relay_call_t notify_disconnected ();

  private:
    friend class session_actor_manager_t;
    friend class actor_gateway_t;
    friend class detail::actor_gateway_runtime_t;

    explicit session_actor_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                              actor_ref_t ref);

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
    result_t<session_actor_t>
    create (std::string actor_type, std::string actor_id, const zlink::message_t &request);
    result_t<session_actor_t>
    create (std::string actor_type, std::string actor_id, const message_t &request);
    template <typename TRequest>
      requires (!std::is_same_v<std::remove_cvref_t<TRequest>, zlink::message_t>
                && !std::is_same_v<std::remove_cvref_t<TRequest>, message_t>)
    result_t<session_actor_t>
    create (std::string actor_type, std::string actor_id, const TRequest &request)
    {
        try {
            return create_erased (std::move (actor_type), std::move (actor_id),
                                  serialize_request (std::type_index (typeid (TRequest)),
                                                     &request));
        }
        catch (const framework_exception_t &error) {
            return result_t<session_actor_t>::failure (error.kind (), error.what (),
                                                       error.is_retriable ());
        }
    }
    std::optional<session_actor_t> find (std::string actor_id) const;
    result_t<session_actor_t> get_or_create (std::string actor_type, std::string actor_id);
    result_t<session_actor_t>
    get_or_create (std::string actor_type, std::string actor_id, const zlink::message_t &request);
    result_t<session_actor_t>
    get_or_create (std::string actor_type, std::string actor_id, const message_t &request);
    template <typename TRequest>
      requires (!std::is_same_v<std::remove_cvref_t<TRequest>, zlink::message_t>
                && !std::is_same_v<std::remove_cvref_t<TRequest>, message_t>)
    result_t<session_actor_t>
    get_or_create (std::string actor_type, std::string actor_id, const TRequest &request)
    {
        try {
            return get_or_create_erased (
              std::move (actor_type), std::move (actor_id),
              serialize_request (std::type_index (typeid (TRequest)), &request));
        }
        catch (const framework_exception_t &error) {
            return result_t<session_actor_t>::failure (error.kind (), error.what (),
                                                       error.is_retriable ());
        }
    }
    request_call_t<session_actor_t> bind (actor_ref_t actor_ref);
    void unbind_session (std::string actor_id) noexcept;

  private:
    friend class actor_gateway_t;
    friend class detail::actor_gateway_runtime_t;
    explicit session_actor_manager_t (std::shared_ptr<detail::actor_gateway_state_t> state);
    result_t<session_actor_t>
    create_erased (std::string actor_type, std::string actor_id, std::optional<zlink::message_t> request);
    result_t<session_actor_t>
    get_or_create_erased (std::string actor_type,
                          std::string actor_id,
                          std::optional<zlink::message_t> request);
    zlink::message_t serialize_request (std::type_index request_type, const void *request) const;

    std::shared_ptr<detail::actor_gateway_state_t> _state;
};

class actor_gateway_t
{
  public:
    actor_gateway_t ();
    ~actor_gateway_t ();

    actor_gateway_t (actor_gateway_t &&) noexcept;
    actor_gateway_t &operator= (actor_gateway_t &&) noexcept;
    actor_gateway_t (const actor_gateway_t &) = default;
    actor_gateway_t &operator= (const actor_gateway_t &) = default;

    session_actor_manager_t manager () const;
    actor_context_t actor_context (const actor_ref_t &actor_ref) const;
    void bind_session_stream (std::string actor_id,
                              stream_t stream,
                              stream_codec_t codec = stream_codec_t::message_pack);
    void bind_session_route (actor_ref_t actor_ref,
                             route_client_t route_client,
                             std::string route_channel_name,
                             zlink::routing_id_t target_node_rid,
                             stream_codec_t codec = stream_codec_t::message_pack);
    void unbind_session_stream (std::string actor_id);

  private:
    friend class detail::actor_gateway_runtime_t;
    explicit actor_gateway_t (std::shared_ptr<detail::actor_gateway_state_t> state);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
};

} // namespace zlink::framework
