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
#include <climits>
#include <chrono>
#include <concepts>
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

using actor_id_t = std::string;

namespace detail
{
class actor_gateway_state_t;
class actor_gateway_runtime_t;
class session_actor_binding_context_t;
class session_actor_manager_access_t;
class actor_manager_access_t;
class actor_manager_state_t;
class actor_create_call_state_t;
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

class actor_client_t;
class route_client_t;
class actor_context_t;

struct actor_join_accepted_t
{
    std::uint64_t operation_id_high = 0;
    std::uint64_t operation_id_low = 0;
    actor_ref_t actor;
    std::optional<message_t> reply;
};

struct actor_join_rejected_t
{
    std::uint64_t operation_id_high = 0;
    std::uint64_t operation_id_low = 0;
    std::optional<message_t> reply;
};

struct actor_join_failed_t
{
    std::uint64_t operation_id_high = 0;
    std::uint64_t operation_id_low = 0;
    framework_error_kind_t error_kind = framework_error_kind_t::request_failed;
    bool retryable = false;
};

using actor_join_completion_t =
  std::variant<actor_join_accepted_t,
               actor_join_rejected_t,
               actor_join_failed_t>;

class actor_t
{
  public:
    virtual ~actor_t () = default;
    virtual actor_context_t &context () noexcept = 0;
    virtual const actor_context_t &context () const noexcept = 0;
    virtual void configure () {}
    virtual task_t<void>
    on_join_completed (const actor_join_completion_t &)
    {
        return task_t<void> (result_t<void>::success ());
    }
};

template <typename TActor>
requires std::derived_from<TActor, actor_t>
class actor_factory_t
{
  public:
    virtual ~actor_factory_t () = default;
    virtual task_t<std::shared_ptr<TActor>>
    create (actor_context_t &context,
            std::stop_token operation_cancellation) = 0;
};

class actor_send_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;

    actor_send_call_t (actor_client_t &client,
                       actor_ref_t actor_ref,
                       std::string packet_name,
                       message_t message);

    actor_send_call_t &metadata (std::string key, std::string value);
    task_t<void> submit ();

  private:
    actor_client_t *_client;
    actor_ref_t _actor_ref;
    std::string _packet_name;
    message_t _message;
    metadata_map_t _metadata;
    std::shared_ptr<detail::submit_once_t> _submission =
      std::make_shared<detail::submit_once_t> ();
};

class actor_request_call_t
{
  public:
    actor_request_call_t (actor_client_t &client,
                          actor_ref_t actor_ref,
                          std::string packet_name,
                          message_t request);

    actor_request_call_t &timeout (std::chrono::milliseconds timeout);

    template <typename TReply> task_t<TReply> submit ()
    {
        auto reply = co_await start (false);
        co_return reply.template decode<TReply> (serializers ());
    }

    template <typename TReply> task_t<TReply> yield ()
    {
        auto reply = co_await start (true);
        co_return reply.template decode<TReply> (serializers ());
    }

    task_t<message_t> submit_message ();
    task_t<message_t> yield_message ();

  private:
    task_t<message_t> start (bool release_turn);
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
                                               message_t message,
                                               const actor_send_call_t::metadata_map_t &metadata) = 0;
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
};

struct actor_create_existing_t
{
    actor_ref_t actor;
};

struct actor_create_created_t
{
    actor_ref_t actor;
    std::optional<message_t> reply;
};

struct actor_create_rejected_t
{
    std::optional<message_t> reply;
};

using actor_create_result_t =
  std::variant<actor_create_existing_t,
               actor_create_created_t,
               actor_create_rejected_t>;

class actor_create_call_t
{
  public:
    actor_create_call_t (actor_create_call_t &&) noexcept;
    actor_create_call_t &operator= (actor_create_call_t &&) noexcept;
    actor_create_call_t (const actor_create_call_t &) = delete;
    actor_create_call_t &operator= (const actor_create_call_t &) = delete;
    ~actor_create_call_t ();

    actor_create_call_t &in_mesh (std::string mesh_name);
    actor_create_call_t &creation_request (message_t request);
    template <typename TCreation>
    requires (!std::is_same_v<std::remove_cvref_t<TCreation>, zlink::message_t>
              && !std::is_same_v<std::remove_cvref_t<TCreation>, message_t>)
    actor_create_call_t &creation_request (TCreation request)
    {
        return creation_request (message_t::from (std::move (request)));
    }
    actor_create_call_t &timeout (std::chrono::milliseconds timeout);
    task_t<actor_create_result_t> submit ();

  private:
    friend class actor_manager_t;
    explicit actor_create_call_t (
      std::shared_ptr<detail::actor_create_call_state_t> state);
    std::shared_ptr<detail::actor_create_call_state_t> _state;
};

class actor_manager_t
{
  public:
    actor_manager_t ();
    ~actor_manager_t ();
    actor_manager_t (actor_manager_t &&) noexcept;
    actor_manager_t &operator= (actor_manager_t &&) noexcept;
    actor_manager_t (const actor_manager_t &) = default;
    actor_manager_t &operator= (const actor_manager_t &) = default;

