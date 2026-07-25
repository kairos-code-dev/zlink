/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>
#include <zlink/framework/version.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/channels/pending_operation.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/app.hpp>
#include <zlink/framework/contracts/configuration/configuration.hpp>
#include <zlink/framework/contracts/configuration/endpoint_connections.hpp>
#include <zlink/framework/contracts/configuration/drain.hpp>
#include <zlink/framework/contracts/configuration/detail/framework_options_state.hpp>
#include <zlink/framework/contracts/configuration/detail/framework_options_validation.hpp>
#include <zlink/framework/contracts/configuration/framework_options.hpp>
#include <zlink/framework/contracts/configuration/logging.hpp>
#include <zlink/framework/contracts/configuration/mesh_node.hpp>
#include <zlink/framework/contracts/configuration/route_mesh_runtime_options.hpp>
#include <zlink/framework/contracts/dispatch/task.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>
#include <zlink/framework/contracts/errors/error.hpp>
#include <zlink/framework/contracts/errors/result.hpp>
#include <zlink/framework/contracts/eventing/events.hpp>
#include <zlink/framework/contracts/eventing/health.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/configuration/transport.hpp>
#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/detail/call_facade.hpp>
#include <zlink/framework/contracts/detail/handler_invocation.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/detail/message_payload.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>
#include <zlink/framework/contracts/http/http.hpp>
#include <zlink/framework/contracts/locations/diagnostics.hpp>
#include <zlink/framework/contracts/locations/keys.hpp>
#include <zlink/framework/contracts/locations/location.hpp>
#include <zlink/framework/contracts/locations/options.hpp>
#include <zlink/framework/contracts/locations/resolvers.hpp>
#include <zlink/framework/contracts/locations/rows.hpp>
#include <zlink/framework/contracts/locations/runtime_query.hpp>
#include <zlink/framework/contracts/locations/spot_handle.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>
#include <zlink/framework/contracts/locations/values.hpp>
#include <zlink/framework/contracts/locations/watch.hpp>
#include <zlink/framework/contracts/locations/writes.hpp>
#include <zlink/framework/contracts/messaging/message.hpp>
#include <zlink/framework/contracts/monitoring/route_mesh_runtime.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink/framework/contracts/spots/spot_identity.hpp>
#include <zlink/framework/contracts/streams/stream.hpp>
#include <zlink/framework/contracts/timers/timer.hpp>
#include <zlink/framework/contracts/workers/worker.hpp>
#include <zlink/framework/codecs/json.hpp>
#include <zlink/framework/codecs/json_stream_connector.hpp>
#include <zlink/framework/codecs/json_stream_e2e_client.hpp>
#include <zlink/codecs/protobuf.hpp>
#include <zlink/http_client.hpp>
#include <zlink/http_client/contracts/client.hpp>
#include <zlink/http_client/contracts/coroutines.hpp>
#include <zlink/http_client/contracts/types.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_connector/codecs/auto_codec.hpp>
#include <zlink/stream_connector_throwing.hpp>
#include <zlink/stream_connector/contracts/calls/zlink_stream_calls.hpp>
#include <zlink/stream_connector/contracts/codec_registry.hpp>
#include <zlink/stream_connector/contracts/compression.hpp>
#include <zlink/stream_connector/contracts/connector.hpp>
#include <zlink/stream_connector/contracts/result.hpp>
#include <zlink/stream_connector/contracts/stream_payload.hpp>
#include <zlink/stream_connector/contracts/version.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_assert.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_connector.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_connector_factory.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_connector_options.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_enums.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_interfaces.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_models.hpp>
#include <zlink/stream_connector/version.hpp>
#include <zlink/stream_e2e_client.hpp>
#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>
#include <zlink/stream_e2e_client/task.hpp>

#include <future>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

struct contract_actor_t;
struct contract_http_client_name_t;

template <typename TContext>
concept has_destroy_actor = requires (TContext & context, contract_actor_t &actor)
{
    context.destroy_actor (actor);
};

template <typename TContext>
concept has_leave_actor = requires (
  TContext & context, const zlink::framework::actor_ref_t &actor_ref, contract_actor_t &actor)
{
    context.leave_actor (actor_ref, actor);
};

template <typename TContext> concept has_run_worker = requires (TContext & context)
{
    context.run_worker ([] { return 1; });
};

template <typename TContext> concept has_split_workers = requires (TContext & context)
{
    context.run_cpu_worker ([] { return 1; });
    context.run_io_worker ([] {
        return zlink::framework::task_t<int> (
          zlink::framework::result_t<int>::success (1));
    });
};

template <typename TRequest> concept has_http_async = requires (TRequest &request)
{
    request.template async<int> ();
};

template <typename TRequest> concept has_http_yield = requires (TRequest &request)
{
    request.template yield<int> ();
};

template <typename TRequest> concept has_http_response_submit = requires (TRequest &request)
{
    request.template submit<int> ();
};

template <typename TRequest> concept has_http_one_way_submit = requires (TRequest &request)
{
    request.submit ();
};

template <typename TRequest> concept has_http_fetch = requires (TRequest &request)
{
    request.template fetch<int> ();
};

template <typename T> concept has_actor_location_spot_kind_member = requires (T value)
{
    value.spot_kind;
};

template <typename T> concept has_actor_location_legacy_generation_member = requires (T value)
{
    value.generation;
};

template <typename T> concept has_actor_location_legacy_location_kind_member = requires (T value)
{
    value.location_kind;
};

template <typename T> concept has_actor_directory_find = requires (T value)
{
    value.find (std::declval<std::string> ());
};

template <typename T> concept has_location_readiness = requires (T value)
{
    value.is_peer_ready (std::declval<std::string> (),
                         zlink::framework::location_role_t::router,
                         std::declval<std::optional<zlink::routing_id_t>> ());
};

static_assert (zlink::framework::version_major == 0);
static_assert (zlink::http_client::version_major == 0);
static_assert (zlink::stream_connector::version_major == 0);
static_assert (std::is_same_v<decltype (zlink::stream_e2e_client::use (
                                std::declval<zlink::stream_connector::connector_t &> ())),
                              zlink::stream_e2e_client::coroutine_connector_t>);
static_assert (!std::is_same_v<zlink::framework::task_t<int>, std::future<int>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::request_call_t<int>> ().submit ()),
                 zlink::framework::task_t<int>>);

template <typename T> concept has_blocking_submit = requires (T value)
{
    value.submit ();
};

template <typename T> concept has_yield = requires (T value)
{
    value.yield ();
};

template <typename T, typename TReply> concept has_typed_yield = requires (T value)
{
    value.template yield<TReply> ();
};

template <typename T, typename TReply> concept has_typed_submit = requires (T value)
{
    value.template submit<TReply> ();
};

template <typename T> concept has_legacy_async = requires (T value)
{
    value.async ();
};

template <typename T, typename TReply> concept has_typed_legacy_async = requires (T value)
{
    value.template async<TReply> ();
};

template <typename T> concept has_packet_name = requires (T value)
{
    value.packet_name ("packet");
};

template <typename T> concept has_callback_submit = requires (T value)
{
    value.submit (std::declval<std::function<
                    zlink::framework::task_t<void> (zlink::framework::result_t<int>)>> ());
};

static_assert (has_yield<zlink::framework::request_call_t<int>>);

template <typename T> concept has_framework_use_discovery = requires (T value)
{
    value.use_discovery ();
};

template <typename T> concept has_framework_add_registry_peer = requires (T value)
{
    value.add_registry_peer ("tcp://127.0.0.1:5501");
};

