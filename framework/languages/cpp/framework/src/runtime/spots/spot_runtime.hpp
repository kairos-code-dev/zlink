/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/execution/serial_execution_queue.hpp"
#include "runtime/registry/registry_runtime.hpp"

#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace zlink::framework::detail
{

namespace runtime = zlink::framework::runtime;

namespace service = zlink::service;

class spot_node_builder_state_t
{
  public:
    explicit spot_node_builder_state_t (std::string name) : snapshot{.name = std::move (name)} {}

    mutable std::recursive_mutex mutex;
    spot_node_snapshot_t snapshot;
    std::map<std::string, std::type_index> spot_factories;
    std::map<std::string, spot_lifecycle_callbacks_t> spot_lifecycles;
    std::map<std::string, spot_rid_t> spot_rids_by_name;
    std::map<std::string, std::string> spot_names_by_rid;
    std::map<std::string, spot_context_t> spot_contexts_by_rid;
    std::weak_ptr<service::spot_node_t> native_node;
    std::shared_ptr<channel_runtime_state_t> channel_runtime;
    dispatch_options_t dispatch;
    std::shared_ptr<registry_runtime_state_t> registry_runtime;
    std::map<std::string, spot_rid_t> actor_spot_rids;
    std::map<std::string, std::uint64_t> actor_generations;
    std::set<std::string> actor_created_keys;
    std::set<std::string> destroying_actors;
    std::set<std::string> destroyed_actor_keys;
    struct actor_factory_registration_t
    {
        std::type_index actor_type{typeid (void)};
        std::function<std::shared_ptr<void> (std::string)> create_instance;
        std::function<void (void *, const actor_ref_t &, void *)> configure_instance;
    };
    std::function<result_t<void> (const actor_ref_t &)> destroy_actor_registry;
    std::function<result_t<void> (const actor_ref_t &)> update_actor_registry_ref;
    std::function<result_t<std::optional<zlink::message_t>> (const actor_ref_t &,
                                                             actor_context_t,
                                                             std::string_view,
                                                             const zlink::message_t &,
                                                             service_provider_t &,
                                                             serializer_registry_t &,
                                                             spot_actor_message_metadata_t)>
      actor_packet_relay;
    std::map<std::string, actor_factory_registration_t> actor_factories;
    std::map<std::string, std::shared_ptr<void>> actor_instances;
    std::map<std::string, spot_route_t> actor_routes;
    std::map<std::string, std::shared_ptr<service::spot_t>> native_spots_by_rid;
    std::map<std::string, std::function<std::optional<spot_route_t> (spot_rid_t)>> resolvers;
    std::uint64_t next_spot_id = 1;
};

class spot_context_state_t
{
  public:
    bool close_now ()
    {
        if (!node) {
            return false;
        }
        std::lock_guard<std::recursive_mutex> node_lock (node->mutex);
        if (closed || actor_count != 0) {
            return false;
        }
        close_requested = false;
        if (lifecycle.on_closing && spot_instance) {
            lifecycle.on_closing (spot_instance.get ());
        }
        const auto rid = std::string (spot_rid.value ());
        node->spot_contexts_by_rid.erase (rid);
        node->spot_names_by_rid.erase (rid);
        node->native_spots_by_rid.erase (rid);
        for (auto iterator = node->spot_rids_by_name.begin ();
             iterator != node->spot_rids_by_name.end (); ++iterator) {
            if (std::string (iterator->second.value ()) == rid) {
                node->spot_rids_by_name.erase (iterator);
                break;
            }
        }
        closed = true;
        return true;
    }

    void enter_callback ();
    void leave_callback ();
    bool is_current_callback_thread () const;

    bool try_post_serial (std::string name, std::function<void ()> work);
    bool try_post_serial_async (std::string name,
                                runtime::serial_execution_queue_t::async_work_t work);
    bool run_serial_sync (std::string name, std::function<void ()> work);
    void drain_serial ();

    std::shared_ptr<spot_node_builder_state_t> node;
    std::shared_ptr<channel_runtime_state_t> channel_runtime;
    node_rid_t node_rid;
    spot_rid_t spot_rid;
    std::string spot_name;
    std::vector<spot_packet_descriptor_t> packets;
    std::vector<spot_handler_descriptor_t> handlers;
    std::vector<spot_handler_registry_t::invoker_t> handler_invokers;
    std::map<std::type_index, spot_actor_admission_callbacks_t> actor_admissions;
    std::vector<std::string> ordering_log;
    std::weak_ptr<service::spot_t> native_spot;
    std::vector<std::shared_ptr<timer_state_t>> timers;
    std::shared_ptr<void> spot_instance;
    std::shared_ptr<runtime::offload_executor_t> serial_executor;
    std::shared_ptr<runtime::serial_execution_queue_t> serial_queue;
    std::shared_ptr<worker_scheduler_t> worker_scheduler;
    spot_lifecycle_callbacks_t lifecycle;
    std::map<std::type_index, std::function<void (void *, void *)>> on_actor_joined_callbacks;
    std::map<std::type_index,
             std::function<void (void *, void *, const zlink::message_t &, serializer_registry_t &)>>
      onCreateActor_callbacks;
    std::map<std::type_index, std::function<void (void *, void *)>> onLeaveActor_callbacks;
    std::map<std::type_index, std::function<void (void *, void *)>> onDisconnectActor_callbacks;
    bool close_requested = false;
    bool closed = false;
    std::size_t actor_count = 0;
    mutable std::mutex callback_mutex;
    std::thread::id callback_thread;
    std::size_t callback_depth = 0;
};

inline void record_actor_route_unlocked (spot_node_builder_state_t &state,
                                         const std::string &key,
                                         spot_route_t route,
                                         std::uint64_t generation)
{
    state.actor_spot_rids[key] = route.spot_rid;
    state.actor_routes[key] = std::move (route);
    state.actor_generations[key] = generation;
}

inline void record_actor_spot_location_unlocked (spot_node_builder_state_t &state,
                                                 const std::string &key,
                                                 spot_rid_t spot_rid,
                                                 std::uint64_t generation)
{
    state.actor_spot_rids[key] = std::move (spot_rid);
    state.actor_routes.erase (key);
    state.actor_generations[key] = generation;
}

inline void record_actor_context_route_unlocked (spot_node_builder_state_t &state,
                                                 const std::string &key,
                                                 const std::string &node_rid,
                                                 spot_context_state_t &context_state,
                                                 std::uint64_t generation)
{
    record_actor_route_unlocked (state, key,
                                 spot_route_t{node_rid_t::from_string (node_rid),
                                              context_state.spot_rid, context_state.spot_name},
                                 generation);
    context_state.actor_count++;
}

inline std::string effective_spot_node_rid (const spot_node_snapshot_t &snapshot)
{
    if (snapshot.routing_id) {
        return snapshot.routing_id->to_string ();
    }
    return snapshot.name;
}

class spot_node_runtime_t
{
  public:
    explicit spot_node_runtime_t (std::shared_ptr<spot_node_builder_state_t> state);

    static spot_node_runtime_t from (const spot_node_builder_t &builder);
    static std::optional<spot_node_runtime_t> from (const zlink_builder_t &builder,
                                                    const std::string &spot_node_name);

    spot_create_result_t create_spot (std::string spot_name);
    spot_create_result_t create_spot (std::string spot_name, zlink::message_t request);
    spot_create_result_t get_or_create_spot (std::string spot_name, spot_rid_t spot_rid);
    spot_create_result_t
    get_or_create_spot (std::string spot_name, spot_rid_t spot_rid, zlink::message_t request);
    std::optional<spot_info_t> find_spot (spot_rid_t spot_rid) const;
    std::vector<spot_info_t> list_spots () const;
    task_t<bool> close_spot (spot_rid_t spot_rid);
    node_rid_t node_rid () const;
    std::optional<std::string> spot_name_for (spot_rid_t spot_rid) const;
    std::optional<spot_route_t> resolve_spot (spot_rid_t spot_rid) const;
    std::optional<spot_rid_t> actor_spot (const actor_ref_t &actor_ref) const;
    void record_actor_spot (const actor_ref_t &actor_ref, spot_rid_t spot_rid);
    std::optional<spot_route_t> actor_route (const actor_ref_t &actor_ref) const;
    void record_actor_route (const actor_ref_t &actor_ref, spot_route_t route);
    std::optional<actor_ref_t> current_actor_ref (const actor_ref_t &actor_ref) const;
    const std::vector<std::string> &ordering_log (const spot_context_t &context) const;
    void attach_native_node (std::shared_ptr<service::spot_node_t> node);
    void detach_native_node ();
    std::shared_ptr<service::spot_node_t> native_node () const;
    std::vector<spot_context_t> active_contexts () const;
    result_t<void> dispatch_subscription (const spot_context_t &context,
                                          std::string topic,
                                          const zlink::message_t &message,
                                          service_provider_t &services,
                                          serializer_registry_t &serializers) const;
    std::size_t drain_routed_packets (service_provider_t &services,
                                      serializer_registry_t &serializers) const;
    std::size_t drain_subscriptions (service_provider_t &services,
                                     serializer_registry_t &serializers) const;
    void on_destroy_actor (std::function<result_t<void> (const actor_ref_t &)> destroy_actor);
    void on_actor_ref_updated (std::function<result_t<void> (const actor_ref_t &)> update_actor);
    void on_actor_packet_relay (
      std::function<result_t<std::optional<zlink::message_t>> (const actor_ref_t &,
                                                               actor_context_t,
                                                               std::string_view,
                                                               const zlink::message_t &,
                                                               service_provider_t &,
                                                               serializer_registry_t &,
                                                               spot_actor_message_metadata_t)>
        relay);
    spot_node_manager_t manager () const;
    result_t<actor_join_reply_t> join_actor_to_spot_erased (const actor_ref_t &actor_ref,
                                                            spot_rid_t spot_rid,
                                                            const zlink::message_t &request);
    result_t<actor_join_reply_t>
    join_remote_actor_to_spot_erased (const actor_ref_t &actor_ref,
                                      spot_rid_t spot_rid,
                                      const zlink::message_t &request,
                                      actor_context_t actor_context = {});
    result_t<actor_join_reply_t> join_actor_to_entry_spot_erased (const actor_ref_t &actor_ref,
                                                                  node_rid_t spot_node_rid,
                                                                  const zlink::message_t &request);
    result_t<std::optional<zlink::message_t>>
    relay_actor_packet (const actor_ref_t &actor_ref,
                        actor_context_t actor_context,
                        std::string_view packet_name,
                        const zlink::message_t &message,
                        service_provider_t &services,
                        serializer_registry_t &serializers,
                        spot_actor_message_metadata_t metadata = {});
    result_t<void> notify_actor_disconnected_erased (const actor_ref_t &actor_ref) const;

    template <typename TActor>
    std::optional<std::reference_wrapper<TActor>> actor_instance (const actor_ref_t &actor_ref)
    {
        const auto found = _state->actor_instances.find (actor_key (actor_ref));
        if (found == _state->actor_instances.end () || !found->second) {
            return std::nullopt;
        }
        return *static_cast<TActor *> (found->second.get ());
    }

    template <typename TSpot, typename TActor>
    result_t<actor_join_reply_t> join_actor_to_spot (const actor_ref_t &actor_ref,
                                                     spot_rid_t spot_rid,
                                                     TActor &actor,
                                                     const zlink::message_t &request)
    {
        if (actor_ref.empty ()) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor ref is empty");
        }
        auto context = find_context (spot_rid);
        if (!context || !context->_state->spot_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "target spot is not registered");
        }
        auto &spot = *static_cast<TSpot *> (context->_state->spot_instance.get ());
        if constexpr (!has_actor_join_callback<TSpot, TActor>) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::handler_not_found,
              "spot actor join callback is not registered");
        } else {
            const auto response = invoke_actor_join_callback (spot, actor, request);
            auto &serializers = *context->_state->channel_runtime->serializers;
            if (!response.accepted) {
                return result_t<actor_join_reply_t>::success (
                  actor_join_reply_t{1, actor_ref, actor_join_reply (response, serializers)});
            }

            const auto committed =
              commit_actor_to_context<TSpot, TActor> (actor_ref, actor, *context, request);
            return result_t<actor_join_reply_t>::success (
              actor_join_reply_t{0, committed, actor_join_reply (response, serializers)});
        }
    }

    template <typename TEntrySpot, typename TActor>
    result_t<actor_join_reply_t> join_actor_to_entry_spot (const actor_ref_t &actor_ref,
                                                           node_rid_t spot_node_rid,
                                                           TActor &actor,
                                                           const zlink::message_t &request)
    {
        if (actor_ref.empty ()) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor ref is empty");
        }
        if (spot_node_rid.empty ()
            || spot_node_rid.value () != detail::effective_spot_node_rid (_state->snapshot)) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found,
              "spot node rid does not match this node");
        }
        if (!_state->snapshot.entry_spot_name) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot is not registered");
        }
        const auto entry_rid = _state->spot_rids_by_name.find (*_state->snapshot.entry_spot_name);
        if (entry_rid == _state->spot_rids_by_name.end ()) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot is not created");
        }
        auto context = find_context (entry_rid->second);
        if (!context || !context->_state->spot_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot context is not registered");
        }

        auto &spot = *static_cast<TEntrySpot *> (context->_state->spot_instance.get ());
        if constexpr (has_actor_join_callback<TEntrySpot, TActor>) {
            const auto response = invoke_actor_join_callback (spot, actor, request);
            auto &serializers = *context->_state->channel_runtime->serializers;
            if (!response.accepted) {
                return result_t<actor_join_reply_t>::success (
                  actor_join_reply_t{1, actor_ref, actor_join_reply (response, serializers)});
            }

            const auto committed =
              commit_actor_to_context<TEntrySpot, TActor> (actor_ref, actor, *context, request);
            return result_t<actor_join_reply_t>::success (
              actor_join_reply_t{0, committed, actor_join_reply (response, serializers)});
        }

        const auto committed =
          commit_actor_to_context<TEntrySpot, TActor> (actor_ref, actor, *context, request);
        return result_t<actor_join_reply_t>::success (actor_join_reply_t{0, committed, {}});
    }

    template <typename TActor>
    result_t<void> leaveActor (const actor_ref_t &actor_ref, TActor &actor)
    {
        if (actor_ref.empty ()) {
            return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                            "actor ref is empty");
        }
        commit_actor_left<TActor> (actor_ref, actor);
        return result_t<void>::success ();
    }

    template <typename TActor>
    result_t<void> notify_onDisconnectActor (const actor_ref_t &actor_ref, TActor &actor)
    {
        if (actor_ref.empty ()) {
            return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                            "actor ref is empty");
        }
        const auto key = actor_key (actor_ref);
        const auto found_location = _state->actor_spot_rids.find (key);
        if (found_location == _state->actor_spot_rids.end ()) {
            return result_t<void>::success ();
        }
        const auto found_generation = _state->actor_generations.find (key);
        if (found_generation != _state->actor_generations.end ()
            && found_generation->second != actor_ref.generation ()) {
            return result_t<void>::failure (framework_error_kind_t::actor_stale_generation,
                                            "actor generation is stale");
        }
        auto context = find_context (found_location->second);
        if (!context) {
            return result_t<void>::success ();
        }
        try {
            notify_onDisconnectActor<TActor> (*context->_state, actor);
            return result_t<void>::success ();
        }
        catch (const framework_exception_t &error) {
            return result_t<void>::failure (error.kind (), error.what (), error.is_retriable ());
        }
        catch (const std::exception &error) {
            return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
        }
        catch (...) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "spot actor disconnected callback failed");
        }
    }

  private:
    template <typename TSpot, typename TActor>
    static constexpr bool has_framework_actor_join_callback =
      requires (TSpot & spot, TActor &actor, const message_t &request)
    {
        {
            spot.on_actor_join (actor, request)
        } -> std::same_as<spot_actor_join_response_t>;
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_raw_actor_join_callback =
      requires (TSpot & spot, TActor &actor, const zlink::message_t &request)
    {
        {
            spot.on_actor_join (actor, request)
        } -> std::same_as<spot_actor_join_response_t>;
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_actor_join_callback = has_framework_actor_join_callback<TSpot, TActor>
                                                    || has_raw_actor_join_callback<TSpot, TActor>;

    template <typename TSpot, typename TActor>
    spot_actor_join_response_t
    invoke_actor_join_callback (TSpot &spot, TActor &actor, const zlink::message_t &request)
    {
        if constexpr (has_framework_actor_join_callback<TSpot, TActor>) {
            return spot.on_actor_join (actor, message_t::from_raw (request, _state->channel_runtime->serializers));
        } else {
            return spot.on_actor_join (actor, request);
        }
    }

    static zlink::message_t actor_join_reply (const spot_actor_join_response_t &response,
                                              serializer_registry_t &serializers)
    {
        return response.reply ? response.reply->to_raw (serializers) : zlink::message_t{};
    }

    template <typename TSpot, typename TActor>
    static constexpr bool has_on_actor_joined_callback = requires (TSpot & spot, TActor &actor)
    {
        spot.on_actor_joined (actor);
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_onCreateActor_callback = requires (TSpot & spot, TActor &actor)
    {
        spot.onCreateActor (actor);
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_framework_payload_onCreateActor_callback =
      requires (TSpot & spot, TActor &actor, const message_t &request)
    {
        spot.onCreateActor (actor, request);
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_raw_payload_onCreateActor_callback =
      requires (TSpot & spot, TActor &actor, const zlink::message_t &request)
    {
        spot.onCreateActor (actor, request);
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_onLeaveActor_callback = requires (TSpot & spot, TActor &actor)
    {
        spot.onLeaveActor (actor);
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_onDisconnectActor_callback = requires (TSpot & spot, TActor &actor)
    {
        spot.onDisconnectActor (actor);
    };

    static std::string actor_key (const actor_ref_t &actor_ref)
    {
        return std::string (actor_ref.actor_type ()) + ":" + std::string (actor_ref.actor_id ());
    }

    std::optional<spot_context_t> find_context (const spot_rid_t &spot_rid) const
    {
        const auto found = _state->spot_contexts_by_rid.find (std::string (spot_rid.value ()));
        if (found == _state->spot_contexts_by_rid.end ()) {
            return std::nullopt;
        }
        return found->second;
    }

    template <typename TSpot, typename TActor>
    actor_ref_t commit_actor_to_context (const actor_ref_t &actor_ref,
                                         TActor &actor,
                                         spot_context_t &context,
                                         const zlink::message_t &create_request)
    {
        commit_actor_left<TActor> (actor_ref, actor);
        auto &context_state = *context._state;
        const auto key = actor_key (actor_ref);
        record_actor_context_route_unlocked (
          *_state, key, effective_spot_node_rid (_state->snapshot), context_state,
          actor_ref.generation () + 1);
        context_state.on_actor_joined_callbacks[std::type_index (typeid (TActor))] =
          [] (void *spot, void *actor) {
              if constexpr (has_on_actor_joined_callback<TSpot, TActor>) {
                  static_cast<TSpot *> (spot)->on_actor_joined (*static_cast<TActor *> (actor));
              }
          };
        context_state.onCreateActor_callbacks[std::type_index (typeid (TActor))] =
          [] (void *spot, void *actor, const zlink::message_t &request,
              serializer_registry_t &serializers) {
              if constexpr (std::is_base_of_v<entry_spot_t, TSpot>
                            && has_framework_payload_onCreateActor_callback<TSpot, TActor>) {
                  static_cast<TSpot *> (spot)->onCreateActor (
                    *static_cast<TActor *> (actor), message_t::from_raw (request, &serializers));
              } else if constexpr (std::is_base_of_v<entry_spot_t, TSpot>
                                   && has_raw_payload_onCreateActor_callback<TSpot, TActor>) {
                  static_cast<TSpot *> (spot)->onCreateActor (*static_cast<TActor *> (actor),
                                                              request);
              } else if constexpr (std::is_base_of_v<entry_spot_t, TSpot>
                            && has_onCreateActor_callback<TSpot, TActor>) {
                  static_cast<TSpot *> (spot)->onCreateActor (*static_cast<TActor *> (actor));
              }
          };
        context_state.onLeaveActor_callbacks[std::type_index (typeid (TActor))] = [] (void *spot,
                                                                                      void *actor) {
            if constexpr (has_onLeaveActor_callback<TSpot, TActor>) {
                static_cast<TSpot *> (spot)->onLeaveActor (*static_cast<TActor *> (actor));
            }
        };
        context_state.onDisconnectActor_callbacks[std::type_index (typeid (TActor))] =
          [] (void *spot, void *actor) {
              if constexpr (has_onDisconnectActor_callback<TSpot, TActor>) {
                  static_cast<TSpot *> (spot)->onDisconnectActor (*static_cast<TActor *> (actor));
              }
          };
        auto committed = actor_ref_t (
          node_rid_t::from_string (effective_spot_node_rid (_state->snapshot)),
          std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()),
          actor_ref.generation () + 1);
        if constexpr (std::is_base_of_v<entry_spot_t, TSpot>) {
            if (_state->actor_created_keys.insert (key).second) {
                notify_onCreateActor<TActor> (context_state, actor, create_request);
            }
        }
        notify_on_actor_joined<TActor> (context_state, actor);
        return committed;
    }

    template <typename TActor> void commit_actor_left (const actor_ref_t &actor_ref, TActor &actor)
    {
        const auto key = actor_key (actor_ref);
        const auto found_location = _state->actor_spot_rids.find (key);
        if (found_location == _state->actor_spot_rids.end ()) {
            return;
        }
        auto previous_context = find_context (found_location->second);
        _state->actor_spot_rids.erase (found_location);
        _state->actor_routes.erase (key);
        _state->actor_generations.erase (key);
        if (!previous_context) {
            return;
        }
        auto &state = *previous_context->_state;
        if (state.actor_count > 0) {
            state.actor_count--;
        }
        notify_onLeaveActor<TActor> (state, actor);
    }

    template <typename TActor>
    void notify_onCreateActor (spot_context_state_t &state,
                               TActor &actor,
                               const zlink::message_t &request)
    {
        const auto found = state.onCreateActor_callbacks.find (std::type_index (typeid (TActor)));
        if (found != state.onCreateActor_callbacks.end () && state.spot_instance) {
            if (!state.channel_runtime || !state.channel_runtime->serializers) {
                throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                             "spot create actor requires a serializer registry");
            }
            if (!state.run_serial_sync ("spot-lifecycle-create", [&] {
                    found->second (state.spot_instance.get (), &actor, request,
                                   *state.channel_runtime->serializers);
                })) {
                throw framework_exception_t (framework_error_kind_t::request_rejected,
                                             "spot serial queue is full");
            }
        }
    }

    template <typename TActor>
    void notify_on_actor_joined (spot_context_state_t &state, TActor &actor)
    {
        const auto found = state.on_actor_joined_callbacks.find (std::type_index (typeid (TActor)));
        if (found != state.on_actor_joined_callbacks.end () && state.spot_instance) {
            if (!state.run_serial_sync ("spot-lifecycle-join", [&] {
                    found->second (state.spot_instance.get (), &actor);
                })) {
                throw framework_exception_t (framework_error_kind_t::request_rejected,
                                             "spot serial queue is full");
            }
        }
    }

    template <typename TActor> void notify_onLeaveActor (spot_context_state_t &state, TActor &actor)
    {
        const auto found = state.onLeaveActor_callbacks.find (std::type_index (typeid (TActor)));
        if (found != state.onLeaveActor_callbacks.end () && state.spot_instance) {
            if (!state.run_serial_sync ("spot-lifecycle-leave", [&] {
                    found->second (state.spot_instance.get (), &actor);
                })) {
                throw framework_exception_t (framework_error_kind_t::request_rejected,
                                             "spot serial queue is full");
            }
        }
    }

    template <typename TActor>
    void notify_onDisconnectActor (spot_context_state_t &state, TActor &actor)
    {
        const auto found =
          state.onDisconnectActor_callbacks.find (std::type_index (typeid (TActor)));
        if (found != state.onDisconnectActor_callbacks.end () && state.spot_instance) {
            if (!state.run_serial_sync ("spot-lifecycle-disconnect", [&] {
                    found->second (state.spot_instance.get (), &actor);
                })) {
                throw framework_exception_t (framework_error_kind_t::request_rejected,
                                             "spot serial queue is full");
            }
        }
    }

    spot_create_result_t create_spot_context_unlocked (std::string spot_name,
                                                       spot_rid_t spot_rid,
                                                       zlink::message_t request);
    result_t<spot_context_t> actor_join_context_unlocked (spot_rid_t spot_rid,
                                                          const zlink::message_t &request);
    result_t<std::reference_wrapper<spot_node_builder_state_t::actor_factory_registration_t>>
    actor_factory_unlocked (const actor_ref_t &actor_ref) const;
    result_t<std::reference_wrapper<spot_actor_admission_callbacks_t>>
    actor_admission_unlocked (spot_context_t &context,
                              std::type_index actor_type,
                              spot_rid_t spot_rid,
                              const actor_ref_t &actor_ref);
    void leave_previous_actor_route_unlocked (const std::string &key,
                                              std::type_index actor_type,
                                              void *actor);
    void commit_accepted_actor_join_unlocked (const std::string &key,
                                              spot_context_t &context,
                                              const actor_ref_t &committed,
                                              std::type_index actor_type,
                                              void *actor,
                                              const spot_actor_admission_callbacks_t &admission,
                                              bool create_entry_actor,
                                              const zlink::message_t &create_request);

    std::shared_ptr<spot_node_builder_state_t> _state;
};

} // namespace zlink::framework::detail