    actor_create_call_t create (actor_id_t actor_id,
                                std::string stable_type);
    actor_create_call_t get_or_create (actor_id_t actor_id,
                                       std::string stable_type);
    task_t<std::optional<actor_ref_t>> find (actor_id_t actor_id) const;
    task_t<std::optional<spot_ref_t>> find_spot (actor_id_t actor_id) const;
    task_t<bool> destroy (actor_ref_t actor);

  private:
    friend class detail::actor_manager_access_t;
    explicit actor_manager_t (
      std::shared_ptr<detail::actor_manager_state_t> state);
    std::shared_ptr<detail::actor_manager_state_t> _state;
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

class actor_join_call_t
{
  public:
    using deferred_fn_t = std::function<void (std::chrono::milliseconds)>;

    explicit actor_join_call_t (deferred_fn_t deferred) :
        _deferred (std::move (deferred))
    {}
    actor_join_call_t (actor_join_call_t &&) noexcept = default;
    actor_join_call_t &operator= (actor_join_call_t &&) noexcept = default;
    actor_join_call_t (const actor_join_call_t &) = delete;
    actor_join_call_t &operator= (const actor_join_call_t &) = delete;

    actor_join_call_t &timeout (std::chrono::milliseconds timeout)
    {
        if (timeout.count () < 1 || timeout.count () > INT_MAX) {
            throw framework_exception_t (
              framework_error_kind_t::invalid_configuration,
              "Actor join timeout must be from 1 through INT_MAX milliseconds");
        }
        _timeout = timeout;
        return *this;
    }

    void defer ()
    {
        if (_deferred_once) {
            throw framework_exception_t (
              framework_error_kind_t::already_submitted,
              "Actor join call was already deferred");
        }
        _deferred_once = true;
        if (!_deferred) {
            throw framework_exception_t (
              framework_error_kind_t::invalid_configuration,
              "Actor join call has no deferred operation");
        }
        const auto deadline = std::chrono::steady_clock::now () + _timeout;
        auto registered = detail::defer_current_serial_turn (
          [deferred = std::move (_deferred), deadline] () mutable {
              const auto now = std::chrono::steady_clock::now ();
              if (now >= deadline) {
                  throw framework_exception_t (
                    framework_error_kind_t::deadline_exceeded,
                    "Deferred Actor join deadline elapsed before activation");
              }
              deferred (std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - now));
          });
        if (!registered) {
            const auto *error = registered.error ();
            throw framework_exception_t (
              registered.error_kind (),
              error != nullptr ? error->what ()
                               : "Actor join defer registration failed");
        }
    }

  private:
    deferred_fn_t _deferred;
    std::chrono::milliseconds _timeout{5000};
    bool _deferred_once = false;
};


class relay_request_call_t : private detail::call_facade_t<relay_request_call_t, zlink::message_t>
{
  private:
    using base_t = detail::call_facade_t<relay_request_call_t, zlink::message_t>;

  public:
    explicit relay_request_call_t (result_t<zlink::message_t> result) : base_t (std::move (result))
    {
    }