template <typename T> concept has_zlink_enable_registry = requires (T value)
{
    value.enable_registry ();
};

template <typename T> concept has_zlink_discovery = requires (T value)
{
    value.discovery ();
};

template <typename T> concept has_spot_node_use_registry_spot_resolver = requires (T value)
{
    value.use_registry_spot_resolver ("route");
};

template <typename T, typename TResult> concept has_callback_async = requires (T value)
{
    value.async ([] (zlink::framework::result_t<TResult>) {});
};

static_assert (has_blocking_submit<zlink::framework::request_call_t<int>>);
static_assert (!has_legacy_async<zlink::framework::request_call_t<int>>);
static_assert (!has_callback_async<zlink::framework::request_call_t<int>, int>);
static_assert (has_blocking_submit<zlink::framework::send_call_t>);
static_assert (!has_callback_async<zlink::framework::send_call_t, void>);
static_assert (!has_yield<zlink::framework::send_call_t>);
static_assert (has_blocking_submit<zlink::framework::relay_request_call_t>);
static_assert (!has_legacy_async<zlink::framework::relay_request_call_t>);
static_assert (
  !has_callback_async<zlink::framework::relay_request_call_t, zlink::framework::message_t>);
static_assert (has_yield<zlink::framework::relay_request_call_t>);
static_assert (has_blocking_submit<zlink::framework::stream_write_call_t>);
static_assert (!has_callback_async<zlink::framework::stream_write_call_t, void>);
static_assert (has_blocking_submit<zlink::framework::route_send_call_t>);
static_assert (!has_callback_async<zlink::framework::route_send_call_t, void>);
static_assert (!has_blocking_submit<zlink::framework::channel_request_call_t>);
static_assert (has_typed_submit<zlink::framework::channel_request_call_t, std::uint64_t>);
static_assert (
  !has_typed_legacy_async<zlink::framework::channel_request_call_t, std::uint64_t>);
static_assert (!has_callback_async<zlink::framework::channel_request_call_t, std::uint64_t>);
static_assert (has_typed_yield<zlink::framework::channel_request_call_t, std::uint64_t>);
static_assert (has_blocking_submit<zlink::framework::actor_send_call_t>);
static_assert (!has_callback_async<zlink::framework::actor_send_call_t, void>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::actor_send_call_t &> ()
                                          .submit ()),
                              zlink::framework::task_t<void>>);
static_assert (!has_blocking_submit<zlink::framework::actor_request_call_t>);
static_assert (
  has_typed_submit<zlink::framework::actor_request_call_t, zlink::framework::message_t>);
static_assert (
  !has_typed_legacy_async<zlink::framework::actor_request_call_t,
                          zlink::framework::message_t>);
static_assert (!has_callback_async<zlink::framework::actor_request_call_t,
                                   zlink::framework::message_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_request_call_t &> ()
                             .submit<zlink::framework::message_t> ()),
                 zlink::framework::task_t<zlink::framework::message_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_request_call_t &> ()
                             .yield<zlink::framework::message_t> ()),
                 zlink::framework::task_t<zlink::framework::message_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_client_t &> ()
                             .send_to_actor (std::declval<zlink::framework::actor_ref_t> (),
                                             std::declval<zlink::framework::message_t> ())),
                 zlink::framework::actor_send_call_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_client_t &> ()
                             .request_to_actor (std::declval<zlink::framework::actor_ref_t> (),
                                                std::declval<zlink::framework::message_t> ())),
                 zlink::framework::actor_request_call_t>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::channel_request_call_t &> ().submit<int> ()),
               zlink::framework::task_t<int>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::publish_call_t &> ().submit ()),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::send_call_t &> ().submit ()),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_send_call_t &> ().submit ()),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::bound_session_send_call_t &> ().submit ()),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::stream_send_call_t &> ().submit ()),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::stream_write_call_t &> ().submit ()),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::publisher_t &> ().publish (
                   std::declval<std::string> (), std::declval<std::string> (), int{})),
                 zlink::framework::fanout_publish_call_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::fanout_publish_call_t &> ().submit ()),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::session_actor_t &> ().relay (
                   std::declval<const zlink::message_t &> ())),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::mesh_node_socket_config_t> ()
                .mailbox_message_budget),
    std::uint64_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::mesh_node_socket_config_t> ()
                .mailbox_byte_budget),
    std::uint64_t>);

template <typename T> concept has_blocking_wait = requires (T value)
{
    value.wait ();
};

template <typename T> concept has_future_get = requires (T value)
{
    value.get ();
};

static_assert (!has_blocking_wait<zlink::framework::task_t<int>>);
static_assert (!has_future_get<zlink::framework::task_t<int>>);

static_assert (std::is_abstract_v<zlink::framework::route_mesh_runtime_t>);
static_assert (std::is_abstract_v<zlink::framework::mesh_runtime_observation_t>);
static_assert (std::is_abstract_v<zlink::framework::route_mesh_runtime_options_t>);
static_assert (std::is_abstract_v<zlink::framework::mesh_node_runtime_options_t>);
static_assert (std::is_abstract_v<zlink::framework::mesh_channel_runtime_options_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_mesh_runtime_options_t &> ()
                             .mesh_node (std::declval<std::string> ())),
                 zlink::framework::mesh_node_runtime_options_t &>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_mesh_runtime_options_t &> ()
                             .channel (std::declval<std::string> (),
                                       std::declval<std::string> ())),
                 zlink::framework::mesh_channel_runtime_options_t &>);
static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::mesh_node_runtime_options_t &> ()
                             .max_message_size ()),
                 std::int64_t>);
static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::mesh_channel_runtime_options_t &> ()
                             .weight ()),
                 int>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_peer_snapshot_t> ().rid),
                 zlink::routing_id_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_node_snapshot_t> ().peers),
                 std::vector<zlink::framework::mesh_peer_snapshot_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_node_snapshot_t> ().channels),
                 std::vector<zlink::framework::mesh_channel_snapshot_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_node_snapshot_t> ()
                             .object_capacity),
                 zlink::framework::placement_capacity_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_node_snapshot_t> ()
                             .activation_concurrency),
                 zlink::framework::activation_concurrency_snapshot_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_runtime_event_t> ().state),
                 std::optional<zlink::framework::mesh_node_state_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::route_mesh_runtime_t &> ()
                             .snapshot (std::declval<std::string> ())),
                 zlink::framework::mesh_node_snapshot_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::route_mesh_runtime_t &> ().observe (
      std::declval<std::string> (),
      std::declval<std::size_t> (),
      std::declval<std::function<void (const zlink::framework::mesh_runtime_event_t &)>> ())),
    std::unique_ptr<zlink::framework::mesh_runtime_observation_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::route_mesh_runtime_t &> ()
                             .is_ready (std::declval<std::string> ())),
                 bool>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_mesh_runtime_t &> ().drain (
                   std::declval<std::string> (), std::declval<std::chrono::milliseconds> ())),
                 zlink::framework::task_t<zlink::framework::drain_result_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_mesh_runtime_t &> ()
                             .await_drained (std::declval<std::string> ())),
                 zlink::framework::task_t<zlink::framework::drain_result_t>>);

static_assert (std::is_same_v<decltype (zlink::http_client::client_t::create ()
                                          .base_url ("http://127.0.0.1:18080")
                                          .coroutines ()
                                          .build ()
                                          .post ("/sample")),
                              zlink::http_client::request_builder_t>);

