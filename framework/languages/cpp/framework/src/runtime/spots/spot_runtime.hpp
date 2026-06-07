/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/actors/actor.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace zlink::framework::detail
{

class spot_node_builder_state_t
{
  public:
    explicit spot_node_builder_state_t (std::string name) : snapshot{.name = std::move (name)} {}

    spot_node_snapshot_t snapshot;
    std::map<std::string, std::type_index> spot_factories;
    std::map<std::string, spot_lifecycle_callbacks_t> spot_lifecycles;
    std::map<std::string, spot_rid_t> spot_rids_by_name;
    std::map<std::string, std::string> spot_names_by_rid;
    std::map<std::string, spot_context_t> spot_contexts_by_rid;
    std::map<std::string, spot_rid_t> actor_spot_rids;
    std::map<std::string, std::type_index> actor_factories;
    std::map<std::string, std::function<std::optional<spot_route_t> (spot_rid_t)>> resolvers;
    std::uint64_t next_spot_id = 1;
};

class spot_context_state_t
{
  public:
    bool close_now ()
    {
        if (!node || closed || actor_count != 0) {
            return false;
        }
        close_requested = false;
        if (lifecycle.on_closing && spot_instance) {
            lifecycle.on_closing (spot_instance.get ());
        }
        const auto rid = std::string (spot_rid.value ());
        node->spot_contexts_by_rid.erase (rid);
        node->spot_names_by_rid.erase (rid);
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

    void enter_callback () { callback_depth++; }

    void leave_callback ()
    {
        if (callback_depth > 0) {
            callback_depth--;
        }
        if (callback_depth == 0 && close_requested) {
            (void) close_now ();
        }
    }

    std::shared_ptr<spot_node_builder_state_t> node;
    node_rid_t node_rid;
    spot_rid_t spot_rid;
    std::string spot_name;
    std::vector<spot_packet_descriptor_t> packets;
    std::vector<spot_handler_descriptor_t> handlers;
    std::vector<spot_handler_registry_t::invoker_t> handler_invokers;
    std::vector<std::string> ordering_log;
    std::vector<std::shared_ptr<timer_state_t>> timers;
    std::shared_ptr<void> spot_instance;
    spot_lifecycle_callbacks_t lifecycle;
    std::map<std::type_index, std::function<void (void *, void *)>> post_actor_joined_callbacks;
    std::map<std::type_index, std::function<void (void *, void *)>> actor_left_callbacks;
    bool close_requested = false;
    bool closed = false;
    std::size_t actor_count = 0;
    std::size_t callback_depth = 0;
};

class spot_node_runtime_t
{
  public:
    explicit spot_node_runtime_t (std::shared_ptr<spot_node_builder_state_t> state);

    static spot_node_runtime_t from (const spot_node_builder_t &builder);

    spot_create_result_t create_spot (std::string spot_name);
    spot_create_result_t create_spot (std::string spot_name, zlink::message_t request);
    spot_create_result_t get_or_create_spot (std::string spot_name, spot_rid_t spot_rid);
    spot_create_result_t
    get_or_create_spot (std::string spot_name, spot_rid_t spot_rid, zlink::message_t request);
    std::optional<spot_info_t> find_spot (spot_rid_t spot_rid) const;
    std::vector<spot_info_t> list_spots () const;
    task_t<bool> close_spot (spot_rid_t spot_rid);
    std::optional<std::string> spot_name_for (spot_rid_t spot_rid) const;
    std::optional<spot_route_t> resolve_spot (spot_rid_t spot_rid) const;
    const std::vector<std::string> &ordering_log (const spot_context_t &context) const;

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
            const auto response = spot.on_actor_join (actor, request);
            if (!response.accepted) {
                return result_t<actor_join_reply_t>::success (
                  actor_join_reply_t{1, actor_ref, response.reply.value_or (zlink::message_t{})});
            }

            const auto committed =
              commit_actor_to_context<TSpot, TActor> (actor_ref, actor, *context);
            return result_t<actor_join_reply_t>::success (
              actor_join_reply_t{0, committed, response.reply.value_or (zlink::message_t{})});
        }
    }

    template <typename TEntrySpot, typename TActor>
    result_t<actor_ref_t>
    join_actor_to_entry_spot (const actor_ref_t &actor_ref, node_rid_t spot_node_rid, TActor &actor)
    {
        if (actor_ref.empty ()) {
            return result_t<actor_ref_t>::failure (framework_error_kind_t::actor_route_not_found,
                                                   "actor ref is empty");
        }
        if (spot_node_rid.empty () || spot_node_rid.value () != _state->snapshot.name) {
            return result_t<actor_ref_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                   "spot node rid does not match this node");
        }
        if (!_state->snapshot.entry_spot_name) {
            return result_t<actor_ref_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                   "entry spot is not registered");
        }
        const auto entry_rid = _state->spot_rids_by_name.find (*_state->snapshot.entry_spot_name);
        if (entry_rid == _state->spot_rids_by_name.end ()) {
            return result_t<actor_ref_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                   "entry spot is not created");
        }
        auto context = find_context (entry_rid->second);
        if (!context || !context->_state->spot_instance) {
            return result_t<actor_ref_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                   "entry spot context is not registered");
        }

        const auto committed =
          commit_actor_to_context<TEntrySpot, TActor> (actor_ref, actor, *context);
        return result_t<actor_ref_t>::success (committed);
    }

    template <typename TActor>
    result_t<void> leave_actor (const actor_ref_t &actor_ref, TActor &actor)
    {
        if (actor_ref.empty ()) {
            return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                            "actor ref is empty");
        }
        commit_actor_left<TActor> (actor_ref, actor);
        return result_t<void>::success ();
    }

  private:
    template <typename TSpot, typename TActor>
    static constexpr bool has_actor_join_callback =
      requires (TSpot & spot, TActor &actor, const zlink::message_t &request)
    {
        {
            spot.on_actor_join (actor, request)
        } -> std::same_as<spot_actor_join_response_t>;
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_post_actor_joined_callback = requires (TSpot & spot, TActor &actor)
    {
        spot.on_post_actor_joined (actor);
    };

    template <typename TSpot, typename TActor>
    static constexpr bool has_actor_left_callback = requires (TSpot & spot, TActor &actor)
    {
        spot.on_actor_left (actor);
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
    actor_ref_t
    commit_actor_to_context (const actor_ref_t &actor_ref, TActor &actor, spot_context_t &context)
    {
        commit_actor_left<TActor> (actor_ref, actor);
        auto &context_state = *context._state;
        const auto key = actor_key (actor_ref);
        _state->actor_spot_rids[key] = context_state.spot_rid;
        context_state.actor_count++;
        context_state.post_actor_joined_callbacks[std::type_index (typeid (TActor))] =
          [] (void *spot, void *actor) {
              if constexpr (has_post_actor_joined_callback<TSpot, TActor>) {
                  static_cast<TSpot *> (spot)->on_post_actor_joined (
                    *static_cast<TActor *> (actor));
              }
          };
        context_state.actor_left_callbacks[std::type_index (typeid (TActor))] = [] (void *spot,
                                                                                    void *actor) {
            if constexpr (has_actor_left_callback<TSpot, TActor>) {
                static_cast<TSpot *> (spot)->on_actor_left (*static_cast<TActor *> (actor));
            }
        };
        auto committed = actor_ref_t (
          node_rid_t::from_string (_state->snapshot.name), std::string (actor_ref.actor_type ()),
          std::string (actor_ref.actor_id ()), actor_ref.generation () + 1);
        notify_post_actor_joined<TActor> (context_state, actor);
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
        if (!previous_context) {
            return;
        }
        auto &state = *previous_context->_state;
        if (state.actor_count > 0) {
            state.actor_count--;
        }
        notify_actor_left<TActor> (state, actor);
    }

    template <typename TActor>
    void notify_post_actor_joined (spot_context_state_t &state, TActor &actor)
    {
        const auto found =
          state.post_actor_joined_callbacks.find (std::type_index (typeid (TActor)));
        if (found != state.post_actor_joined_callbacks.end () && state.spot_instance) {
            found->second (state.spot_instance.get (), &actor);
        }
    }

    template <typename TActor> void notify_actor_left (spot_context_state_t &state, TActor &actor)
    {
        const auto found = state.actor_left_callbacks.find (std::type_index (typeid (TActor)));
        if (found != state.actor_left_callbacks.end () && state.spot_instance) {
            found->second (state.spot_instance.get (), &actor);
        }
    }

    std::shared_ptr<spot_node_builder_state_t> _state;
};

} // namespace zlink::framework::detail
