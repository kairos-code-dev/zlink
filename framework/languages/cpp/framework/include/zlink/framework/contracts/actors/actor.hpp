/* SPDX-License-Identifier: FSL-1.1-ALv2 */
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
#include <variant>
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
class session_actor_binding_context_t;
class session_actor_manager_access_t;
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

struct actor_ref_snapshot_t
{
    node_rid_t node_rid;
    std::string actor_id;
    std::uint64_t generation = 0;

    static actor_ref_snapshot_t from (const actor_ref_t &actor_ref)
    {
        return actor_ref_snapshot_t{actor_ref.node_rid (), std::string (actor_ref.actor_id ()),
                                    actor_ref.generation ()};
    }

    actor_ref_t to_actor_ref (std::string actor_type) const
    {
        return actor_ref_t (node_rid, std::move (actor_type), actor_id, generation);
    }
};

struct actor_placement_t
{
    std::optional<node_rid_t> preferred_node_rid;
    std::optional<std::string> route_mesh;
};

class actor_client_t;
class route_client_t;

class actor_send_call_t
{
  public:
    actor_send_call_t (actor_client_t &client,
                       actor_ref_t actor_ref,
                       std::string packet_name,
                       message_t message);

    void submit ();

  private:
    actor_client_t *_client;
    actor_ref_t _actor_ref;
    std::string _packet_name;
    message_t _message;
};

class actor_request_call_t
{
  public:
    actor_request_call_t (actor_client_t &client,
                          actor_ref_t actor_ref,
                          std::string packet_name,
                          message_t request);

    actor_request_call_t &timeout (std::chrono::milliseconds timeout);

    template <typename TReply> task_t<TReply> async ()
    {
        auto reply = co_await async_message ();
        co_return reply.template decode<TReply> (serializers ());
    }

    task_t<message_t> async_message ();

  private:
    serializer_registry_t &serializers () const;

    actor_client_t *_client;
    actor_ref_t _actor_ref;
    std::string _packet_name;
    message_t _request;
    std::optional<std::chrono::milliseconds> _timeout;
};

class actor_client_t
{
  public:
    virtual ~actor_client_t () = default;

    template <typename TMessage>
    actor_send_call_t send_to_actor (actor_ref_t actor_ref, TMessage message)
    {
        using message_type = std::remove_cvref_t<TMessage>;
        return actor_send_call_t (*this, std::move (actor_ref),
                                  detail::message_name<message_type> (),
                                  message_t::from (std::move (message)));
    }

    template <typename TRequest>
    actor_request_call_t request_to_actor (actor_ref_t actor_ref, TRequest request)
    {
        using request_type = std::remove_cvref_t<TRequest>;
        return actor_request_call_t (*this, std::move (actor_ref),
                                     detail::message_name<request_type> (),
                                     message_t::from (std::move (request)));
    }

  protected:
    virtual task_t<void> send_to_actor_erased (actor_ref_t actor_ref,
                                               std::string packet_name,
                                               message_t message) = 0;
    virtual task_t<message_t> request_to_actor_erased (
      actor_ref_t actor_ref,
      std::string packet_name,
      message_t request,
      std::optional<std::chrono::milliseconds> timeout) = 0;
    virtual serializer_registry_t &actor_client_serializers () = 0;

  private:
    friend class actor_send_call_t;
    friend class actor_request_call_t;
};

class actor_directory_t
{
  public:
    virtual ~actor_directory_t () = default;
    virtual task_t<std::optional<actor_ref_t>> find (std::string actor_id) = 0;
    virtual task_t<actor_ref_t> ensure (std::string actor_id,
                                        message_t create_request,
                                        actor_placement_t placement = {}) = 0;
};

template <typename TReply> struct actor_join_accepted_t
{
    actor_ref_t actor;
    TReply reply;
};

template <typename TReply> struct actor_join_rejected_t
{
    TReply reply;
};

template <typename TReply>
using typed_actor_join_result_t =
  std::variant<actor_join_accepted_t<TReply>, actor_join_rejected_t<TReply>>;

using actor_join_result_t = typed_actor_join_result_t<message_t>;

namespace detail
{
inline actor_join_result_t
make_actor_join_result (int result_code, actor_ref_t actor, message_t reply)
{
    if (result_code == 0) {
        return actor_join_accepted_t<message_t>{std::move (actor), std::move (reply)};
    }
    return actor_join_rejected_t<message_t>{std::move (reply)};
}
} // namespace detail

namespace detail
{
struct actor_join_reply_t
{
    int result_code = 0;
    actor_ref_t actor;
    zlink::message_t reply;
};
} // namespace detail

class actor_join_call_t
{
  public:
    using submit_fn_t = std::function<result_t<actor_join_result_t> ()>;

    explicit actor_join_call_t (result_t<actor_join_result_t> result) :
        _result (std::move (result))
    {
    }

    actor_join_call_t (result_t<actor_join_result_t> result,
                       serializer_registry_t *serializers) :
        _result (std::move (result)), _serializers (serializers)
    {
    }

    actor_join_call_t (submit_fn_t submit, serializer_registry_t *serializers) :
        _submit (std::move (submit)), _serializers (serializers)
    {
    }