static_assert (std::is_polymorphic_v<zlink::http_client::coroutine_execute_scheduler_t>);
static_assert (std::is_polymorphic_v<zlink::http_client::coroutine_resume_scheduler_t>);
static_assert (std::is_base_of_v<zlink::http_client::coroutine_resume_scheduler_t,
                                 zlink::http_client::framework_resume_scheduler_t>);
static_assert (std::is_polymorphic_v<zlink::framework::location_store_t>);
static_assert (
  std::is_polymorphic_v<
    zlink::framework::client_server_location_store_t>);
static_assert (
  std::is_polymorphic_v<
    zlink::framework::fanout_location_store_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  client_server_server_descriptor_t> ()
                .channel_name),
    std::string>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  client_server_server_descriptor_t> ()
                .server_rid),
    zlink::routing_id_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  client_server_server_descriptor_t> ()
                .descriptor_revision),
    std::uint64_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  client_server_location_store_t &> ()
                .update_client_server (
                  std::declval<zlink::framework::
                    client_server_server_descriptor_t> (),
                  zlink::framework::
                    location_write_intent_t::new_claim)),
    zlink::framework::task_t<
      zlink::framework::location_write_result_t>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  client_server_location_store_t &> ()
                .remove_client_server (
                  std::declval<zlink::framework::
                    client_server_server_descriptor_key_t> (),
                  std::declval<zlink::framework::
                    location_owner_token_t> ())),
    zlink::framework::task_t<
      zlink::framework::location_write_status_t>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  client_server_location_store_t &> ()
                .list_client_servers (
                  std::declval<std::string> (),
                  std::declval<zlink::framework::
                    location_page_request_t> ())),
    zlink::framework::task_t<
      zlink::framework::location_page_t<
        zlink::framework::
          client_server_server_descriptor_t>>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  fanout_publisher_descriptor_t> ()
                .publisher_rid),
    zlink::routing_id_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  fanout_publisher_descriptor_t> ()
                .descriptor_revision),
    std::uint64_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  fanout_location_store_t &> ()
                .update_fanout_publisher (
                  std::declval<zlink::framework::
                    fanout_publisher_descriptor_t> (),
                  zlink::framework::
                    location_write_intent_t::new_claim)),
    zlink::framework::task_t<
      zlink::framework::location_write_result_t>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  fanout_location_store_t &> ()
                .remove_fanout_publisher (
                  std::declval<zlink::framework::
                    fanout_publisher_descriptor_key_t> (),
                  std::declval<zlink::framework::
                    location_owner_token_t> ())),
    zlink::framework::task_t<
      zlink::framework::location_write_status_t>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<
                zlink::framework::
                  fanout_location_store_t &> ()
                .list_fanout_publishers (
                  std::declval<std::string> (),
                  std::declval<zlink::framework::
                    location_page_request_t> ())),
    zlink::framework::task_t<
      zlink::framework::location_page_t<
        zlink::framework::
          fanout_publisher_descriptor_t>>>);
static_assert (std::is_base_of_v<zlink::framework::peer_location_store_t,
                                 zlink::framework::location_store_t>);
static_assert (std::is_base_of_v<zlink::framework::spot_location_store_t,
                                 zlink::framework::location_store_t>);
static_assert (std::is_base_of_v<zlink::framework::actor_location_store_t,
                                 zlink::framework::location_store_t>);
static_assert (std::is_base_of_v<zlink::framework::route_location_store_t,
                                 zlink::framework::location_store_t>);
static_assert (std::is_base_of_v<zlink::framework::owner_lease_store_t,
                                 zlink::framework::location_store_t>);
static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::authority_snapshot_t> ()
                .pending_creation),
    std::optional<
      zlink::framework::pending_object_creation_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_store_t &> ()
                             .remove_all_by_owner (
                               std::declval<zlink::framework::
                                 location_owner_token_t> ())),
                 zlink::framework::task_t<std::int64_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_store_t &> ()
                             .claim_owner_lease (
                               "owner", std::chrono::milliseconds (10))),
                 zlink::framework::task_t<
                   zlink::framework::owner_lease_claim_result_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_store_t &> ()
                             .renew_owner_lease (
                               std::declval<zlink::framework::
                                 location_owner_token_t> (),
                               std::chrono::milliseconds (10))),
                 zlink::framework::task_t<
                   zlink::framework::owner_lease_renew_result_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_store_t &> ()
                             .release_owner_lease (
                               std::declval<zlink::framework::
                                 location_owner_token_t> ())),
                 zlink::framework::task_t<
                   zlink::framework::owner_lease_release_result_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::zlink_framework_options_t &> ()
                             .add_location_store (
                               std::declval<std::shared_ptr<zlink::framework::location_store_t>> ())),
                 zlink::framework::zlink_framework_options_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::peer_location_resolver_t &> ()
                             .list_live_peers (
                               std::declval<zlink::framework::peer_location_filter_t> ())),
                 zlink::framework::task_t<std::vector<zlink::framework::peer_location_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_handle_resolver_t &> ()
                             .resolve_spot_handle (
                               std::declval<zlink::framework::spot_id_t> ())),
                 zlink::framework::task_t<std::optional<zlink::framework::spot_handle_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_spot_handle_resolver_t &> ()
                             .resolve_actor_spot_handle (std::declval<std::string> ())),
                 zlink::framework::task_t<std::optional<zlink::framework::spot_handle_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::spot_handle_t &> ().spot_id ()),
                 zlink::framework::spot_id_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_runtime_query_t &> ()
                             .get_status ()),
                 zlink::framework::task_t<zlink::framework::location_runtime_status_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_runtime_query_t &> ()
                             .list_peer_locations (
                               std::declval<zlink::framework::peer_location_filter_t> ())),
                 zlink::framework::task_t<std::vector<zlink::framework::peer_location_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_runtime_query_t &> ()
                             .list_spot_locations (
                               std::declval<zlink::framework::spot_location_filter_t> (),
                               std::declval<zlink::framework::location_page_request_t> ())),
                 zlink::framework::task_t<
                   zlink::framework::location_page_t<zlink::framework::spot_location_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_runtime_query_t &> ()
                             .list_actor_locations (
                               std::declval<zlink::framework::actor_location_filter_t> (),
                               std::declval<zlink::framework::location_page_request_t> ())),
                 zlink::framework::task_t<
                   zlink::framework::location_page_t<zlink::framework::actor_location_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_runtime_query_t &> ()
                             .list_route_locations (
                               std::declval<zlink::framework::route_location_filter_t> (),
                               std::declval<zlink::framework::location_page_request_t> ())),
                 zlink::framework::task_t<
                   zlink::framework::location_page_t<zlink::framework::route_location_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_runtime_query_t &> ()
                             .list_topology (
                               std::declval<zlink::framework::location_topology_filter_t> (),
                               std::declval<zlink::framework::location_page_request_t> ())),
                 zlink::framework::task_t<
                   zlink::framework::location_page_t<zlink::framework::location_topology_entry_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_runtime_query_t &> ()
                             .list_service_summaries (
                               std::declval<zlink::framework::location_service_summary_filter_t> ())),
                 zlink::framework::task_t<
                   std::vector<zlink::framework::location_service_summary_t>>>);
static_assert (has_location_readiness<zlink::framework::location_readiness_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::location_readiness_t &> ()
                             .is_peer_ready (
                               std::declval<std::string> (),
                               zlink::framework::location_role_t::router,
                               std::declval<std::optional<zlink::routing_id_t>> ())),
                 zlink::framework::task_t<bool>>);

