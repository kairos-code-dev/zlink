/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework/contracts/actors/actor.hpp>

#include "runtime/mesh/mesh_node_runtime.hpp"
#include "runtime/actors/actor_manager_access.hpp"
#include "runtime/execution/actor_execution_context.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/request_failure_mapper.hpp"
#include "runtime/locations/store_location_resolvers.hpp"
#include "runtime/locations/actor_authority_payload.hpp"

#include <zlink/framework/contracts/locations/stores.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class actor_manager_state_t
{
  public:
    actor_manager_access_t::create_fn_t create_actor;
    actor_manager_access_t::find_fn_t find_actor;
    actor_manager_access_t::find_spot_fn_t find_spot;
    actor_manager_access_t::destroy_fn_t destroy_actor;
    std::atomic<std::uint64_t> operation_sequence{1};
};

class actor_create_call_state_t
{
  public:
    std::shared_ptr<actor_manager_state_t> manager;
    bool exclusive = false;
    actor_id_t actor_id;
    std::string stable_type;
    std::optional<std::string> mesh_name;
    std::optional<message_t> request;
    std::chrono::milliseconds timeout{std::chrono::seconds (30)};
    bool mesh_set = false;
    bool request_set = false;
    bool timeout_set = false;
    bool submitted = false;
    std::mutex mutex;
};
} // namespace detail

namespace
{
void require_call_option (bool &flag, const char *name)
{
    if (flag)
        throw framework_exception_t (
          framework_error_kind_t::invalid_configuration,
          std::string ("Actor create option was set more than once: ") + name);
    flag = true;
}
} // namespace

actor_create_call_t::actor_create_call_t (
  std::shared_ptr<detail::actor_create_call_state_t> state) :
    _state (std::move (state))
{
}

actor_create_call_t::~actor_create_call_t () = default;
actor_create_call_t::actor_create_call_t (actor_create_call_t &&) noexcept = default;
actor_create_call_t &
actor_create_call_t::operator= (actor_create_call_t &&) noexcept = default;

actor_create_call_t &actor_create_call_t::in_mesh (std::string mesh_name)
{
    std::lock_guard lock (_state->mutex);
    require_call_option (_state->mesh_set, "in_mesh");
    _state->mesh_name = std::move (mesh_name);
    return *this;
}

actor_create_call_t &actor_create_call_t::creation_request (message_t request)
{
    std::lock_guard lock (_state->mutex);
    require_call_option (_state->request_set, "creation_request");
    _state->request = std::move (request);
    return *this;
}

actor_create_call_t &
actor_create_call_t::timeout (std::chrono::milliseconds timeout)
{
    std::lock_guard lock (_state->mutex);
    require_call_option (_state->timeout_set, "timeout");
    if (timeout <= std::chrono::milliseconds::zero ())
        throw framework_exception_t (
          framework_error_kind_t::invalid_configuration,
          "Actor create timeout must be positive");
    _state->timeout = timeout;
    return *this;
}

task_t<actor_create_result_t> actor_create_call_t::submit ()
{
    std::lock_guard lock (_state->mutex);
    if (_state->submitted)
        return task_t<actor_create_result_t> (
          result_t<actor_create_result_t>::failure (
            framework_error_kind_t::already_submitted,
            "Actor create call was already submitted"));
    _state->submitted = true;
    if (!_state->manager || !_state->manager->create_actor)
        return task_t<actor_create_result_t> (
          result_t<actor_create_result_t>::failure (
            framework_error_kind_t::object_client_not_configured,
            "Actor manager is not bound to an object runtime"));
    const auto sequence =
      _state->manager->operation_sequence.fetch_add (1);
    const creation_operation_id_t operation{
      static_cast<std::uint64_t> (
        reinterpret_cast<std::uintptr_t> (
          _state->manager.get ())),
      sequence};
    return _state->manager->create_actor (
      _state->exclusive, _state->actor_id, _state->stable_type,
      _state->mesh_name, _state->request,
      _state->timeout, operation);
}

