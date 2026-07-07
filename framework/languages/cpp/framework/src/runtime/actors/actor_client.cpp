/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/actors/actor.hpp>

#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include "runtime/spots/spot_route_packets.hpp"

#include <zlink/Contracts/Service/spot_node.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework
{

actor_send_call_t::actor_send_call_t (actor_client_t &client,
                                      std::string actor_id,
                                      std::string packet_name,
                                      message_t message) :
    _client (&client),
    _actor_id (std::move (actor_id)),
    _packet_name (std::move (packet_name)),
    _message (std::move (message))
{
}

actor_send_call_t &actor_send_call_t::packet_name (std::string packet_name)
{
    _packet_name = std::move (packet_name);
    return *this;
}

task_t<void> actor_send_call_t::async ()
{
    return _client->send_to_actor_erased (std::move (_actor_id), std::move (_packet_name),
                                          std::move (_message));
}

actor_request_call_t::actor_request_call_t (actor_client_t &client,
                                            std::string actor_id,
                                            std::string packet_name,
                                            message_t request) :
    _client (&client),
    _actor_id (std::move (actor_id)),
    _packet_name (std::move (packet_name)),
    _request (std::move (request))
{
}

actor_request_call_t &actor_request_call_t::packet_name (std::string packet_name)
{
    _packet_name = std::move (packet_name);
    return *this;
}

actor_request_call_t &actor_request_call_t::timeout (std::chrono::milliseconds timeout)
{
    _timeout = timeout;
    return *this;
}

task_t<message_t> actor_request_call_t::async_message ()
{
    return _client->request_to_actor_erased (std::move (_actor_id), std::move (_packet_name),
                                             std::move (_request), _timeout);
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
    actor_client_impl_t (actor_location_store_t &store,
                         serializer_registry_t &serializers,
                         std::vector<detail::spot_node_runtime_t> spot_nodes) :
        _store (&store),
        _serializers (&serializers),
        _spot_nodes (std::move (spot_nodes))
    {
    }

  protected:
    task_t<void> send_to_actor_erased (std::string actor_id,
                                       std::string packet_name,
                                       message_t message) override
    {
        auto actor = resolve_actor (actor_id, stale_policy_t::route_not_found);
        if (!actor) {
            return task_t<void> (result_t<void>::failure (
              actor.error_kind (),
              actor.error () ? actor.error ()->what () : "actor route was not found",
              actor.error () && actor.error ()->is_retriable ()));
        }
        auto first = submit_send (actor.value (), std::move (packet_name), std::move (message));
        if (first) {
            return task_t<void> (result_t<void>::success ());
        }
        if (!is_stale_actor_error (first.error_kind ())) {
            return task_t<void> (std::move (first));
        }
        auto resolved = resolve_actor (actor_id, stale_policy_t::location_stale);
        if (!resolved) {
            return task_t<void> (result_t<void>::failure (
              resolved.error_kind (),
              resolved.error () ? resolved.error ()->what () : "actor location is stale", true));
        }
        auto retry = submit_send (resolved.value (), std::move (packet_name), std::move (message));
        if (retry) {
            return task_t<void> (result_t<void>::success ());
        }
        if (is_stale_actor_error (retry.error_kind ())) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::actor_location_stale,
              "actor route is stale after re-resolve", true));
        }
        return task_t<void> (std::move (retry));
    }

    task_t<message_t> request_to_actor_erased (
      std::string actor_id,
      std::string packet_name,
      message_t request,
      std::optional<std::chrono::milliseconds> timeout) override
    {
        auto actor = resolve_actor (actor_id, stale_policy_t::route_not_found);
        if (!actor) {
            co_return result_t<message_t>::failure (
              actor.error_kind (), actor.error () ? actor.error ()->what ()
                                                  : "actor route was not found",
              actor.error () && actor.error ()->is_retriable ());
        }
        auto first =
          submit_request (actor.value (), packet_name, request, timeout.value_or (_default_timeout));
        if (first) {
            co_return first;
        }
        if (!is_stale_actor_error (first.error_kind ())) {
            co_return first;
        }
        auto resolved = resolve_actor (actor_id, stale_policy_t::location_stale);
        if (!resolved) {
            co_return result_t<message_t>::failure (
              resolved.error_kind (), resolved.error () ? resolved.error ()->what ()
                                                       : "actor location is stale",
              true);
        }
        auto retry = submit_request (resolved.value (), std::move (packet_name), std::move (request),
                                     timeout.value_or (_default_timeout));
        if (retry) {
            co_return retry;
        }
        if (is_stale_actor_error (retry.error_kind ())) {
            co_return result_t<message_t>::failure (
              framework_error_kind_t::actor_location_stale,
              "actor route is stale after re-resolve", true);
        }
        co_return retry;
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

    result_t<resolved_actor_t> resolve_actor (const std::string &actor_id, stale_policy_t policy)
    {
        auto row = _store->resolve_actor (actor_location_key_t{actor_id}).result ();
        if (!row) {
            return result_t<resolved_actor_t>::failure (
              framework_error_kind_t::request_failed,
              row.error () ? row.error ()->what () : "actor location lookup failed",
              row.error () && row.error ()->is_retriable ());
        }
        if (!row.value () || !row.value ()->actor_ref) {
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
                                        std::chrono::milliseconds timeout)
    {
        auto runtime = first_spot_node ();
        if (!runtime) {
            return result_t<message_t>::failure (framework_error_kind_t::route_not_connected,
                                                 "actor request requires a running SPOT node",
                                                 true);
        }
        auto relayed = relay_actor_packet (*runtime, actor, runtime::messaging::message_kind_t::request,
                                           std::move (packet_name), std::move (request), timeout);
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
                        std::chrono::milliseconds timeout)
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
        if (kind == runtime::messaging::message_kind_t::command) {
            metadata.values["__zlink.actorRelayKind"] = "send";
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
                return result_t<std::optional<zlink::message_t>>::failure (
                  decoded.error_kind (),
                  decoded.error () ? decoded.error ()->what () : "actor mesh request failed",
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

    static bool is_stale_actor_error (framework_error_kind_t kind)
    {
        return kind == framework_error_kind_t::actor_route_not_found
               || kind == framework_error_kind_t::actor_location_stale;
    }

    template <typename TResult>
    static result_t<TResult> map_native_exception (const std::exception &error,
                                                  const char *fallback)
    {
        const std::string message = error.what () && *error.what () ? error.what () : fallback;
        if (message.find ("not connected") != std::string::npos
            || message.find ("NotConnected") != std::string::npos) {
            return result_t<TResult>::failure (framework_error_kind_t::route_not_connected,
                                               message, true);
        }
        if (message.find ("not found") != std::string::npos
            || message.find ("NotFound") != std::string::npos) {
            return result_t<TResult>::failure (framework_error_kind_t::actor_route_not_found,
                                               message);
        }
        if (message.find ("conflict") != std::string::npos
            || message.find ("stale") != std::string::npos) {
            return result_t<TResult>::failure (framework_error_kind_t::actor_location_stale,
                                               message, true);
        }
        return result_t<TResult>::failure (framework_error_kind_t::request_failed, message);
    }

    actor_location_store_t *_store;
    serializer_registry_t *_serializers;
    std::vector<detail::spot_node_runtime_t> _spot_nodes;
    std::chrono::milliseconds _default_timeout{std::chrono::seconds (30)};
};

std::shared_ptr<actor_client_t>
make_actor_client (actor_location_store_t &store,
                   serializer_registry_t &serializers,
                   std::vector<detail::spot_node_runtime_t> spot_nodes)
{
    return std::make_shared<actor_client_impl_t> (store, serializers, std::move (spot_nodes));
}

} // namespace zlink::framework::runtime