static_assert (static_cast<int> (zlink::framework::location_auto_connect_type_t::invalid) == 0);
static_assert (static_cast<int> (zlink::framework::location_auto_connect_type_t::route_mesh) == 1);
static_assert (static_cast<int> (zlink::framework::location_auto_connect_type_t::client_server) == 2);
static_assert (static_cast<int> (zlink::framework::location_auto_connect_type_t::dealer_mesh) == 3);
static_assert (static_cast<int> (zlink::framework::location_auto_connect_type_t::fanout) == 4);
static_assert (static_cast<int> (zlink::framework::location_auto_connect_type_t::spot_mesh) == 5);
static_assert (static_cast<int> (zlink::framework::location_role_t::invalid) == 0);
static_assert (static_cast<int> (zlink::framework::location_role_t::spot) == 2);
static_assert (static_cast<int> (zlink::framework::location_role_t::router) == 3);
static_assert (static_cast<int> (zlink::framework::location_role_t::dealer) == 4);
static_assert (static_cast<int> (zlink::framework::location_role_t::pub) == 5);
static_assert (static_cast<int> (zlink::framework::location_role_t::sub) == 6);
static_assert (static_cast<int> (zlink::framework::route_kind_t::invalid) == 0);
static_assert (static_cast<int> (zlink::framework::route_kind_t::actor_session) == 1);
static_assert (static_cast<int> (zlink::framework::route_kind_t::spot_name) == 2);
static_assert (static_cast<int> (zlink::framework::route_kind_t::framework_route) == 3);
static_assert (static_cast<int> (zlink::framework::location_kind_t::invalid) == 0);
static_assert (static_cast<int> (zlink::framework::location_kind_t::peer) == 1);
static_assert (static_cast<int> (zlink::framework::location_kind_t::spot) == 2);
static_assert (static_cast<int> (zlink::framework::location_kind_t::actor) == 3);
static_assert (static_cast<int> (zlink::framework::location_kind_t::route) == 4);
static_assert (static_cast<int> (zlink::framework::location_write_intent_t::new_claim) == 1);
static_assert (static_cast<int> (zlink::framework::location_write_intent_t::renew) == 2);
static_assert (static_cast<int> (zlink::framework::location_write_intent_t::takeover) == 3);
static_assert (static_cast<int> (zlink::framework::location_write_status_t::stored) == 1);
static_assert (static_cast<int> (zlink::framework::location_write_status_t::ignored_stale) == 2);
static_assert (static_cast<int> (zlink::framework::location_write_status_t::rejected_conflict) == 3);
static_assert (static_cast<int> (zlink::framework::location_change_type_t::upserted) == 1);
static_assert (static_cast<int> (zlink::framework::location_change_type_t::removed) == 2);
static_assert (static_cast<int> (zlink::framework::location_change_type_t::expired) == 3);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_route_not_found) == 0);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_create_failed) == 1);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_already_exists) == 2);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_type_mismatch) == 3);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::spot_create_failed) == 4);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::spot_route_not_found) == 5);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::spot_type_mismatch) == 6);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_session_not_bound) == 7);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::handler_not_found) == 8);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::route_handler_not_found) == 9);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_dispatch_handler_not_found) == 10);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::payload_decode_failed) == 11);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::route_not_connected) == 12);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::request_target_not_found) == 13);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::request_rejected) == 14);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::request_protocol_error) == 15);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::request_failed) == 16);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::worker_queue_full) == 17);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::worker_timed_out) == 18);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::worker_failed) == 19);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_location_stale) == 20);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_create_rejected) == 21);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::object_client_not_configured) == 22);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::mesh_selection_required) == 23);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::mesh_not_found) == 24);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::invalid_configuration) == 25);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::already_submitted) == 26);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_generation_stale) == 27);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::actor_moving) == 28);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::deadline_exceeded) == 29);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::placement_capacity_exhausted) == 30);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::routing_id_conflict) == 31);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::spot_generation_stale) == 32);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::spot_moving) == 33);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::relocation_data_lost) == 34);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::spot_id_conflict) == 35);
static_assert (static_cast<int> (zlink::framework::framework_error_kind_t::runtime_shutdown) == 36);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::location_changed_t> ().key),
                              zlink::framework::location_key_t>);
static_assert (
  std::is_same_v<zlink::framework::location_key_t,
                 std::variant<zlink::framework::peer_location_key_t,
                              zlink::framework::spot_location_key_t,
                              zlink::framework::actor_location_key_t,
                              zlink::framework::route_location_key_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_location_t> ().actor_ref),
                 zlink::framework::actor_ref_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_location_t> ().spot_kind),
                 zlink::spot_kind>);
static_assert (has_actor_location_spot_kind_member<zlink::framework::actor_location_t>);
static_assert (!has_actor_location_legacy_generation_member<zlink::framework::actor_location_t>);
static_assert (!has_actor_location_legacy_location_kind_member<zlink::framework::actor_location_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_location_key_t> ().mesh_name),
                 std::string>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_location_key_t> ().actor_id),
                 std::string>);
static_assert (has_actor_directory_find<zlink::framework::actor_directory_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_directory_t &> ()
                             .find (std::declval<std::string> ())),
                 zlink::framework::task_t<std::optional<zlink::framework::actor_ref_t>>>);
template <typename T>
concept has_http_client_coroutine_resume_builder =
  requires (T value, std::shared_ptr<zlink::http_client::coroutine_resume_scheduler_t> resume)
{
    value.coroutines (resume);
};

template <typename T>
concept has_http_client_coroutine_execute_resume_builder =
  requires (T value,
            std::shared_ptr<zlink::http_client::coroutine_execute_scheduler_t> execute,
            std::shared_ptr<zlink::http_client::coroutine_resume_scheduler_t> resume)
{
    value.coroutines (execute, resume);
};

static_assert (has_http_client_coroutine_resume_builder<zlink::http_client::client_builder_t>);
static_assert (
  has_http_client_coroutine_execute_resume_builder<zlink::http_client::client_builder_t>);

template <typename T> concept has_channel_capability_socket_options = requires (T value)
{
    value.send_high_water_mark (zlink::message_count_t::value (8));
    value.receive_high_water_mark (zlink::message_count_t::value (8));
    value.max_message_size (zlink::byte_size_t::bytes (4096));
    value.peer_weight (zlink::peer_weight_t::value (75));
};

template <typename T>
concept has_legacy_client_server_role_methods = requires (T value)
{
    value.enable_client ();
    value.enable_server ("tcp://127.0.0.1:5000");
};

static_assert (has_channel_capability_socket_options<zlink::framework::capability_builder_t>);
static_assert (
  !has_legacy_client_server_role_methods<
    zlink::framework::client_server_channel_builder_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::channel_runtime_options_t &> ()
                             .client_server_channel ("api")
                             .configure_server_socket ()
                             .peer_weight (zlink::peer_weight_t::value (0))),
                 zlink::framework::channel_server_socket_runtime_options_t &>);

namespace
{

struct named_request_t
{
    static constexpr const char *packet_name = "NamedRequest";
    int value{};
};

struct named_context_request_t
{
    static constexpr const char *packet_name = "NamedContextRequest";
    int value{};
};

struct named_reply_t
{
    int value{};
};

struct contract_actor_t
{
};

struct contract_exact_actor_t : zlink::framework::actor_t
{
    explicit contract_exact_actor_t (
      zlink::framework::actor_context_t &context) :
        value (&context)
    {
    }