    actor_join_call_t &timeout (std::chrono::milliseconds timeout)
    {
        (void) timeout;
        return *this;
    }

    task_t<actor_join_result_t> async ()
    {
        auto submit_fn = submitter ();
        auto turn_handle = detail::capture_current_serial_turn ();
        if (!submit_fn || !turn_handle || turn_handle->released () || !turn_handle->release ()) {
            return task_t<actor_join_result_t> (submit ());
        }
        // The join submit blocks until the admission reply arrives, so it must
        // run off the Spot serial line. The line is released at the await point so
        // independent work progresses; the continuation resumes serialized on
        // the same line.
        auto source = std::make_shared<detail::task_completion_source_t<actor_join_result_t>> (
          turn_handle->resume_scheduler ());
        auto task = source->task ();
        std::thread ([source, submit = std::move (submit_fn)] () mutable {
            try {
                source->complete (submit ());
            }
            catch (const framework_exception_t &error) {
                source->complete (detail::result_access_t::failure<actor_join_result_t> (error));
            }
            catch (...) {
                source->complete (result_t<actor_join_result_t>::failure (
                  framework_error_kind_t::request_failed, "actor join spot failed"));
            }
        }).detach ();
        return task;
    }

    /* 타입 지정 await도 무타입 경로와 같은 실행 규약을 따라야 한다(async-execution-policy §1):
     * join submit은 admission 응답까지 블로킹하므로 Spot 직렬 줄 밖에서 돌고, 줄은 await
     * 지점에서 풀려 형제 actor가 진행한다. 여기서 submit()을 직접 부르면 줄을 쥔 채로
     * 기다리게 되어 같은 Spot의 다른 actor 요청이 굶는다. */
    template <typename TReply> task_t<typed_actor_join_result_t<TReply>> async ()
    {
        auto submit_fn = submitter ();
        auto turn_handle = detail::capture_current_serial_turn ();
        auto *registry = _serializers;
        if (!submit_fn || !turn_handle || turn_handle->released () || !turn_handle->release ()) {
            return task_t<typed_actor_join_result_t<TReply>> (
              decode_join<TReply> (submit (), registry));
        }
        auto source =
          std::make_shared<detail::task_completion_source_t<typed_actor_join_result_t<TReply>>> (
            turn_handle->resume_scheduler ());
        auto task = source->task ();
        std::thread ([source, submit = std::move (submit_fn), registry] () mutable {
            try {
                source->complete (decode_join<TReply> (submit (), registry));
            }
            catch (const framework_exception_t &error) {
                source->complete (
                  detail::result_access_t::failure<typed_actor_join_result_t<TReply>> (error));
            }
            catch (...) {
                source->complete (result_t<typed_actor_join_result_t<TReply>>::failure (
                  framework_error_kind_t::request_failed, "actor join spot failed"));
            }
        }).detach ();
        return task;
    }

  private:
    template <typename TReply>
    static result_t<typed_actor_join_result_t<TReply>>
    decode_join (const result_t<actor_join_result_t> &result, serializer_registry_t *registry)
    {
        if (!result) {
            const auto *error = result.error ();
            return result_t<typed_actor_join_result_t<TReply>>::failure (
              result.error_kind (), error != nullptr ? error->what () : "actor join spot failed");
        }
        if (registry == nullptr) {
            return result_t<typed_actor_join_result_t<TReply>>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join result has no serializer registry");
        }
        try {
            const auto &joined = result.value ();
            if (const auto *accepted = std::get_if<actor_join_accepted_t<message_t>> (&joined)) {
                return result_t<typed_actor_join_result_t<TReply>>::success (
                  typed_actor_join_result_t<TReply>{actor_join_accepted_t<TReply>{
                    accepted->actor, accepted->reply.template decode<TReply> (*registry)}});
            }
            const auto &rejected = std::get<actor_join_rejected_t<message_t>> (joined);
            return result_t<typed_actor_join_result_t<TReply>>::success (
              typed_actor_join_result_t<TReply>{actor_join_rejected_t<TReply>{
                rejected.reply.template decode<TReply> (*registry)}});
        }
        catch (const framework_exception_t &error) {
            return detail::result_access_t::failure<typed_actor_join_result_t<TReply>> (error);
        }
    }

  public:

  protected:
    result_t<actor_join_result_t> submit ()
    {
        if (_submit) {
            return _submit ();
        }
        return *_result;
    }

    serializer_registry_t *serializers () const noexcept { return _serializers; }
    submit_fn_t submitter () const { return _submit; }