actor_manager_t::actor_manager_t () :
    _state (std::make_shared<detail::actor_manager_state_t> ())
{
}

actor_manager_t::actor_manager_t (
  std::shared_ptr<detail::actor_manager_state_t> state) :
    _state (std::move (state))
{
}

actor_manager_t::~actor_manager_t () = default;
actor_manager_t::actor_manager_t (actor_manager_t &&) noexcept = default;
actor_manager_t &actor_manager_t::operator= (actor_manager_t &&) noexcept = default;

actor_create_call_t actor_manager_t::create (
  actor_id_t actor_id, std::string stable_type)
{
    auto state = std::make_shared<detail::actor_create_call_state_t> ();
    state->manager = _state;
    state->exclusive = true;
    state->actor_id = std::move (actor_id);
    state->stable_type = std::move (stable_type);
    return actor_create_call_t (std::move (state));
}

actor_create_call_t actor_manager_t::get_or_create (
  actor_id_t actor_id, std::string stable_type)
{
    auto state = std::make_shared<detail::actor_create_call_state_t> ();
    state->manager = _state;
    state->actor_id = std::move (actor_id);
    state->stable_type = std::move (stable_type);
    return actor_create_call_t (std::move (state));
}

task_t<std::optional<actor_ref_t>>
actor_manager_t::find (actor_id_t actor_id) const
{
    if (!_state || !_state->find_actor)
        return task_t<std::optional<actor_ref_t>> (
          result_t<std::optional<actor_ref_t>>::failure (
            framework_error_kind_t::object_client_not_configured,
            "Actor manager is not bound to an object runtime"));
    return _state->find_actor (std::move (actor_id));
}

task_t<std::optional<spot_ref_t>>
actor_manager_t::find_spot (actor_id_t actor_id) const
{
    if (!_state || !_state->find_spot)
        return task_t<std::optional<spot_ref_t>> (
          result_t<std::optional<spot_ref_t>>::failure (
            framework_error_kind_t::object_client_not_configured,
            "Actor manager is not bound to an object runtime"));
    return _state->find_spot (std::move (actor_id));
}

task_t<bool> actor_manager_t::destroy (actor_ref_t actor)
{
    if (!_state || !_state->destroy_actor)
        return task_t<bool> (result_t<bool>::failure (
          framework_error_kind_t::object_client_not_configured,
          "Actor manager is not bound to an object runtime"));
    return _state->destroy_actor (std::move (actor));
}

actor_manager_t detail::actor_manager_access_t::create (
  create_fn_t create_actor,
  find_fn_t find_actor,
  find_spot_fn_t find_spot,
  destroy_fn_t destroy_actor)
{
    auto state = std::make_shared<actor_manager_state_t> ();
    state->create_actor = std::move (create_actor);
    state->find_actor = std::move (find_actor);
    state->find_spot = std::move (find_spot);
    state->destroy_actor = std::move (destroy_actor);
    return actor_manager_t (std::move (state));
}

actor_send_call_t::actor_send_call_t (actor_client_t &client,
                                      actor_ref_t actor_ref,
                                      std::string packet_name,
                                      message_t message) :
    _client (&client),
    _actor_ref (std::move (actor_ref)),
    _packet_name (std::move (packet_name)),
    _message (std::move (message))
{
}

actor_send_call_t &actor_send_call_t::metadata (std::string key, std::string value)
{
    _metadata[std::move (key)] = std::move (value);
    return *this;
}

task_t<void> actor_send_call_t::submit ()
{
    if (!_submission->try_claim ()) {
        return task_t<void> (result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "actor send call has already been submitted"));
    }
    if (_client == nullptr) {
        return task_t<void> (result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "actor send call is not bound to an actor client"));
    }
    return detail::submit_one_way_task (
      [client = _client, actor_ref = _actor_ref, packet_name = _packet_name,
       message = _message, metadata = _metadata] () mutable {
          return client->send_to_actor_erased (actor_ref, packet_name, message, metadata)
            .result ();
      });
}