    zlink::framework::actor_context_t &context () noexcept override
    {
        return *value;
    }

    const zlink::framework::actor_context_t &
    context () const noexcept override
    {
        return *value;
    }

    zlink::framework::actor_context_t *value;
};

struct contract_exact_actor_factory_t
    : zlink::framework::actor_factory_t<contract_exact_actor_t>
{
    zlink::framework::task_t<
      std::shared_ptr<contract_exact_actor_t>>
    create (zlink::framework::actor_context_t &context,
            std::stop_token) override
    {
        return zlink::framework::task_t<
          std::shared_ptr<contract_exact_actor_t>> (
          zlink::framework::result_t<
            std::shared_ptr<contract_exact_actor_t>>::success (
              std::make_shared<contract_exact_actor_t> (context)));
    }
};

struct contract_actor_transfer_t
    : zlink::framework::actor_transfer_adapter_t<contract_actor_t>
{
    zlink::framework::task_t<zlink::framework::message_t>
    transfer_out (const contract_actor_t &) override
    {
        return zlink::framework::task_t<zlink::framework::message_t> (
          zlink::framework::result_t<zlink::framework::message_t>::success ({}));
    }

    zlink::framework::task_t<contract_actor_t>
    transfer_in (std::string, zlink::framework::message_t) override
    {
        return zlink::framework::task_t<contract_actor_t> (
          zlink::framework::result_t<contract_actor_t>::success ({}));
    }
};

struct contract_create_request_t
{
    int value{};
};

struct contract_spot_t : public zlink::framework::spot_t
{
    zlink::framework::spot_actor_join_response_t on_actor_join (
      std::string_view, zlink::framework::message_t)
    {
        return zlink::framework::spot_actor_join_response_t::accept ();
    }

    void on_create_actor (contract_actor_t &, const zlink::framework::message_t &) {}
    zlink::framework::task_t<void> on_actor_joined (contract_actor_t &) { co_return; }
    zlink::framework::task_t<void> on_leave_actor (contract_actor_t &) { co_return; }
    void on_actor_send (contract_actor_t &,
                        zlink::framework::spot_actor_send_context_t &,
                        const named_request_t &)
    {
    }
    named_reply_t on_actor_request (contract_actor_t &,
                                    zlink::framework::spot_actor_request_context_t &,
                                    const named_request_t &)
    {
        return {};
    }
};

void to_json (nlohmann::json &json, const named_request_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

void from_json (const nlohmann::json &json, named_request_t &value)
{
    value.value = json.value ("value", 0);
}

void to_json (nlohmann::json &json, const named_context_request_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

void from_json (const nlohmann::json &json, named_context_request_t &value)
{
    value.value = json.value ("value", 0);
}

void to_json (nlohmann::json &json, const named_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

void from_json (const nlohmann::json &json, named_reply_t &value)
{
    value.value = json.value ("value", 0);
}

zlink::message_t to_stream_payload (const named_request_t &value)
{
    return zlink::message_t::from_json (value);
}

void from_stream_payload (const zlink::message_t &message, named_request_t &value)
{
    value = message.parse_json<named_request_t> ();
}

zlink::message_t to_stream_payload (const named_context_request_t &value)
{
    return zlink::message_t::from_json (value);
}

void from_stream_payload (const zlink::message_t &message, named_context_request_t &value)
{
    value = message.parse_json<named_context_request_t> ();
}

zlink::message_t to_stream_payload (const named_reply_t &value)
{
    return zlink::message_t::from_json (value);
}

void from_stream_payload (const zlink::message_t &message, named_reply_t &value)
{
    value = message.parse_json<named_reply_t> ();
}

struct named_send_handler_t
{
    using message_type = named_request_t;
    void handle (const named_request_t &) {}
};

struct named_request_handler_t
{
    using request_type = named_request_t;
    using reply_type = named_reply_t;
    named_reply_t handle (const named_request_t &) { return {}; }
};

struct named_publish_handler_t
{
    using event_type = named_request_t;
    void handle (const named_request_t &) {}
};

struct named_route_handler_t
{
    named_reply_t handle_request (const named_request_t &,
                                  const zlink::framework::route_handler_context_t &)
    {
        return {};
    }

    void handle_send (const named_request_t &, const zlink::framework::route_handler_context_t &) {}
};

class named_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_error (zlink::framework::stream_t &,
                                             const zlink::framework::stream_error_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_packet (zlink::framework::stream_t &,
                                              const zlink::framework::stream_dispatch_context_t &,
                                              const zlink::message_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }
};

struct typed_session_payload_t
{
    int value = 0;
};

struct typed_session_handler_t
{
    zlink::framework::task_t<void> handle (zlink::framework::stream_t &,
                                           const typed_session_payload_t &)
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }
};

struct untyped_session_handler_t
{
    void handle (zlink::framework::stream_t &, const typed_session_payload_t &) {}
};

static_assert (zlink::framework::typed_session_packet_handler_for<
               typed_session_handler_t, zlink::framework::stream_t, typed_session_payload_t>);
static_assert (!zlink::framework::typed_session_packet_handler_for<
               untyped_session_handler_t, zlink::framework::stream_t, typed_session_payload_t>);
static_assert (
  std::is_same_v<decltype (zlink::framework::dispatch_typed_session_packet<typed_session_payload_t> (
                   std::declval<typed_session_handler_t &> (),
                   std::declval<zlink::framework::stream_t &> (),
                   std::declval<zlink::framework::serializer_registry_t &> (),
                   std::declval<const zlink::message_t &> ())),
                 zlink::framework::task_t<void>>);

struct typed_config_t
{
    std::string endpoint;

    static typed_config_t bind (const zlink::framework::configuration_section_t &section)
    {
        return {.endpoint = section.require ("endpoint")};
    }
};

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::channel_client_t &> ()
                                          .request ("sample", named_request_t{})),
                              zlink::framework::channel_request_call_t>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::route_send_call_t &> ()
                                          .metadata ("trace-id", "abc")),
                              zlink::framework::route_send_call_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::send_call_t &> ()),
                 zlink::framework::send_call_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::send_call_t &> ().metadata (
                                "trace-id", "abc")),
                              zlink::framework::send_call_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::channel_request_call_t &> ()
                                          ),
                              zlink::framework::channel_request_call_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::channel_request_call_t &> ()
                                          .metadata ("trace-id", "abc")),
                              zlink::framework::channel_request_call_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::channel_request_call_t &> ()
                                          .metadata ("trace-id", "abc")),
                              zlink::framework::channel_request_call_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::stream_write_call_t &> ()
                                          .metadata ("trace-id", "abc")),
                              zlink::framework::stream_write_call_t &>);

static_assert (!has_packet_name<zlink::framework::stream_write_call_t>);
static_assert (has_packet_name<zlink::framework::stream_send_call_t>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::stream_write_call_t &> ().compress ()),
                 zlink::framework::stream_write_call_t &>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::monitoring_builder_t &> ().add_socket_events (
      "profile.server",
      std::initializer_list<zlink::framework::socket_event_kind_t>{
        zlink::framework::socket_event_kind_t::connection_ready})),
    zlink::framework::monitoring_builder_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::stream_t &> ().close ()),
                              zlink::framework::task_t<void>>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::stream_t &> ().write_packet (
                                std::declval<const zlink::message_t &> ())),
                              zlink::framework::stream_send_call_t>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::stream_t &> ().reply_packet (
                                std::declval<const zlink::message_t &> ())),
                              zlink::framework::stream_write_call_t>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::bound_session_t &> ().disconnect ()),
                 zlink::framework::task_t<void>>);
