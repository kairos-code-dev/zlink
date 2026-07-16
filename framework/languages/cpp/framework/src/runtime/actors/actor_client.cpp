/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework/contracts/actors/actor.hpp>

#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/locations/store_location_resolvers.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include "runtime/spots/spot_route_packets.hpp"

#include <zlink/Contracts/Service/spot_node.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework
{

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

void actor_send_call_t::submit ()
{
    detail::submit_one_way_task (_client->send_to_actor_erased (
      std::move (_actor_ref), std::move (_packet_name), std::move (_message)));
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

task_t<message_t> actor_request_call_t::async_message ()
{
    return start (false);
}

task_t<message_t> actor_request_call_t::yield_message ()
{
    return start (true);
}

task_t<message_t> actor_request_call_t::start (bool release_turn)
{
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

class actor_client_impl_t final : public actor_client_t
{
  public:
    actor_client_impl_t (live_location_reader_t &store,
                         serializer_registry_t &serializers,
                         std::vector<detail::spot_node_runtime_t> spot_nodes,
                         std::shared_ptr<actor_location_observer_t> actor_locations) :
        _store (&store),
        _serializers (&serializers),
        _spot_nodes (std::move (spot_nodes)),
        _actor_locations (std::move (actor_locations))
    {
    }

  protected:
    task_t<void> send_to_actor_erased (actor_ref_t actor_ref,
                                       std::string packet_name,
                                       message_t message) override
    {
        auto actor = resolve_explicit_actor (actor_ref);
        if (!actor) {
            return task_t<void> (result_t<void>::failure (
              actor.error_kind (),
              actor.error () ? actor.error ()->what () : "actor route was not found",
              actor.error () && actor.error ()->is_retriable ()));
        }
        return task_t<void> (
          submit_send (actor.value (), std::move (packet_name), std::move (message)));
    }

    task_t<message_t> request_to_actor_erased (
      actor_ref_t actor_ref,
      std::string packet_name,
      message_t request,
      std::optional<std::chrono::milliseconds> timeout) override
    {
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
                    //     a cross-node straggler; after the forwarding window it
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
                // Native SPOT transport can collapse a remote framework error
                // to request_failed before the envelope reaches this client.
                // Consult the local source runtime only for the actor whose
                // supplied ref was current; this preserves the moving retry
                // without turning ordinary handler failures into retries.
                const bool collapsed_moving_failure =
                  !last && ref_was_current
                  && last.error_kind () == framework_error_kind_t::request_failed
                  && actor_transfer_in_progress (actor.value ().framework_ref);
                if (!is_moving_stale (last) && !collapsed_moving_failure) {
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
        zlink::actor_ref_t native_ref;
        node_rid_t node_rid;
        spot_rid_t spot_rid;
    };

    result_t<resolved_actor_t> resolve_explicit_actor (const actor_ref_t &actor_ref)
    {
        if (actor_ref.empty () || actor_ref.node_rid ().empty ()) {
            return result_t<resolved_actor_t>::failure (
              framework_error_kind_t::actor_route_not_found,
              "actor send requires a non-empty actor ref and node rid");
        }
        auto native = zlink::service::spot_node_t::remote_actor_ref (
          zlink::routing_id_t::from (std::string (actor_ref.node_rid ().value ())),
          std::string (actor_ref.actor_id ()));
        return result_t<resolved_actor_t>::success (
          resolved_actor_t{
            actor_ref, std::move (native), actor_ref.node_rid (),
            spot_rid_t::from_string (std::string (actor_ref.node_rid ().value ())) });
    }

    result_t<resolved_actor_t> resolve_actor (const std::string &actor_id, stale_policy_t policy)
    {
        auto row = _store->resolve_actor (actor_location_key_t{actor_id}).result ();
        if (!row) {
            return result_t<resolved_actor_t>::failure (
              framework_error_kind_t::request_failed,
              row.error () ? row.error ()->what () : "actor location lookup failed",
              row.error () && row.error ()->is_retriable ());
        }
        if (!row.value () || !_actor_locations->accepts (*row.value ())) {
            return result_t<resolved_actor_t>::failure (
              policy == stale_policy_t::route_not_found
                ? framework_error_kind_t::actor_route_not_found
                : framework_error_kind_t::actor_location_stale,
              policy == stale_policy_t::route_not_found ? "actor route was not found"
                                                        : "actor location became stale",
              policy == stale_policy_t::location_stale);
        }
        if (!row.value ()->spot_rid) {
            return result_t<resolved_actor_t>::failure (
              policy == stale_policy_t::route_not_found
                ? framework_error_kind_t::actor_route_not_found
                : framework_error_kind_t::actor_location_stale,
              policy == stale_policy_t::route_not_found ? "actor SPOT route was not found"
                                                        : "actor SPOT location became stale",
              policy == stale_policy_t::location_stale);
        }
        auto native = zlink::service::spot_node_t::remote_actor_ref (
          zlink::routing_id_t::from (std::string (row.value ()->actor_ref->node_rid ().value ())),
          std::string (row.value ()->actor_ref->actor_id ()));
        return result_t<resolved_actor_t>::success (
          resolved_actor_t{*row.value ()->actor_ref, std::move (native),
                           node_rid_t::from_string (
                             row.value ()->node_rid.to_string ()),
                           spot_rid_t::from_string (
                             row.value ()->spot_rid->to_string ())});
    }

    result_t<void> submit_send (const resolved_actor_t &actor,
                                std::string packet_name,
                                message_t message)
    {
        auto runtime = first_spot_node ();
        if (!runtime) {
            return result_t<void>::failure (framework_error_kind_t::route_not_connected,
                                            "actor send requires a running SPOT node", true);
        }
        auto relayed = relay_actor_packet (*runtime, actor, runtime::messaging::message_kind_t::command,
                                           std::move (packet_name), std::move (message),
                                           std::chrono::milliseconds (0));
        if (!relayed) {
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
        auto runtime = first_spot_node ();
        if (!runtime) {
            return result_t<message_t>::failure (framework_error_kind_t::route_not_connected,
                                                 "actor request requires a running SPOT node",
                                                 true);
        }
        auto relayed = relay_actor_packet (*runtime, actor, runtime::messaging::message_kind_t::request,
                                           std::move (packet_name), std::move (request), timeout,
                                           request_id);
        if (!relayed) {
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
    relay_actor_packet (detail::spot_node_runtime_t &runtime,
                        const resolved_actor_t &actor,
                        runtime::messaging::message_kind_t kind,
                        std::string packet_name,
                        message_t message,
                        std::chrono::milliseconds timeout,
                        const std::string &request_id = {})
    {
        auto native_node = runtime.native_node ();
        if (!native_node) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::route_not_connected, "actor request requires a running SPOT node",
              true);
        }
        runtime::messaging::client_call_codec_t codec;
        auto route_header = codec.create_envelope (
          runtime::messaging::message_kind_t::request, "spot",
          std::string (detail::spot_actor_packet_route_request_t::packet_name), timeout);
        spot_actor_message_metadata_t metadata;
        metadata.values["__zlink.actorBindSessionRoute"] = "false";
        if (kind == runtime::messaging::message_kind_t::command) {
            metadata.values["__zlink.actorRelayKind"] = "send";
        }
        // A stable id lets the target dispatch a preserved-then-retried request
        // exactly once (§10.2-1): every retry and the commit replay carry the
        // same id, so the target de-duplicates them to a single dispatch.
        if (!request_id.empty ()) {
            metadata.values["__zlink.actorRequestId"] = request_id;
        }
        auto request = detail::make_spot_actor_packet_route_request (
          actor.framework_ref, actor.spot_rid, packet_name,
          detail::message_to_raw (message, *_serializers), metadata);
        auto parts = codec.encode_envelope_parts (route_header, request, *_serializers);
        try {
            auto copied = parts.items ();
            auto origin_spot =
              native_node
                ->get_or_create_spot (zlink::routing_id_t::from (
                  std::string (runtime.node_rid ().value ()) + ":__zlink-actor-client-origin"))
                .first;
            auto iterator = copied.begin ();
            auto submit = origin_spot
                            .request_to_spot (
                              zlink::routing_id_t::from (std::string (actor.node_rid.value ())),
                              zlink::routing_id_t::from (std::string (actor.spot_rid.value ())))
                            .message (*iterator);
            ++iterator;
            for (; iterator != copied.end (); ++iterator) {
                submit = std::move (submit).message (*iterator);
            }
            auto reply = std::move (submit).timeout (timeout).async ().get ();
            auto decoded = codec.decode_envelope_reply<detail::spot_actor_packet_route_reply_t> (
              runtime::messaging::message_parts_t (std::move (reply)), *_serializers,
              "actor mesh reply is empty", "actor mesh reply decode failed", packet_name);
            if (!decoded) {
                const auto message =
                  decoded.error () ? decoded.error ()->what () : "actor mesh request failed";
                const auto mapped = map_actor_route_reply_error (decoded.error_kind (), message);
                return result_t<std::optional<zlink::message_t>>::failure (
                  mapped,
                  message,
                  decoded.error () && decoded.error ()->is_retriable ());
            }
            return result_t<std::optional<zlink::message_t>>::success (
              decoded.value ().has_reply
                ? std::make_optional (zlink::message_t::from (decoded.value ().payload))
                : std::nullopt);
        }
        catch (const std::exception &error) {
            return map_native_exception<std::optional<zlink::message_t>> (
              error, kind == runtime::messaging::message_kind_t::request ? "actor request failed"
                                                                         : "actor send failed");
        }
    }

    runtime::messaging::message_parts_t create_parts (runtime::messaging::message_kind_t kind,
                                                      std::string packet_name,
                                                      message_t message,
                                                      std::chrono::milliseconds timeout)
    {
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (kind, "actor", std::move (packet_name), timeout);
        return runtime::messaging::envelope_codec_t{}.encode_raw_body_parts (
          header, detail::message_to_raw (message, *_serializers));
    }

    result_t<message_t> decode_reply (runtime::messaging::message_parts_t reply)
    {
        runtime::messaging::envelope_codec_t codec;
        auto header = codec.decode_header (reply);
        if (!header) {
            return result_t<message_t>::failure (
              header.error_kind (), header.error () ? header.error ()->what ()
                                                    : "actor reply header decode failed");
        }
        if (header.value ().kind == runtime::messaging::message_kind_t::error) {
            return result_t<message_t>::failure (
              framework_error_kind_t::request_failed,
              header.value ().error_message.value_or ("actor request failed"));
        }
        auto body = codec.decode_body (reply);
        if (!body) {
            return result_t<message_t>::failure (
              body.error_kind (), body.error () ? body.error ()->what ()
                                                : "actor request reply body is missing");
        }
        return result_t<message_t>::success (message_t::from_raw (body.value (), _serializers));
    }

    std::optional<detail::spot_node_runtime_t> first_spot_node () const
    {
        for (const auto &spot_node : _spot_nodes) {
            if (auto native = spot_node.native_node ()) {
                (void) native;
                return spot_node;
            }
        }
        return std::nullopt;
    }

    bool actor_transfer_in_progress (const actor_ref_t &actor_ref) const
    {
        for (const auto &spot_node : _spot_nodes) {
            if (spot_node.node_rid ().value () == actor_ref.node_rid ().value ()) {
                return spot_node.actor_transfer_in_progress (actor_ref);
            }
        }
        return false;
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
    std::vector<detail::spot_node_runtime_t> _spot_nodes;
    std::shared_ptr<actor_location_observer_t> _actor_locations;
    std::chrono::milliseconds _default_timeout{std::chrono::seconds (30)};
    const std::string _request_id_prefix =
      std::to_string (reinterpret_cast<std::uintptr_t> (this)) + "-";
    std::atomic<std::uint64_t> _request_id_seq{1};
};

std::shared_ptr<actor_client_t>
make_actor_client (live_location_reader_t &store,
                   serializer_registry_t &serializers,
                   std::vector<detail::spot_node_runtime_t> spot_nodes,
                   std::shared_ptr<actor_location_observer_t> actor_locations)
{
    return std::make_shared<actor_client_impl_t> (
      store, serializers, std::move (spot_nodes), std::move (actor_locations));
}

} // namespace zlink::framework::runtime