actor_request_call_t::actor_request_call_t (actor_client_t &client,
                                            actor_ref_t actor_ref,
                                            std::string packet_name,
                                            message_t request) :
    _client (&client),
    _actor_ref (std::move (actor_ref)),
    _packet_name (std::move (packet_name)),
    _request (std::move (request))
{
}

actor_request_call_t &actor_request_call_t::timeout (std::chrono::milliseconds timeout)
{
    _timeout = timeout;
    return *this;
}

task_t<message_t> actor_request_call_t::submit_message ()
{
    return start (false);
}

task_t<message_t> actor_request_call_t::yield_message ()
{
    return start (true);
}

task_t<message_t> actor_request_call_t::start (bool release_turn)
{
    if (release_turn && !detail::current_serial_turn_allows_yield ()) {
        return detail::unsupported_yield_task<message_t> ();
    }
    const auto target_key = std::string (_actor_ref.actor_type ())
                            + ":" + std::string (_actor_ref.actor_id ());
    if (!runtime::current_actor_execution.actor_key.empty ()
        && runtime::current_actor_execution.actor_key == target_key) {
        return task_t<message_t> (result_t<message_t>::failure (
          framework_error_kind_t::invalid_configuration,
          "awaited request to the current Actor cannot complete while its FIFO claim is held"));
    }
    auto task = _client->request_to_actor_erased (std::move (_actor_ref), std::move (_packet_name),
                                                  std::move (_request), _timeout);
    auto turn_plan = detail::prepare_serial_turn_await (release_turn);
    if (!turn_plan) {
        return task;
    }
    return detail::reschedule_task (std::move (task), std::move (turn_plan->scheduler));
}

serializer_registry_t &actor_request_call_t::serializers () const
{
    return _client->actor_client_serializers ();
}

} // namespace zlink::framework

namespace zlink::framework::runtime
{

namespace
{

result_t<messaging::message_parts_t> wait_for_actor_completion (
  detail::mesh_node_runtime_t &node,
  const detail::host::operation_id_t &operation_id,
  std::chrono::milliseconds timeout)
{
    auto completion = node.wait_for_completion (operation_id, timeout);
    if (!completion) {
        return result_t<messaging::message_parts_t>::failure (
          completion.error_kind (),
          completion.error () ? completion.error ()->what () : "actor request timed out",
          true);
    }
    if (completion.value ().record.terminal_result != 0) {
        return result_t<messaging::message_parts_t>::failure (
          framework_error_kind_t::request_failed,
          "actor request completed with terminal result "
            + std::to_string (completion.value ().record.terminal_result)
            + " (errno "
            + std::to_string (completion.value ().record.failure_errno) + ")");
    }
    return result_t<messaging::message_parts_t>::success (
      messaging::message_parts_t (std::move (completion.value ().parts)));
}

} // namespace

class actor_client_impl_t final : public actor_client_t
{
  public:
    actor_client_impl_t (live_location_reader_t &store,
                         serializer_registry_t &serializers,
                         std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> mesh_nodes,
                         std::shared_ptr<actor_location_observer_t> actor_locations,
                         location_options_t options) :
        _store (&store),
        _serializers (&serializers),
        _mesh_nodes (std::move (mesh_nodes)),
        _actor_locations (std::move (actor_locations)),
        _location_options (std::move (options))
    {
    }