static_assert (!has_yield<zlink::framework::bound_session_send_call_t>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::config_builder_t &> ()
                                          .bind<typed_config_t> ("server")),
                              std::optional<typed_config_t>>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::zlink_framework_options_t &> ().configure_dispatch ()),
    zlink::framework::dispatch_options_t &>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::dispatch_options_t &> ().set_message_flow_observer (
      std::declval<std::shared_ptr<zlink::framework::message_flow_observer_t>> ())),
    zlink::framework::dispatch_options_t &>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::dispatch_options_t &> ().set_message_flow_observer (
      std::declval<std::function<void (const zlink::framework::message_flow_event_t &)>> ())),
    zlink::framework::dispatch_options_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::http_context_t &> ()
                                          .response_header ("X-Test", "value")),
                              zlink::framework::http_context_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::http_response_t &> ()
                                          .header ("X-Test", "value")),
                              zlink::framework::http_response_t &>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::http_options_builder_t &> ().configure_tls (
      std::declval<std::function<void (zlink::framework::http_tls_options_builder_t &)>> ())),
    zlink::framework::http_options_builder_t &>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::http_options_builder_t &> ().configure_server (
      std::declval<std::function<void (zlink::framework::http_server_options_builder_t &)>> ())),
    zlink::framework::http_options_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::http_server_options_builder_t &> ()
                             .set_max_connections (4)),
                 zlink::framework::http_server_options_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::handler_options_builder_t &> ()
                             .group ("api")
                             .add_send<named_send_handler_t> ()),
                 zlink::framework::handler_options_builder_t::group_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::handler_options_builder_t &> ()
                             .group ("events")
                             .add_publish<named_publish_handler_t> ()),
                 zlink::framework::handler_options_builder_t::group_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::stream_node_options_builder_t &> ()
                             .register_session<named_session_t> ()),
                 zlink::framework::stream_node_options_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::stream_node_options_builder_t &> ()
                             .set_tls_server ("server.crt", "server.key")),
                 zlink::framework::stream_node_options_builder_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::stream_builder_t &> ()
                                          .set_tls_server ("server.crt", "server.key")),
                              zlink::framework::stream_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::fanout_channel_builder_t &> ()
                             .enable_publisher ("tcp://127.0.0.1:5000")),
                 zlink::framework::fanout_channel_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::fanout_channel_builder_t &> ()
                             .enable_subscriber ("tcp://127.0.0.1:5001")),
                 zlink::framework::fanout_channel_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::fanout_channel_builder_t &> ()
                             .use_handler_group ("events")),
                 zlink::framework::fanout_channel_builder_t &>);

static_assert (
  std::is_same_v<
    decltype (
      std::declval<
        zlink::framework::client_server_channel_builder_t &> ()
        .client ()),
    zlink::framework::client_server_channel_client_builder_t>);

static_assert (
  std::is_same_v<
    decltype (
      std::declval<
        zlink::framework::client_server_channel_builder_t &> ()
        .server ()),
    zlink::framework::client_server_channel_server_builder_t>);

static_assert (
  std::is_same_v<
    decltype (
      std::declval<
        zlink::framework::
          client_server_channel_client_builder_t &> ()
        .connect ("tcp://127.0.0.1:5300")),
    zlink::framework::client_server_channel_client_builder_t &>);

static_assert (
  std::is_same_v<
    decltype (
      std::declval<
        zlink::framework::
          client_server_channel_server_builder_t &> ()
        .listen ()
        .set_bind_host ("127.0.0.1")
        .set_advertise_host ("server.example")
        .set_weight (75)
        .add_handler_group ("orders")),
    zlink::framework::client_server_channel_server_builder_t &>);

static_assert (
  std::is_same_v<
    decltype (
      std::declval<
        zlink::framework::
          client_server_channel_server_builder_t &> ()
        .add_send_handler<
          named_send_handler_t, named_request_t> ("send")),
    zlink::framework::client_server_channel_server_builder_t &>);

static_assert (
  std::is_same_v<
    decltype (
      std::declval<
        zlink::framework::
          client_server_channel_server_builder_t &> ()
        .add_request_handler<
          named_request_handler_t, named_request_t,
          named_reply_t> ("request")),
    zlink::framework::client_server_channel_server_builder_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::capability_builder_t &> ()
                                          .set_routing_id (zlink::routing_id_t::from ("api"))),
                              zlink::framework::capability_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::zlink_framework_options_t &> ()
                             .add_fanout_channel ("events")),
                 zlink::framework::fanout_channel_builder_t>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_mesh_channel_builder_t &> ()
                             .enable_server ("tcp://127.0.0.1:5300")),
                 zlink::framework::route_mesh_channel_builder_t &>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::route_mesh_channel_builder_t &> ().enable_client ()),
    zlink::framework::route_mesh_channel_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_mesh_channel_builder_t &> ()
                             .enable_client ("tcp://127.0.0.1:5301")),
                 zlink::framework::route_mesh_channel_builder_t &>);

static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::route_mesh_channel_builder_t &> ()
                .add_request_handler<named_route_handler_t, named_request_t, named_reply_t> (
                  "request", &named_route_handler_t::handle_request)),
    zlink::framework::route_mesh_channel_builder_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::route_mesh_channel_builder_t &> ()
                             .add_send_handler<named_route_handler_t, named_request_t> (
                               "send", &named_route_handler_t::handle_send)),
                 zlink::framework::route_mesh_channel_builder_t &>);

static_assert (std::has_virtual_destructor_v<zlink::framework::spot_t>);
static_assert (std::has_virtual_destructor_v<zlink::framework::entry_spot_t>);
static_assert (std::is_base_of_v<zlink::framework::spot_t, zlink::framework::entry_spot_t>);
static_assert (std::has_virtual_destructor_v<
               zlink::framework::actor_transfer_adapter_t<contract_actor_t>>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .set_placement_weight (10000)),
               zlink::framework::mesh_node_builder_t &>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .add_actor_transfer_adapter<contract_actor_t,
                                                       contract_actor_transfer_t> ("contract")),
               zlink::framework::mesh_node_builder_t &>);
static_assert (std::is_same_v<decltype (std::declval<contract_spot_t &> ().on_actor_join (
                                std::declval<std::string_view> (),
                                std::declval<zlink::framework::message_t> ())),
                              zlink::framework::spot_actor_join_response_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_context_t &> ().close ()),
                 zlink::framework::task_t<bool>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_context_t &> ().manager ()),
                 zlink::framework::spot_manager_t>);
static_assert (!has_run_worker<zlink::framework::spot_context_t>);
static_assert (has_split_workers<zlink::framework::spot_context_t>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::spot_context_t &> ()
                                          .run_cpu_worker ([] { return 1; })),
                              zlink::framework::worker_call_t<int>>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::spot_context_t &> ()
                                          .run_io_worker ([] {
                                              return zlink::framework::task_t<int> (
                                                zlink::framework::result_t<int>::success (1));
                                          })),
                              zlink::framework::worker_call_t<int>>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::worker_call_t<int> &> ()
                                          .timeout (std::chrono::milliseconds (1))),
                              zlink::framework::worker_call_t<int> &>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::worker_call_t<int> &> ().submit ()),
                 zlink::framework::task_t<int>>);