  private:
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
        using message_type = std::remove_cvref_t<TMessage>;
        return send_typed (detail::message_name<message_type> (),
                           [&message] (serializer_registry_t &serializers) {
                               return serializers.template get<message_type> ().serialize (message);
                           });
    }
    task_t<void> disconnect ();

  private:
    friend class actor_context_t;
    friend class session_actor_t;
    friend class session_actor_manager_t;
    friend class detail::actor_gateway_runtime_t;

    explicit bound_session_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                              actor_ref_t actor_ref);

    bound_session_send_call_t
    send_typed (std::string packet_name,
                std::function<encoded_payload_t (serializer_registry_t &)> encode_payload);
    bound_session_send_call_t
    send_typed (std::string packet_name, std::type_index message_type, const void *message);
    bound_session_send_call_t send_erased (std::string packet_name,
                                           const zlink::message_t &payload);

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
    std::optional<spot_rid_t> spot_rid () const;
    bound_session_t bound_session () const;

  private:
    actor_join_call_t join_spot_payload (spot_rid_t spot_rid, const zlink::message_t &request)
    {
        return actor_join_call_t (
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
                  return result_t<actor_join_result_t>::success (detail::make_actor_join_result (
                    reply.result_code, reply.actor,
                    message_t::from_raw (reply.reply, context.serializer_registry ())));
              }
              catch (const framework_exception_t &error) {
                  return detail::result_access_t::failure<actor_join_result_t> (error);
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
    actor_join_call_t join_spot (spot_rid_t spot_rid, const message_t &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join spot requires a serializer registry"));
        }
        try {
            return join_spot_payload (std::move (spot_rid), request.to_raw (*serializers));
        }
        catch (const framework_exception_t &error) {
            return actor_join_call_t (detail::result_access_t::failure<actor_join_result_t> (error));
        }
    }

    template <typename TRequest>
    requires (!std::is_same_v<std::remove_cvref_t<TRequest>, message_t>) actor_join_call_t
      join_spot (spot_rid_t spot_rid, const TRequest &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join spot requires a serializer registry"));
        }
        try {
            return join_spot_payload (
              std::move (spot_rid),
              detail::encoded_payload_to_raw (serializers->get<TRequest> ().serialize (request)));
        }
        catch (const framework_exception_t &error) {
            return actor_join_call_t (detail::result_access_t::failure<actor_join_result_t> (error));
        }
    }

  private:
    actor_join_call_t join_entry_spot_payload (node_rid_t spot_node_rid,
                                                 const zlink::message_t &request);

  public:
    actor_join_call_t join_entry_spot (node_rid_t spot_node_rid,
                                             const message_t &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot requires a serializer registry"));
        }
        try {
            return join_entry_spot_payload (std::move (spot_node_rid), request.to_raw (*serializers));
        }
        catch (const framework_exception_t &error) {
            return actor_join_call_t (detail::result_access_t::failure<actor_join_result_t> (error));
        }
    }

    template <typename TRequest>
    requires (!std::is_same_v<std::remove_cvref_t<TRequest>, message_t>)
      actor_join_call_t
      join_entry_spot (node_rid_t spot_node_rid, const TRequest &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            return actor_join_call_t (result_t<actor_join_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot requires a serializer registry"));
        }
        try {
            return join_entry_spot_payload (
              std::move (spot_node_rid),
              detail::encoded_payload_to_raw (serializers->get<TRequest> ().serialize (request)));
        }
        catch (const framework_exception_t &error) {
            return actor_join_call_t (detail::result_access_t::failure<actor_join_result_t> (error));
        }
    }

  private:
    friend class spot_node_builder_t;
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
    task_t<void> relay (const zlink::message_t &payload);
    task_t<void> relay (std::string packet_name, const zlink::message_t &payload);
    relay_request_call_t relay_request (const zlink::message_t &payload);
    relay_request_call_t relay_request (std::string packet_name, const zlink::message_t &payload);
    task_t<void> notify_disconnected ();

  private:
    friend class session_actor_manager_t;
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
      result_t<session_actor_t> create (std::string actor_type,
                                        std::string actor_id,
                                        const TRequest &request)
    {
        try {
            return create (std::move (actor_type), std::move (actor_id), message_t::from (request));
        }
        catch (const framework_exception_t &error) {
            return detail::result_access_t::failure<session_actor_t> (error);
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
      result_t<session_actor_t> get_or_create (std::string actor_type,
                                               std::string actor_id,
                                               const TRequest &request)
    {
        try {
            return get_or_create (std::move (actor_type), std::move (actor_id),
                                  message_t::from (request));
        }
        catch (const framework_exception_t &error) {
            return detail::result_access_t::failure<session_actor_t> (error);
        }
    }
    request_call_t<session_actor_t> bind (actor_ref_t actor_ref);
    request_call_t<session_actor_t> bind_or_get (actor_ref_t actor_ref);

  private:
    friend class detail::actor_gateway_runtime_t;
    friend class detail::session_actor_manager_access_t;
    explicit session_actor_manager_t (std::shared_ptr<detail::actor_gateway_state_t> state);
    result_t<session_actor_t> create_erased (std::string actor_type,
                                             std::string actor_id,
                                             std::optional<zlink::message_t> request);
    result_t<session_actor_t> get_or_create_erased (std::string actor_type,
                                                    std::string actor_id,
                                                    std::optional<zlink::message_t> request);
    zlink::message_t serialize_request (std::type_index request_type, const void *request) const;
    void bind_current_session (const actor_ref_t &actor_ref);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    std::shared_ptr<detail::session_actor_binding_context_t> _binding_context;
};

} // namespace zlink::framework