  protected:
    task_t<void> send_to_actor_erased (actor_ref_t actor_ref,
                                       std::string packet_name,
                                       message_t message,
                                       const actor_send_call_t::metadata_map_t &metadata) override
    {
        auto runtime = first_mesh_node ();
        if (!runtime) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::route_not_connected,
              "actor send requires a running MeshNode", true));
        }
        auto actor = resolve_explicit_actor (actor_ref);
        auto current = resolve_actor (
          std::string (actor_ref.actor_id ()), stale_policy_t::route_not_found);
        if (current
            && (current.value ().framework_ref.node_rid ().value ()
                  != actor_ref.node_rid ().value ()
                || current.value ().framework_ref.generation ()
                     != actor_ref.generation ())) {
            const auto followed = runtime->follow_relocated_actor (actor_ref);
            if (!followed) {
                return task_t<void> (result_t<void>::failure (
                  framework_error_kind_t::actor_location_stale,
                  "actor ref is outside the Message Follow duration"));
            }
            actor = resolve_explicit_actor (*followed);
        }
        if (!actor) {
            return task_t<void> (result_t<void>::failure (
              actor.error_kind (),
              actor.error () ? actor.error ()->what () : "actor route was not found",
              actor.error () && actor.error ()->is_retriable ()));
        }
        return task_t<void> (
          submit_send (actor.value (), std::move (packet_name), std::move (message), metadata));
    }

    task_t<message_t> request_to_actor_erased (
      actor_ref_t actor_ref,
      std::string packet_name,
      message_t request,
      std::optional<std::chrono::milliseconds> timeout) override
    {
        if (detail::current_serial_turn_allows_yield ()
            && !runtime::current_actor_execution.spot_id.empty ()) {
            const auto target =
              resolve_actor (std::string (actor_ref.actor_id ()),
                             stale_policy_t::route_not_found);
            if (target
                && target.value ().spot_id
                     == runtime::current_actor_execution.spot_id) {
                co_return result_t<message_t>::failure (
                  framework_error_kind_t::invalid_configuration,
                  "awaited request requires the current Spot execution gate");
            }
        }
        // In-flight handoff (spot-actor.ko.md 10.2-5): a request that lands
        // while the actor is moving fails fast as retriable, and the sender
        // re-resolves and retries. The caller's timeout keeps running across
        // retries — the move does not reset it (10.5-2).
        const auto actor_id = std::string (actor_ref.actor_id ());
        const auto caller_node_rid =
          actor_ref.node_rid ().empty () ? std::string{}
                                         : std::string (actor_ref.node_rid ().value ());
        const auto caller_generation = actor_ref.generation ();
        // Stable across every retry and the commit replay so the target
        // dispatches this request exactly once (§10.2-1). Scoped by the client
        // instance so ids do not collide across nodes.
        const auto request_id =
          _request_id_prefix + std::to_string (_request_id_seq.fetch_add (1));
        const auto budget = timeout.value_or (_default_timeout);
        const auto deadline = std::chrono::steady_clock::now () + budget;
        auto policy = stale_policy_t::route_not_found;
        bool first_resolve = true;
        bool ref_was_current = false;
        result_t<message_t> last = result_t<message_t>::failure (
          framework_error_kind_t::actor_location_stale, "actor location is stale", true);
        // The loop only ever retries a "transfer is in progress" stale (the actor
        // is mid-move and re-resolving will land the committed location). If such
        // a request never lands within the budget it reports a plain timeout —
        // the actor was reachable, just still moving (config-10 ST-F6). Any other
        // stale is terminal and already returned from the loop body below.
        const auto on_deadline = [] () -> result_t<message_t> {
            return detail::boundary_failure<message_t> (detail::boundary_error_t::timed_out,
                                                 "actor request timed out", true);
        };
        // A stale means "retry" only while the actor is moving/committing; a
        // terminally wrong record (e.g. the generation does not match, config-9
        // TA-B2) re-resolves to the same answer, so it is returned immediately as
        // actor_location_stale rather than spun on until the deadline. The moving
        // stale is the only one whose message says "transfer is in progress" — the
        // retriable flag does not survive the actor-mesh reply.
        const auto is_moving_stale = [] (const result_t<message_t> &result) {
            if (result || !is_stale_actor_error (result.error_kind ())) {
                return false;
            }
            const auto *error = result.error ();
            return error != nullptr && error->what () != nullptr
                   && std::string_view (error->what ()).find ("transfer is in progress")
                        != std::string_view::npos;
        };
        while (true) {
            auto actor = resolve_actor (actor_id, policy);
            if (actor) {
                const bool ref_matches =
                  caller_node_rid.empty ()
                  || (actor.value ().node_rid.value () == caller_node_rid
                      && actor.value ().framework_ref.generation () == caller_generation);
                if (first_resolve) {
                    // Whether the ref was current when the request was issued
                    // decides the two in-flight cases (spot-actor.ko.md §10.2-5/6):
                    //   - ref already stale at issue (actor is elsewhere) → this is
                    //     a cross-node delayed message; after Message Follow it
                    //     fails fast so the sender re-resolves (§10.2-6, ST-F4/F5).
                    //   - ref current at issue but the actor moves mid-request →
                    //     follow it to the committed location so the reply still
                    //     correlates to this caller (§10.5 in-flight, ST-F6).
                    ref_was_current = ref_matches;
                    first_resolve = false;
                }
                if (!ref_was_current && !ref_matches) {
                    co_return result_t<message_t>::failure (
                      framework_error_kind_t::actor_location_stale,
                      "actor ref is stale: current node/generation='"
                        + std::string (actor.value ().node_rid.value ()) + "/"
                        + std::to_string (actor.value ().framework_ref.generation ())
                        + "', supplied node/generation='" + caller_node_rid + "/"
                        + std::to_string (caller_generation) + "'. actor=" + actor_id,
                      false);
                }
                const auto now = std::chrono::steady_clock::now ();
                if (now >= deadline) {
                    co_return on_deadline ();
                }
                const auto remaining =
                  std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now);
                last = submit_request (actor.value (), packet_name, request, remaining, request_id);
                if (!is_moving_stale (last)) {
                    co_return last;
                }
            } else if (!actor.error () || !actor.error ()->is_retriable ()) {
                co_return result_t<message_t>::failure (
                  actor.error_kind (),
                  actor.error () ? actor.error ()->what () : "actor route was not found",
                  actor.error () && actor.error ()->is_retriable ());
            }
            policy = stale_policy_t::location_stale;
            if (std::chrono::steady_clock::now () + std::chrono::milliseconds (50) >= deadline) {
                co_return on_deadline ();
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
    }

    serializer_registry_t &actor_client_serializers () override { return *_serializers; }

  private:
    enum class stale_policy_t
    {
        route_not_found,
        location_stale
    };

    struct resolved_actor_t
    {
        actor_ref_t framework_ref;
        actor_ref_t native_ref;
        node_rid_t node_rid;
        spot_id_t spot_id;
    };

    result_t<resolved_actor_t> resolve_explicit_actor (const actor_ref_t &actor_ref)
    {
        if (actor_ref.empty () || actor_ref.node_rid ().empty ()) {
            return result_t<resolved_actor_t>::failure (
              framework_error_kind_t::actor_route_not_found,
              "actor send requires a non-empty actor ref and node rid");
        }
        return result_t<resolved_actor_t>::success (
          resolved_actor_t{
            actor_ref, actor_ref, actor_ref.node_rid (),
            spot_id_t (std::string (actor_ref.node_rid ().value ())) });
    }

    result_t<resolved_actor_t> resolve_actor (const std::string &actor_id, stale_policy_t policy)
    {
        const auto runtime = first_mesh_node ();
        if (!runtime) {
            return result_t<resolved_actor_t>::failure (
              framework_error_kind_t::route_not_connected,
              "actor lookup requires a running MeshNode", true);
        }
        if (_location_options.route_cache_max_age > std::chrono::milliseconds::zero ()) {
            std::lock_guard lock (_route_cache_gate);
            const auto cached = _route_cache.find (actor_id);
            if (cached != _route_cache.end ()) {
                if (std::chrono::steady_clock::now () < cached->second.expires_at) {
                    return result_t<resolved_actor_t>::success (cached->second.actor);
                }
                _route_cache.erase (cached);
            }
        }
        auto read = _store->read_authority (authority_key_t{"1:" + actor_id}).result ();
        if (!read) {
            return result_t<resolved_actor_t>::failure (
              framework_error_kind_t::request_failed,
              read.error () ? read.error ()->what () : "actor authority lookup failed",
              read.error () && read.error ()->is_retriable ());
        }
        const auto *snapshot = std::get_if<authority_snapshot_t> (&read.value ());
        const auto projection = snapshot
          ? zlink::framework::runtime::decode_actor_authority_payload (snapshot->payload)
          : std::nullopt;
        if (!snapshot || snapshot->allocation.state != placement_allocation_state_t::active
            || snapshot->allocation.object_kind != placement_object_kind_t::actor
            || !projection || projection->actor.actor_id () != actor_id) {
            return result_t<resolved_actor_t>::failure (
              policy == stale_policy_t::route_not_found
                ? framework_error_kind_t::actor_route_not_found
                : framework_error_kind_t::actor_location_stale,
              policy == stale_policy_t::route_not_found ? "actor route was not found"
                                                        : "actor location became stale",
              policy == stale_policy_t::location_stale);
        }
        if (projection->spot_id.empty ()) {
            return result_t<resolved_actor_t>::failure (
              policy == stale_policy_t::route_not_found
                ? framework_error_kind_t::actor_route_not_found
                : framework_error_kind_t::actor_location_stale,
              policy == stale_policy_t::route_not_found ? "actor SPOT route was not found"
                                                        : "actor SPOT location became stale",
              policy == stale_policy_t::location_stale);
        }
        auto resolved = resolved_actor_t{
          projection->actor, projection->actor,
          snapshot->allocation.target.node_rid,
          projection->spot_id};
        const auto lease_lifetime = _store->owner_admission_lifetime (snapshot->owner);
        if (_location_options.route_cache_max_age > std::chrono::milliseconds::zero ()
            && lease_lifetime) {
            const auto lifetime = std::min (
              std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                _location_options.route_cache_max_age),
              *lease_lifetime);
            if (lifetime > std::chrono::steady_clock::duration::zero ()) {
                std::lock_guard lock (_route_cache_gate);
                _route_cache.insert_or_assign (
                  actor_id, cached_actor_t{resolved, std::chrono::steady_clock::now () + lifetime});
            }
        }
        return result_t<resolved_actor_t>::success (std::move (resolved));
    }

    result_t<void> submit_send (const resolved_actor_t &actor,
                                std::string packet_name,
                                message_t message,
                                const actor_send_call_t::metadata_map_t &metadata)
    {
        auto runtime = first_mesh_node ();
        if (!runtime) {
            return result_t<void>::failure (framework_error_kind_t::route_not_connected,
                                            "actor send requires a running MeshNode", true);
        }
        auto relayed = relay_actor_packet (*runtime, actor,
                                           runtime::messaging::message_kind_t::command,
                                           std::move (packet_name), std::move (message),
                                           std::chrono::milliseconds (0), {}, metadata);
        if (!relayed) {
            invalidate_cached_route_on_stale (actor, relayed.error_kind ());
            return result_t<void>::failure (
              relayed.error_kind (),
              relayed.error () ? relayed.error ()->what () : "actor send failed",
              relayed.error () && relayed.error ()->is_retriable ());
        }
        return result_t<void>::success ();
    }

    result_t<message_t> submit_request (const resolved_actor_t &actor,
                                        std::string packet_name,
                                        message_t request,
                                        std::chrono::milliseconds timeout,
                                        const std::string &request_id)
    {
        auto runtime = first_mesh_node ();
        if (!runtime) {
            return result_t<message_t>::failure (framework_error_kind_t::route_not_connected,
                                                 "actor request requires a running MeshNode",
                                                 true);
        }
        auto relayed = relay_actor_packet (*runtime, actor,
                                           runtime::messaging::message_kind_t::request,
                                           std::move (packet_name), std::move (request), timeout,
                                           request_id);
        if (!relayed) {
            invalidate_cached_route_on_stale (actor, relayed.error_kind ());
            return result_t<message_t>::failure (
              relayed.error_kind (),
              relayed.error () ? relayed.error ()->what () : "actor request failed",
              relayed.error () && relayed.error ()->is_retriable ());
        }
        if (!relayed.value ()) {
            return result_t<message_t>::failure (framework_error_kind_t::request_failed,
                                                 "actor request reply body is missing");
        }
        return result_t<message_t>::success (
          message_t::from_raw (*relayed.value (), _serializers));
    }

    result_t<std::optional<zlink::message_t>>
    relay_actor_packet (detail::mesh_node_runtime_t &runtime,
                        const resolved_actor_t &actor,
                        runtime::messaging::message_kind_t kind,
                        std::string packet_name,
                        message_t message,
                        std::chrono::milliseconds timeout,
                        const std::string &request_id = {},
                        const actor_send_call_t::metadata_map_t &metadata = {})
    {
        (void) request_id;
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (kind, "actor", packet_name, timeout);
        header.metadata = metadata;
        auto parts = runtime::messaging::envelope_codec_t{}.encode_raw_body_parts (
          header, detail::message_to_raw (message, *_serializers));
        try {
            auto copied = parts.items ();
            if (kind == runtime::messaging::message_kind_t::command) {
                const auto submit = runtime.send_to_actor (actor.native_ref, copied);
                if (submit != zlink::submit_result_t::ok) {
                    if (submit == zlink::submit_result_t::backpressured) {
                        return result_t<std::optional<zlink::message_t>>::failure (
                          framework_error_kind_t::worker_queue_full,
                          "actor send is backpressured", true);
                    }
                    if (submit == zlink::submit_result_t::not_connected) {
                        return result_t<std::optional<zlink::message_t>>::failure (
                          framework_error_kind_t::route_not_connected,
                          "actor route is not connected", true);
                    }
                    if (submit == zlink::submit_result_t::not_found
                        || submit == zlink::submit_result_t::not_admitted) {
                        return result_t<std::optional<zlink::message_t>>::failure (
                          framework_error_kind_t::actor_route_not_found,
                          "actor route was not found");
                    }
                    return result_t<std::optional<zlink::message_t>>::failure (
                      framework_error_kind_t::request_failed,
                      "actor send was not accepted (result "
                        + std::to_string (static_cast<int> (submit)) + ", errno "
                        + std::to_string (errno) + ")");
                }
                return result_t<std::optional<zlink::message_t>>::success (std::nullopt);
            }
            detail::host::operation_id_t operation_id;
            const auto submit =
              runtime.request_to_actor (actor.native_ref, copied, operation_id, timeout);
            if (submit != zlink::submit_result_t::ok) {
                return result_t<std::optional<zlink::message_t>>::failure (
                  framework_error_kind_t::request_failed,
                      "actor request was not accepted");
            }
            auto reply = wait_for_actor_completion (runtime, operation_id, timeout);
            if (!reply) {
                return result_t<std::optional<zlink::message_t>>::failure (
                  reply.error_kind (),
                  std::string ("actor request completion failed for node/generation '")
                    + std::string (actor.native_ref.node_rid ().value ()) + "/"
                    + std::to_string (actor.native_ref.generation ()) + "': "
                    + (reply.error () ? reply.error ()->what () : "unknown failure"),
                  reply.error () && reply.error ()->is_retriable ());
            }
            runtime::messaging::envelope_codec_t reply_codec;
            auto reply_header = reply_codec.decode_header (reply.value ());
            if (!reply_header) {
                return result_t<std::optional<zlink::message_t>>::failure (
                  reply_header.error_kind (),
                  reply_header.error () ? reply_header.error ()->what ()
                                         : "actor mesh reply header decode failed");
            }
            if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
                const auto message =
                  reply_header.value ().error_message.value_or ("actor mesh request failed");
                runtime::messaging::request_failure_mapper_t failure_mapper;
                const auto mapped = failure_mapper.error_header_exception (
                  reply_header.value ().error_code.value_or ("request_failed"), message,
                  "actor mesh request");
                return result_t<std::optional<zlink::message_t>>::failure (
                  map_actor_route_reply_error (mapped.kind (), message), message,
                  mapped.is_retriable ());
            }
            auto body = reply_codec.decode_body (reply.value ());
            if (!body)
                return result_t<std::optional<zlink::message_t>>::success (std::nullopt);
            return result_t<std::optional<zlink::message_t>>::success (
              std::make_optional (std::move (body.value ())));
        }
        catch (const std::exception &error) {
            return map_native_exception<std::optional<zlink::message_t>> (
              error, kind == runtime::messaging::message_kind_t::request ? "actor request failed"
                                                                         : "actor send failed");
        }
    }

    void invalidate_cached_route_on_stale (const resolved_actor_t &actor,
                                           framework_error_kind_t kind)
    {
        if (kind != framework_error_kind_t::actor_location_stale
            && kind != framework_error_kind_t::actor_route_not_found
            && kind != framework_error_kind_t::route_not_connected) {
            return;
        }
        std::lock_guard lock (_route_cache_gate);
        _route_cache.erase (std::string (actor.framework_ref.actor_id ()));
    }

    std::shared_ptr<detail::mesh_node_runtime_t> first_mesh_node () const
    {
        for (const auto &mesh_node : _mesh_nodes)
            if (mesh_node)
                return mesh_node;
        return {};
    }

    static bool is_stale_actor_error (framework_error_kind_t kind)
    {
        return kind == framework_error_kind_t::actor_route_not_found
               || kind == framework_error_kind_t::actor_location_stale;
    }

    static framework_error_kind_t map_actor_route_reply_error (framework_error_kind_t kind,
                                                               const std::string &message)
    {
        if (message.find ("stale") != std::string::npos
            || message.find ("conflict") != std::string::npos
            || message.find ("transfer is in progress") != std::string::npos) {
            return framework_error_kind_t::actor_location_stale;
        }
        if (message.find ("not found") != std::string::npos
            || message.find ("not joined") != std::string::npos) {
            return framework_error_kind_t::actor_route_not_found;
        }
        if (message.find ("not connected") != std::string::npos
            || message.find ("No such file or directory") != std::string::npos
            || message.find ("errno=113") != std::string::npos) {
            return framework_error_kind_t::route_not_connected;
        }
        return kind;
    }

    template <typename TResult>
    static result_t<TResult> map_native_exception (const std::exception &error,
                                                  const char *fallback)
    {
        const std::string message = error.what () && *error.what () ? error.what () : fallback;
        if (message.find ("not connected") != std::string::npos
            || message.find ("NotConnected") != std::string::npos
            || message.find ("No such file or directory") != std::string::npos
            || message.find ("errno=113") != std::string::npos) {
            return result_t<TResult>::failure (framework_error_kind_t::route_not_connected,
                                               message, true);
        }
        if (message.find ("not found") != std::string::npos
            || message.find ("NotFound") != std::string::npos) {
            return result_t<TResult>::failure (framework_error_kind_t::actor_route_not_found,
                                               message);
        }
        if (message.find ("conflict") != std::string::npos
            || message.find ("stale") != std::string::npos
            || message.find ("transfer is in progress") != std::string::npos) {
            return result_t<TResult>::failure (framework_error_kind_t::actor_location_stale,
                                               message, true);
        }
        return result_t<TResult>::failure (framework_error_kind_t::request_failed, message);
    }

    live_location_reader_t *_store;
    serializer_registry_t *_serializers;
    std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> _mesh_nodes;
    std::shared_ptr<actor_location_observer_t> _actor_locations;
    location_options_t _location_options;
    struct cached_actor_t
    {
        resolved_actor_t actor;
        std::chrono::steady_clock::time_point expires_at;
    };
    std::mutex _route_cache_gate;
    std::map<std::string, cached_actor_t> _route_cache;
    std::chrono::milliseconds _default_timeout{std::chrono::seconds (30)};
    const std::string _request_id_prefix =
      std::to_string (reinterpret_cast<std::uintptr_t> (this)) + "-";
    std::atomic<std::uint64_t> _request_id_seq{1};
};

std::shared_ptr<actor_client_t>
make_actor_client (live_location_reader_t &store,
                   serializer_registry_t &serializers,
                   std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> mesh_nodes,
                   std::shared_ptr<actor_location_observer_t> actor_locations,
                   location_options_t options)
{
    return std::make_shared<actor_client_impl_t> (
      store, serializers, std::move (mesh_nodes), std::move (actor_locations),
      std::move (options));
}

} // namespace zlink::framework::runtime