static_assert (!has_legacy_async<zlink::framework::worker_call_t<int>>);
static_assert (has_yield<zlink::framework::worker_call_t<int>>);
static_assert (!has_callback_submit<zlink::framework::worker_call_t<int>>);
static_assert (has_leave_actor<zlink::framework::spot_context_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_context_t &> ().leave_actor (
                   std::declval<const zlink::framework::actor_ref_t &> (),
                   std::declval<contract_actor_t &> ())),
                 zlink::framework::task_t<zlink::framework::actor_ref_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::session_actor_manager_t &> ()
                             .bind_or_get (std::declval<zlink::framework::actor_ref_t> ())),
                 zlink::framework::request_call_t<zlink::framework::session_actor_t>>);
static_assert (
  std::is_same_v<decltype (zlink::framework::actor_ref_snapshot_t::from (
                              std::declval<const zlink::framework::actor_ref_t &> ())),
                 zlink::framework::actor_ref_snapshot_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_node_builder_t &> ().snapshot ()),
                 zlink::framework::spot_node_snapshot_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_handler_registry_t &> ()
                             .add_actor_send<&contract_spot_t::on_actor_send> ()),
                 zlink::framework::spot_handler_registry_t &>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_handler_registry_t &> ()
                             .add_actor_request<&contract_spot_t::on_actor_request> ()),
                 zlink::framework::spot_handler_registry_t &>);
static_assert (static_cast<int> (zlink::framework::spot_handler_kind_t::actor_send) == 2);
static_assert (static_cast<int> (zlink::framework::spot_handler_kind_t::actor_request) == 3);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_manager_t &> ()
                             .create (std::declval<std::string> ())),
                 zlink::framework::spot_create_call_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_manager_t &> ()
                             .get_or_create (
                               std::declval<zlink::framework::spot_id_t> (),
                               std::declval<std::string> ())),
                 zlink::framework::spot_create_call_t>);
static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::spot_manager_t &> ()
                             .find (std::declval<zlink::framework::spot_id_t> ())),
                 zlink::framework::task_t<
                   std::optional<zlink::framework::spot_ref_t>>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_manager_t &> ()
                             .close (std::declval<zlink::framework::spot_ref_t> ())),
                 zlink::framework::task_t<bool>>);
static_assert (!std::is_copy_constructible_v<zlink::framework::spot_create_call_t>);
static_assert (std::is_move_constructible_v<zlink::framework::spot_create_call_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_create_call_t &> ()
                             .creation_request (
                               std::declval<zlink::framework::message_t> ())),
                 zlink::framework::spot_create_call_t &>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_create_call_t &> ()
                             .submit ()),
                 zlink::framework::task_t<zlink::framework::spot_create_result_t>>);
static_assert (!has_destroy_actor<zlink::framework::spot_context_t>);
static_assert (has_destroy_actor<zlink::framework::entry_spot_context_t>);
static_assert (!has_run_worker<zlink::framework::entry_spot_context_t>);
static_assert (has_split_workers<zlink::framework::entry_spot_context_t>);
static_assert (!has_http_async<zlink::http_client::request_builder_t>);
static_assert (!has_http_yield<zlink::http_client::request_builder_t>);
static_assert (has_http_response_submit<zlink::http_client::request_builder_t>);
static_assert (!has_http_one_way_submit<zlink::http_client::request_builder_t>);
static_assert (!has_http_fetch<zlink::http_client::request_builder_t>);
static_assert (!has_http_async<zlink::http_client::server_request_builder_t>);
static_assert (has_http_yield<zlink::http_client::server_request_builder_t>);
static_assert (has_http_response_submit<zlink::http_client::server_request_builder_t>);
static_assert (has_http_one_way_submit<zlink::http_client::server_request_builder_t>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::http_client::client_builder_t &> ().build_server (
                 std::declval<std::shared_ptr<zlink::http_client::execution_turn_t>> ())),
               zlink::http_client::server_client_t>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::http_client::client_builder_t &> ()
                           .template build_server<contract_http_client_name_t> (
                             std::declval<std::shared_ptr<
                               zlink::http_client::execution_turn_t>> ())),
               zlink::http_client::named_server_client_t<contract_http_client_name_t>>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::entry_spot_context_t &> ()
                                          .run_cpu_worker ([] { return 1; })),
                              zlink::framework::worker_call_t<int>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::entry_spot_context_t &> ().destroy_actor (
                   std::declval<contract_actor_t &> ())),
                 zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::spot_node_builder_t &> ().add_spot<contract_spot_t> (
      "stage", std::declval<std::function<std::shared_ptr<contract_spot_t> ()>> ())),
    zlink::framework::spot_node_builder_t &>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::spot_create_result_t> ().reply),
                 std::optional<zlink::framework::message_t>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_context_t &> ().join_spot (
                   std::declval<zlink::framework::spot_id_t> (),
                   std::declval<const zlink::framework::message_t &> ())),
                 zlink::framework::actor_join_call_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::actor_join_call_t &> ().defer ()),
                 void>);
static_assert (std::is_same_v<
               decltype (std::declval<contract_exact_actor_t &> ()
                           .on_join_completed (
                             std::declval<const zlink::framework::
                               actor_join_completion_t &> ())),
               zlink::framework::task_t<void>>);
static_assert (std::variant_size_v<
                 zlink::framework::actor_join_completion_t>
               == 3);
static_assert (
  std::is_same_v<
    decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                .add_actor_factory<contract_exact_actor_t> (
                  "actor",
                  std::declval<std::shared_ptr<
                    contract_exact_actor_factory_t>> ())),
    zlink::framework::mesh_node_builder_t &>);
static_assert (!has_legacy_async<zlink::framework::actor_join_call_t>);
static_assert (!has_blocking_submit<zlink::framework::actor_join_call_t>);
static_assert (!has_yield<zlink::framework::actor_join_call_t>);
static_assert (!has_typed_yield<zlink::framework::actor_join_call_t, std::string>);
static_assert (std::is_same_v<decltype (std::declval<zlink::framework::session_actor_t &> ().relay (
                                std::declval<const zlink::message_t &> ())),
                              zlink::framework::task_t<void>>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::session_actor_t &> ().relay_request (
                   std::declval<const zlink::message_t &> ())),
                 zlink::framework::relay_request_call_t>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                             .peer_connections ()),
                 zlink::framework::mesh_peer_connections_t &>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_peer_connections_t &> ()
                             .connect ("tcp://127.0.0.1:5503")),
                 void>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_peer_connections_t &> ()
                             .connect (zlink::routing_id_t::from ("peer"),
                                       "tcp://127.0.0.1:5503")),
                 void>);

static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::zlink_framework_options_t &> ()
                             .dispatch_options ()),
                 zlink::framework::dispatch_options_t>);

static_assert (!has_framework_use_discovery<zlink::framework::zlink_framework_options_t>);
static_assert (!has_framework_add_registry_peer<zlink::framework::zlink_framework_options_t>);
static_assert (!has_zlink_enable_registry<zlink::framework::zlink_builder_t>);
static_assert (!has_zlink_discovery<zlink::framework::zlink_builder_t>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::message_metadata_policy_t &> ()
                             .add_forwarded_metadata_key ("trace-id")),
                 zlink::framework::message_metadata_policy_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::spot_actor_message_metadata_t &> ()
                             .find ("trace-id")),
                 std::optional<std::string_view>>);

static_assert (
  std::is_same_v<decltype (std::declval<const zlink::framework::spot_actor_message_metadata_t &> ()
                             .contains ("trace-id")),
                 bool>);