    using base_t::submit;
    using base_t::timeout;
    using base_t::yield;
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
                           std::type_index (typeid (message_type)),
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
                              actor_ref_t actor_ref,
                              std::uint64_t expected_binding_generation = 0);

    bound_session_send_call_t
    send_typed (std::string packet_name,
                std::type_index message_type,
                std::function<encoded_payload_t (serializer_registry_t &)> encode_payload);
    bound_session_send_call_t
    send_typed (std::string packet_name, std::type_index message_type, const void *message);
    bound_session_send_call_t send_erased (std::string packet_name,
                                           stream_codec_t codec,
                                           const zlink::message_t &payload);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    actor_ref_t _actor_ref;
    std::uint64_t _expected_binding_generation = 0;
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
    std::optional<spot_id_t> spot_id () const;
    bound_session_t bound_session () const;

  private:
    actor_join_call_t join_spot_payload (spot_id_t spot_id, const zlink::message_t &request)
    {
        return actor_join_call_t (
          [context = *this, spot_id = std::move (spot_id), request] (
            std::chrono::milliseconds timeout) mutable {
              const auto erased =
                context.join_spot_erased (std::move (spot_id), request, timeout);
              if (!erased) {
                  const auto *error = erased.error ();
                  throw framework_exception_t (
                    erased.error_kind (),
                    error != nullptr ? error->what () : "actor join spot failed");
              }
          });
    }

  public:
    actor_join_call_t join_spot (spot_id_t spot_id, const message_t &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            throw framework_exception_t (
              framework_error_kind_t::request_protocol_error,
              "actor join spot requires a serializer registry");
        }
        return join_spot_payload (std::move (spot_id), request.to_raw (*serializers));
    }

    template <typename TRequest>
    requires (!std::is_same_v<std::remove_cvref_t<TRequest>, message_t>) actor_join_call_t
      join_spot (spot_id_t spot_id, const TRequest &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            throw framework_exception_t (
              framework_error_kind_t::request_protocol_error,
              "actor join spot requires a serializer registry");
        }
        return join_spot_payload (
          std::move (spot_id),
          detail::encoded_payload_to_raw (serializers->get<TRequest> ().serialize (request)));
    }

    actor_join_call_t join_spot (spot_id_t spot_id)
    {
        return join_spot_payload (std::move (spot_id), zlink::message_t{});
    }

  private:
    actor_join_call_t join_entry_spot_payload (const zlink::message_t &request);

  public:
    actor_join_call_t join_entry_spot (const message_t &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            throw framework_exception_t (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot requires a serializer registry");
        }
        return join_entry_spot_payload (request.to_raw (*serializers));
    }

    template <typename TRequest>
    requires (!std::is_same_v<std::remove_cvref_t<TRequest>, message_t>)
      actor_join_call_t
      join_entry_spot (const TRequest &request)
    {
        auto *serializers = serializer_registry ();
        if (serializers == nullptr) {
            throw framework_exception_t (
              framework_error_kind_t::request_protocol_error,
              "actor join entry spot requires a serializer registry");
        }
        return join_entry_spot_payload (
          detail::encoded_payload_to_raw (serializers->get<TRequest> ().serialize (request)));
    }

    actor_join_call_t join_entry_spot ()
    {
        return join_entry_spot_payload (zlink::message_t{});
    }

  private:
    friend class spot_node_builder_t;
    friend class detail::spot_node_runtime_t;
    friend class session_actor_t;
    friend class session_actor_manager_t;
    friend class detail::actor_gateway_runtime_t;
    explicit actor_context_t (std::shared_ptr<detail::actor_gateway_state_t> state,
                              actor_ref_t actor_ref,
                              std::uint64_t source_binding_generation = 0);

    result_t<detail::actor_join_reply_t> join_spot_erased (spot_id_t spot_id,
                                                           const zlink::message_t &request,
                                                           std::chrono::milliseconds timeout);
    serializer_registry_t *serializer_registry () const noexcept;
    std::optional<zlink::message_t> create_payload () const;

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    std::shared_ptr<actor_ref_t> _actor_ref;
    std::uint64_t _source_binding_generation = 0;
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
                              actor_ref_t ref,
                              std::uint64_t binding_token = 0);
    task_t<void> relay_internal (const zlink::message_t &payload);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    actor_ref_t _ref;
    std::uint64_t _binding_token = 0;
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
    std::uint64_t bind_current_session (const actor_ref_t &actor_ref);

    std::shared_ptr<detail::actor_gateway_state_t> _state;
    std::shared_ptr<detail::session_actor_binding_context_t> _binding_context;
};

template <typename TActor, typename TActorFactory>
spot_node_builder_t &
spot_node_builder_t::add_actor_factory (
  std::string actor_type,
  std::shared_ptr<TActorFactory> factory)
{
    static_assert (std::derived_from<TActor, actor_t>);
    static_assert (
      std::derived_from<TActorFactory, actor_factory_t<TActor>>);
    if (!factory) {
        throw framework_exception_t (
          framework_error_kind_t::invalid_configuration,
          "Actor factory must not be null");
    }
    return add_actor_factory_erased (
      std::move (actor_type), std::type_index (typeid (TActor)),
      {},
      [] (void *actor, const actor_ref_t &,
          void *actor_context) {
          if (actor_context != nullptr) {
              static_cast<TActor *> (actor)->context ()
                = *static_cast<actor_context_t *> (
                  actor_context);
          }
      },
      [] (void *, serializer_registry_t &)
        -> std::optional<zlink::message_t> { return std::nullopt; },
      [] (void *, const zlink::message_t &, serializer_registry_t &) {},
      [factory = std::move (factory)] (
        actor_context_t &context) -> std::shared_ptr<void> {
          auto created = factory->create (context, {}).result ();
          if (!created) {
              const auto *error = created.error ();
              throw framework_exception_t (
                created.error_kind (),
                error != nullptr ? error->what ()
                                 : "Actor factory failed");
          }
          if (!created.value ()) {
              throw framework_exception_t (
                framework_error_kind_t::actor_route_not_found,
                "Actor factory returned null");
          }
          created.value ()->configure ();
          return std::static_pointer_cast<void> (
            std::move (created.value ()));
      },
      [] (void *actor,
          std::uint64_t operation_id_high,
          std::uint64_t operation_id_low,
          const actor_ref_t &committed,
          const std::optional<message_t> &reply) {
          const actor_join_completion_t completion =
            actor_join_accepted_t{
              operation_id_high,
              operation_id_low,
              committed,
              reply};
          return static_cast<TActor *> (actor)
            ->on_join_completed (completion);
      });
}

} // namespace zlink::framework