static_assert (
  std::is_same_v<
    decltype (std::declval<const zlink::framework::spot_actor_message_metadata_t &> ().empty ()),
    bool>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::metadata_policy_builder_t &> ()
                             .add_forwarded_metadata_key ("trace-id")),
                 zlink::framework::metadata_policy_builder_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::config_builder_t &> ()
                                          .bind_required<typed_config_t> ("server")),
                              typed_config_t>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::config_builder_t &> ().load_json (
                   "appsettings.development.json", zlink::framework::optional_t::yes)),
                 zlink::framework::config_builder_t &>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::config_builder_t &> ()
                                          .use_environment ("development")),
                              zlink::framework::config_builder_t &>);

static_assert (std::is_same_v<decltype (std::declval<const zlink::framework::config_builder_t &> ()
                                          .environment ()),
                              std::string>);

static_assert (std::is_same_v<decltype (std::declval<const zlink::framework::config_builder_t &> ()
                                          .is_environment ("development")),
                              bool>);

class named_handler_t
{
  public:
    named_reply_t handle (const named_request_t &) { return {}; }
    named_reply_t handle_context (const named_context_request_t &,
                                  const zlink::framework::request_context_t &)
    {
        return {};
    }
    void send_context (const named_request_t &, const zlink::framework::send_context_t &) {}
    void publish_context (const named_request_t &, const zlink::framework::publish_context_t &) {}
};

class alias_registered_handler_t
{
  public:
    using request_type = named_request_t;
    using reply_type = named_reply_t;
    static constexpr const char *topic_name = "alias-topic";

    reply_type handle (const request_type &) { return {}; }
};

class named_filter_t
{
  public:
    zlink::framework::task_t<zlink::message_t>
    invoke (const zlink::framework::handler_invocation_context_t &,
            zlink::framework::handler_next_t next)
    {
        return next ();
    }
};

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::handler_registry_t &> ()
                                          .use_filter<named_filter_t> ()),
                              zlink::framework::handler_registry_t &>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::handler_invocation_context_t> ().context),
                 zlink::framework::handler_context_t>);

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::handler_invocation_context_t> ().message),
                 std::shared_ptr<const zlink::message_t>>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::route_client_t &> ().send_to_spot (
                                std::declval<zlink::framework::spot_handle_t> (),
                                std::declval<named_request_t> ())),
                              zlink::framework::route_send_call_t>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::route_client_t &> ()
                                         .send_to_channel (
                                           std::declval<std::string> (),
                                           std::declval<named_request_t> ())),
                              zlink::framework::route_send_call_t>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::route_client_t &> ()
                                         .request_to_channel (
                                           std::declval<std::string> (),
                                           std::declval<named_request_t> ())),
                              zlink::framework::channel_request_call_t>);

static_assert (std::is_same_v<decltype (std::declval<zlink::framework::route_client_t &> ()
                                          .request_to_spot (
                                            std::declval<zlink::framework::spot_handle_t> (),
                                            std::declval<named_request_t> ())),
                              zlink::framework::channel_request_call_t>);

} // namespace

int main ()
{
    zlink::framework::request_call_t<int> call (zlink::framework::detail::boundary_failure<int> (zlink::framework::detail::boundary_error_t::timed_out, "timeout"));

    auto task = call.submit ();
    const auto coroutine_result = task.result ();
    if (coroutine_result || coroutine_result.error () == nullptr
        || zlink::framework::detail::boundary_state (*coroutine_result.error ())
             != zlink::framework::detail::boundary_error_t::timed_out) {
        return 1;
    }

    zlink::framework::request_call_t<int> shutdown_call (zlink::framework::detail::boundary_failure<int> (zlink::framework::detail::boundary_error_t::shutdown, "shutdown"));

    const auto shutdown_result = shutdown_call.submit ().result ();
    if (shutdown_result || shutdown_result.error () == nullptr
        || zlink::framework::detail::boundary_state (*shutdown_result.error ())
             != zlink::framework::detail::boundary_error_t::shutdown) {
        return 2;
    }

    const zlink::framework::framework_error_kind_t retriable_kinds[] = {
      zlink::framework::framework_error_kind_t::route_not_connected,
      zlink::framework::framework_error_kind_t::actor_location_stale,
      zlink::framework::framework_error_kind_t::actor_moving,
      zlink::framework::framework_error_kind_t::deadline_exceeded,
      zlink::framework::framework_error_kind_t::placement_capacity_exhausted,
      zlink::framework::framework_error_kind_t::spot_moving};
    for (const auto kind : retriable_kinds) {
        if (!zlink::framework::framework_exception_t (kind, "retriable").is_retriable ()) {
            return 3;
        }
    }

    const zlink::framework::framework_error_kind_t non_retriable_kinds[] = {
      zlink::framework::framework_error_kind_t::actor_route_not_found,
      zlink::framework::framework_error_kind_t::actor_create_failed,
      zlink::framework::framework_error_kind_t::actor_already_exists,
      zlink::framework::framework_error_kind_t::actor_type_mismatch,
      zlink::framework::framework_error_kind_t::spot_create_failed,
      zlink::framework::framework_error_kind_t::spot_route_not_found,
      zlink::framework::framework_error_kind_t::spot_type_mismatch,
      zlink::framework::framework_error_kind_t::actor_session_not_bound,
      zlink::framework::framework_error_kind_t::handler_not_found,
      zlink::framework::framework_error_kind_t::route_handler_not_found,
      zlink::framework::framework_error_kind_t::actor_dispatch_handler_not_found,
      zlink::framework::framework_error_kind_t::payload_decode_failed,
      zlink::framework::framework_error_kind_t::request_target_not_found,
      zlink::framework::framework_error_kind_t::request_rejected,
      zlink::framework::framework_error_kind_t::request_protocol_error,
      zlink::framework::framework_error_kind_t::request_failed,
      zlink::framework::framework_error_kind_t::worker_queue_full,
      zlink::framework::framework_error_kind_t::worker_timed_out,
      zlink::framework::framework_error_kind_t::worker_failed,
      zlink::framework::framework_error_kind_t::actor_create_rejected,
      zlink::framework::framework_error_kind_t::runtime_shutdown};
    for (const auto kind : non_retriable_kinds) {
        if (zlink::framework::framework_exception_t (kind, "non-retriable").is_retriable ()) {
            return 4;
        }
    }

    zlink::framework::handler_registry_t handlers;
    handlers.on_request<named_handler_t, named_request_t, named_reply_t> ("sample", "topic",
                                                                          &named_handler_t::handle);
    handlers.on_request<named_handler_t, named_context_request_t, named_reply_t> (
      "sample", "context-topic", &named_handler_t::handle_context);
    handlers.on_send<named_handler_t, named_request_t> ("sample", "send-topic",
                                                        &named_handler_t::send_context);
    handlers.on_event<named_handler_t, named_request_t> ("sample", "publish-topic",
                                                         &named_handler_t::publish_context);
    const auto *descriptor = handlers.find ("sample", "topic", named_request_t::packet_name);
    if (descriptor == nullptr || descriptor->packet_name != named_request_t::packet_name) {
        return 5;
    }

    zlink::framework::service_collection_t services;
    zlink::framework::handler_registry_t option_handlers;
    zlink::framework::serializer_registry_t serializers;
    zlink::framework::zlink_builder_t zlink;
    zlink::framework::monitoring_builder_t monitoring;
    zlink::framework::zlink_framework_options_t options (services, option_handlers, serializers,
                                                         zlink, monitoring);
    options.use_filter<named_filter_t> ();
    options.handlers ().group ("sample").add<alias_registered_handler_t> ();

    return 0;
}
