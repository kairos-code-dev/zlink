/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/mesh_node_runtime.hpp"
#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/mesh/mesh_node_host_service.hpp"
#include "runtime/mesh/mesh_metadata_codec.hpp"
#include "runtime/mesh/route_mesh_runtime_options_service.hpp"
#include "runtime/mesh/route_mesh_runtime_service.hpp"
#include "runtime/messaging/client_call_codec.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__unix__)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
using namespace std::chrono_literals;

class monitoring_mesh_store_t final :
    public zlink::framework::mesh_node_location_store_t
{
  public:
    zlink::framework::mesh_node_descriptor_t descriptor;

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_mesh_node (
      zlink::framework::mesh_node_descriptor_t value,
      zlink::framework::location_write_intent_t) override
    {
        descriptor = std::move (value);
        return zlink::framework::task_t<
          zlink::framework::location_write_result_t> (
          zlink::framework::result_t<
            zlink::framework::location_write_result_t>::success (
            zlink::framework::location_write_result_t::stored (1, {})));
    }

    zlink::framework::task_t<zlink::framework::location_write_status_t>
    remove_mesh_node (
      zlink::framework::mesh_node_descriptor_key_t,
      zlink::framework::location_owner_token_t) override
    {
        return zlink::framework::task_t<
          zlink::framework::location_write_status_t> (
          zlink::framework::result_t<
            zlink::framework::location_write_status_t>::success (
            zlink::framework::location_write_status_t::stored));
    }

    zlink::framework::task_t<zlink::framework::location_page_t<
      zlink::framework::mesh_node_descriptor_t>>
    list_mesh_nodes (
      std::string mesh_name,
      zlink::framework::location_page_request_t = {}) override
    {
        zlink::framework::location_page_t<
          zlink::framework::mesh_node_descriptor_t> page;
        if (descriptor.mesh_name == mesh_name)
            page.items.push_back (descriptor);
        return zlink::framework::task_t<decltype (page)> (
          zlink::framework::result_t<decltype (page)>::success (
            std::move (page)));
    }
};

static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::zlink_builder_t &> ().add_route_mesh (
                   std::declval<std::string> ())),
                 zlink::framework::mesh_node_builder_t>);
static_assert (
  std::is_same_v<decltype (std::declval<zlink::framework::mesh_node_builder_t &> ().channel_name (
                   std::declval<std::string> ())),
                 zlink::framework::mesh_channel_builder_t>);
struct contract_entry_spot_t;
struct contract_spot_t;
struct contract_actor_factory_t;
struct contract_actor_t;
struct contract_transfer_adapter_t;
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .add_entry_spot<contract_entry_spot_t> ()),
               zlink::framework::mesh_node_builder_t &>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .add_entry_spot<contract_entry_spot_t> (
                             std::declval<std::function<
                               std::shared_ptr<contract_entry_spot_t> ()>> ())),
               zlink::framework::mesh_node_builder_t &>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .add_spot<contract_spot_t> (std::declval<std::string> ())),
               zlink::framework::mesh_node_builder_t &>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .add_spot<contract_spot_t> (
                             std::declval<std::string> (),
                             std::declval<
                               std::function<std::shared_ptr<contract_spot_t> ()>> ())),
               zlink::framework::mesh_node_builder_t &>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .add_actor_factory<contract_actor_factory_t> (
                             std::declval<std::string> ())),
               zlink::framework::mesh_node_builder_t &>);
static_assert (std::is_same_v<
               decltype (std::declval<zlink::framework::mesh_node_builder_t &> ()
                           .add_actor_transfer_adapter<contract_actor_t,
                                                       contract_transfer_adapter_t> (
                             std::declval<std::string> ())),
               zlink::framework::mesh_node_builder_t &>);

bool wait_until_admitted (zlink::framework::detail::mesh_node_runtime_t &node)
{
    // The transport drains its socket monitor from dispatch_ready, so a waiter
    // pumps the node the way the host service does instead of sleeping blind.
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    while (std::chrono::steady_clock::now () < deadline) {
        if (node.admitted_peer_count () > 0)
            return true;
        (void) node.dispatch_ready (
          [] (const zlink::framework::runtime::host::ready_record_t &,
              const zlink::framework::runtime::host::receive_record_t &,
              std::vector<zlink::message_t>) {});
        std::this_thread::sleep_for (10ms);
    }
    std::fprintf (stderr, "[vertical] admission timeout rid=%s peers=%zu state=%d\n",
                  node.status ().routing_id ().to_string ().c_str (),
                  node.admitted_peer_count (),
                  static_cast<int> (node.status ().state));
    return false;
}

bool wait_until_admitted_count (zlink::framework::detail::mesh_node_runtime_t &node,
                                std::size_t expected)
{
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    while (std::chrono::steady_clock::now () < deadline) {
        if (node.admitted_peer_count () >= expected)
            return true;
        (void) node.dispatch_ready (
          [] (const zlink::framework::runtime::host::ready_record_t &,
              const zlink::framework::runtime::host::receive_record_t &,
              std::vector<zlink::message_t>) {});
        std::this_thread::sleep_for (10ms);
    }
    return false;
}

#if defined(__unix__)
std::string reserve_loopback_endpoint ()
{
    const int socket_fd = socket (AF_INET, SOCK_STREAM, 0);
    assert (socket_fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    address.sin_port = 0;
    assert (bind (socket_fd, reinterpret_cast<sockaddr *> (&address),
                  sizeof (address))
            == 0);
    socklen_t size = sizeof (address);
    assert (getsockname (socket_fd, reinterpret_cast<sockaddr *> (&address), &size)
            == 0);
    close (socket_fd);
    return "tcp://127.0.0.1:" + std::to_string (ntohs (address.sin_port));
}
#endif

template <typename TSubmit> bool submit_until_ok (TSubmit submit)
{
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    zlink::submit_result_t last = zlink::submit_result_t::internal_error;
    while (std::chrono::steady_clock::now () < deadline) {
        last = submit ();
        if (last == zlink::submit_result_t::ok)
            return true;
        std::this_thread::sleep_for (10ms);
    }
    std::cerr << "last submit result=" << static_cast<int> (last) << '\n';
    return false;
}

bool receive_one (zlink::framework::detail::mesh_node_runtime_t &node,
                  zlink::framework::runtime::host::record_kind_t expected_kind,
                  const std::string &expected_text,
                  const std::vector<std::uint8_t> &expected_metadata)
{
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    while (std::chrono::steady_clock::now () < deadline) {
        bool matched = false;
        (void) node.dispatch_ready (
          [&] (const zlink::framework::runtime::host::ready_record_t &,
               const zlink::framework::runtime::host::receive_record_t &record,
               std::vector<zlink::message_t> parts) {
              matched = matched
                        || (record.kind == expected_kind && !parts.empty ()
                            && parts.front ().to_string () == expected_text);
          });
        if (matched)
            return true;
        std::this_thread::sleep_for (5ms);
    }
    return false;
}

bool reply_to_one_request (zlink::framework::detail::mesh_node_runtime_t &node,
                           zlink::framework::runtime::host::record_kind_t expected_kind,
                           const std::string &expected_text,
                           const std::string &reply_text)
{
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    while (std::chrono::steady_clock::now () < deadline) {
        bool replied = false;
        (void) node.dispatch_ready (
          [&] (const zlink::framework::runtime::host::ready_record_t &,
               const zlink::framework::runtime::host::receive_record_t &record,
               std::vector<zlink::message_t> parts) {
              if (record.kind == expected_kind && !parts.empty ()
                  && parts.front ().to_string () == expected_text) {
                  const std::vector<zlink::message_t> reply_parts{
                    zlink::message_t::from (reply_text)};
                  replied = zlink::framework::runtime::host::reply (record.reply_token, reply_parts)
                            == zlink::submit_result_t::ok;
              }
          });
        if (replied)
            return true;
        std::this_thread::sleep_for (5ms);
    }
    return false;
}

bool receive_completion (zlink::framework::detail::mesh_node_runtime_t &node,
                         const zlink::framework::runtime::host::operation_id_t &operation_id,
                         const std::string &expected_text)
{
    // v11: Core service pull batches are gone. The Framework MeshNode runtime
    // pushes ready records through dispatch_ready, so the completion is matched
    // on that callback instead of drain_ready/recv_batch claims.
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    while (std::chrono::steady_clock::now () < deadline) {
        bool matched = false;
        (void) node.dispatch_ready (
          [&] (const zlink::framework::runtime::host::ready_record_t &,
               const zlink::framework::runtime::host::receive_record_t &record,
               std::vector<zlink::message_t> parts) {
              matched =
                matched
                || (record.kind
                      == zlink::framework::runtime::host::record_kind_t::completion
                    && record.operation_id == operation_id && record.terminal_result == 0
                    && !parts.empty () && parts.front ().to_string () == expected_text);
          });
        if (matched)
            return true;
        std::this_thread::sleep_for (5ms);
    }
    return false;
}

std::shared_ptr<zlink::framework::detail::mesh_node_builder_state_t>
make_node (std::string endpoint, std::string routing_id)
{
    auto state =
      std::make_shared<zlink::framework::detail::mesh_node_builder_state_t> ("vertical-mesh");
    state->listen_endpoint = std::move (endpoint);
    state->routing_id = zlink::routing_id_t::from (routing_id);
    state->channels.emplace ("work",
                             zlink::framework::detail::mesh_channel_registration_t{});
    // The host admits object creation only for declared stable types.
    state->spot_state->snapshot.actor_types.emplace_back ("vertical.actor");
    return state;
}

std::shared_ptr<zlink::framework::detail::mesh_node_builder_state_t>
make_named_node (std::string mesh_name, std::string routing_id)
{
    auto state =
      std::make_shared<zlink::framework::detail::mesh_node_builder_state_t> (
        std::move (mesh_name));
    state->listen_endpoint = "tcp://127.0.0.1:0";
    state->routing_id = zlink::routing_id_t::from (std::move (routing_id));
    state->channels.emplace ("work",
                             zlink::framework::detail::mesh_channel_registration_t{});
    // The host admits object creation only for declared stable types.
    state->spot_state->snapshot.actor_types.emplace_back ("vertical.actor");
    return state;
}

struct local_route_probe_message_t
{
    std::string value;
};

struct local_route_probe_state_t
{
    std::mutex mutex;
    std::condition_variable changed;
    bool gate_open = false;
    int entered = 0;
    int completed = 0;
    std::vector<std::string> values;
};

class local_route_probe_handler_t
{
  public:
    explicit local_route_probe_handler_t (std::shared_ptr<local_route_probe_state_t> state) :
        _state (std::move (state))
    {
    }

    void handle (const local_route_probe_message_t &message,
                 const zlink::framework::route_message_context_t &)
    {
        std::unique_lock lock (_state->mutex);
        ++_state->entered;
        _state->values.push_back (message.value);
        _state->changed.notify_all ();
        if (message.value == "throw")
            throw std::runtime_error ("local route probe failure");
        _state->changed.wait (lock, [this] { return _state->gate_open; });
        ++_state->completed;
        _state->changed.notify_all ();
    }

  private:
    std::shared_ptr<local_route_probe_state_t> _state;
};

void verify_local_node_submit_bridge ()
{
    auto registration = make_node ("tcp://127.0.0.1:0", "local-route-node");
    registration->max_pending = 1;
    registration->handlers.on_send<local_route_probe_handler_t, local_route_probe_message_t> (
      "vertical-mesh", "LocalRouteProbe", &local_route_probe_handler_t::handle);

    zlink::framework::serializer_registry_t serializers;
    serializers.add<local_route_probe_message_t> (
      [] (const local_route_probe_message_t &message) {
          return zlink::framework::encoded_payload_t::from_string (message.value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return local_route_probe_message_t{payload.to_string ()};
      });
    auto probe = std::make_shared<local_route_probe_state_t> ();
    zlink::framework::service_collection_t services;
    services.add_singleton<local_route_probe_handler_t> (
      std::make_unique<local_route_probe_handler_t> (probe));
    // v11: the MeshNode host resolves the Location store from the provider, so
    // a vertical check registers the in-memory store like any application.
    auto owned_store =
      std::make_unique<zlink::framework::runtime::in_memory_location_store_t> ();
    auto &location_store = *owned_store;
    services.add_singleton<zlink::framework::location_store_t> (
      std::unique_ptr<zlink::framework::location_store_t> (owned_store.release ()));
    services.add_singleton<zlink::framework::runtime::location_runtime_t> (
      std::make_unique<zlink::framework::runtime::location_runtime_t> (location_store));
    auto provider = services.build_provider ();
    // The MeshNode publishes its descriptor under an owner lease, so the
    // Location runtime starts first just as the host does in production.
    provider.get_required<zlink::framework::runtime::location_runtime_t> ().start (
      *registration->routing_id);
    zlink::framework::runtime::mesh_node_host_service_t service (
      {registration}, serializers);
    service.start (provider);
    const auto node = service.nodes ().front ();

    auto encode = [&serializers] (std::string value) {
        zlink::framework::runtime::messaging::client_call_codec_t codec;
        const auto header = codec.create_envelope (
          zlink::framework::runtime::messaging::message_kind_t::command,
          "vertical-mesh", "LocalRouteProbe");
        return codec.encode_envelope_parts (
          header, local_route_probe_message_t{std::move (value)}, serializers);
    };

    {
        auto parts = encode ("owned-after-return");
        assert (service.submit_local_node_send (node, parts.items ())
                == zlink::submit_result_t::ok);
    }
    {
        std::unique_lock lock (probe->mutex);
        assert (probe->changed.wait_for (lock, 1s, [&] { return probe->entered == 1; }));
        assert (probe->completed == 0);
    }
    auto rejected = encode ("capacity-rejected");
    assert (service.submit_local_node_send (node, rejected.items ())
            == zlink::submit_result_t::backpressured);

    {
        std::lock_guard lock (probe->mutex);
        probe->gate_open = true;
    }
    probe->changed.notify_all ();
    {
        std::unique_lock lock (probe->mutex);
        assert (probe->changed.wait_for (lock, 1s, [&] { return probe->completed == 1; }));
        assert (probe->values == std::vector<std::string>{"owned-after-return"});
    }
    assert (service.wait_for_accepted_callbacks_until (
      std::chrono::steady_clock::now () + 1s));

    auto throwing = encode ("throw");
    assert (service.submit_local_node_send (node, throwing.items ())
            == zlink::submit_result_t::ok);
    assert (service.wait_for_accepted_callbacks_until (
      std::chrono::steady_clock::now () + 1s));
    auto after_throw = encode ("after-throw");
    assert (service.submit_local_node_send (node, after_throw.items ())
            == zlink::submit_result_t::ok);
    {
        std::unique_lock lock (probe->mutex);
        assert (probe->changed.wait_for (lock, 1s, [&] { return probe->completed == 2; }));
    }
    assert (service.wait_for_accepted_callbacks_until (
      std::chrono::steady_clock::now () + 1s));
    assert (node->pending_application_callbacks () == 0);
    assert (node->active_application_callbacks () == 0);

    service.seal_application_dispatch ();
    auto after_seal = encode ("after-seal");
    assert (service.submit_local_node_send (node, after_seal.items ())
            == zlink::submit_result_t::terminated);
    service.stop ();
}

void verify_public_runtime_surface ()
{
    auto registration = make_node ("tcp://127.0.0.1:0", "runtime-a");
    registration->actor_limit = 0;
    registration->spot_limit = 17;
    registration->activation_concurrency_limit = 5;
    registration->placement_weight = 42;
    auto node =
      std::make_shared<zlink::framework::detail::mesh_node_runtime_t> (registration);
    node->start ();
    monitoring_mesh_store_t monitoring_store;
    const auto node_status = node->status ();
    monitoring_store.descriptor.mesh_name = "vertical-mesh";
    monitoring_store.descriptor.rid = node_status.routing_id ();
    monitoring_store.descriptor.lifecycle_generation =
      node_status.lifecycle_generation ();
    monitoring_store.descriptor.descriptor_revision = 3;
    monitoring_store.descriptor.placement_weight = 42;
    monitoring_store.descriptor.capacity = {
      .actors = {.active = 4, .reserved = 2, .limit = 0},
      .spots = {.active = 3, .reserved = 1, .limit = 17},
      .spot_types =
        {{.object_kind =
            zlink::framework::placement_object_kind_t::user_spot,
          .stable_type = "room",
          .usage = {.active = 3, .reserved = 1, .limit = 9}}}};
    monitoring_store.descriptor.activation_concurrency = {
      .active = 2, .limit = 5};
    assert (registration->listen_endpoint == node->status ().local_endpoint ());
    assert (registration->listen_endpoint != "tcp://127.0.0.1:0");
    auto runtime =
      std::make_shared<zlink::framework::runtime::route_mesh_runtime_service_t> (
        std::vector<std::shared_ptr<zlink::framework::detail::mesh_node_runtime_t>>{
          node},
        nullptr,
        [] (std::chrono::milliseconds) {
            return zlink::framework::task_t<zlink::framework::drain_result_t> (
              zlink::framework::result_t<zlink::framework::drain_result_t>::success (
                zlink::framework::drained_t{}));
        },
        [] {
            return zlink::framework::task_t<zlink::framework::drain_result_t> (
              zlink::framework::result_t<zlink::framework::drain_result_t>::success (
                zlink::framework::drained_t{}));
        },
        &monitoring_store);
    runtime->start ();
    zlink::framework::runtime::route_mesh_runtime_options_service_t runtime_options (
      {node});

    const auto first = runtime->snapshot ("vertical-mesh");
    const auto second = runtime->snapshot ("vertical-mesh");
    assert (first.mesh_name == "vertical-mesh");
    assert (first.rid.to_string () == "runtime-a");
    assert (first.state == zlink::framework::mesh_node_state_t::serving);
    assert (first.placement_weight == 42);
    assert (first.object_capacity.actors.limit == 0);
    assert (first.object_capacity.actors.active == 4);
    assert (first.object_capacity.actors.reserved == 2);
    assert (first.object_capacity.spots.limit == 17);
    assert (first.object_capacity.spot_types.size () == 1);
    assert (first.object_capacity.spot_types.front ().usage.active == 3);
    assert (first.object_capacity.spot_types.front ().usage.reserved == 1);
    assert (first.object_capacity.spot_types.front ().usage.limit == 9);
    assert (first.activation_concurrency.active == 2);
    assert (first.activation_concurrency.limit == 5);
    assert (first.channels.size () == 1);
    assert (first.channels.front ().channel_name == "work");
    assert (first.channels.front ().ready_member_count == 1);
    assert (first.channels.front ().selectable);
    assert (second.sequence > first.sequence);
    auto &channel_options =
      runtime_options.channel ("vertical-mesh", "work");
    channel_options.weight (0);
    assert (channel_options.weight () == 0);
    assert (!runtime->snapshot ("vertical-mesh").channels.front ().selectable);
    channel_options.weight (100);
    assert (channel_options.weight () == 100);
    auto &node_options = runtime_options.mesh_node ("vertical-mesh");
    node_options.max_message_size (4096);
    assert (node_options.max_message_size () == 4096);
    node_options.max_message_size (0);
    assert (node_options.max_message_size () == 0);

    std::mutex event_mutex;
    std::condition_variable event_ready;
    std::optional<zlink::framework::mesh_runtime_event_t> received;
    auto observation = runtime->observe (
      "vertical-mesh", 1,
      [&] (const zlink::framework::mesh_runtime_event_t &event) {
          {
              std::lock_guard lock (event_mutex);
              received = event;
          }
          event_ready.notify_one ();
      });
    {
        std::unique_lock lock (event_mutex);
        assert (event_ready.wait_for (lock, 2s, [&] { return received.has_value (); }));
        assert (received->identifier == "zlink.runtime.mesh_node.state_changed");
        assert (received->mesh_name == "vertical-mesh");
    }
    observation->close ();

    bool rejected_capacity = false;
    try {
        (void) runtime->observe (
          "vertical-mesh", 0,
          [] (const zlink::framework::mesh_runtime_event_t &) {});
    }
    catch (const zlink::framework::framework_exception_t &) {
        rejected_capacity = true;
    }
    assert (rejected_capacity);

    bool rejected_mesh = false;
    try {
        (void) runtime->snapshot ("missing");
    }
    catch (const zlink::framework::framework_exception_t &) {
        rejected_mesh = true;
    }
    assert (rejected_mesh);
    assert (runtime->is_ready ("vertical-mesh"));
    assert (std::holds_alternative<zlink::framework::drained_t> (
      runtime->drain ("vertical-mesh", 1s).result ().value ()));

    runtime->stop ();
    node->stop ();
}

void verify_fixed_drain_callback_barrier ()
{
    auto registration = make_node ("tcp://127.0.0.1:0", "drain-barrier");
    zlink::framework::serializer_registry_t serializers;
    zlink::framework::runtime::mesh_node_host_service_t service (
      {registration}, serializers);
    const auto node = service.nodes ().front ();

    node->application_work_enqueued ();
    node->application_work_started ();
    service.seal_application_dispatch ();
    assert (!service.wait_for_accepted_callbacks_until (
      std::chrono::steady_clock::now () + 20ms));

    std::thread completion ([node] {
        std::this_thread::sleep_for (20ms);
        node->application_work_finished ();
    });
    assert (service.wait_for_accepted_callbacks_until (
      std::chrono::steady_clock::now () + 1s));
    completion.join ();
    assert (node->pending_application_callbacks () == 0);
    assert (node->active_application_callbacks () == 0);
}

void verify_multi_mesh_drain_fails_before_global_callback ()
{
    auto first = std::make_shared<zlink::framework::detail::mesh_node_runtime_t> (
      make_named_node ("mesh-a", "runtime-a"));
    auto second = std::make_shared<zlink::framework::detail::mesh_node_runtime_t> (
      make_named_node ("mesh-b", "runtime-b"));
    int drain_calls = 0;
    int await_calls = 0;
    zlink::framework::runtime::route_mesh_runtime_service_t runtime (
      {first, second}, nullptr,
      [&drain_calls] (std::chrono::milliseconds) {
          ++drain_calls;
          return zlink::framework::task_t<zlink::framework::drain_result_t> (
            zlink::framework::result_t<zlink::framework::drain_result_t>::success (
              zlink::framework::drained_t{}));
      },
      [&await_calls] {
          ++await_calls;
          return zlink::framework::task_t<zlink::framework::drain_result_t> (
            zlink::framework::result_t<zlink::framework::drain_result_t>::success (
              zlink::framework::drained_t{}));
      });

    bool drain_rejected = false;
    try {
        (void) runtime.drain ("mesh-a", 1s);
    }
    catch (const zlink::framework::framework_exception_t &error) {
        drain_rejected =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    assert (drain_rejected);
    assert (drain_calls == 0);

    bool await_rejected = false;
    try {
        (void) runtime.await_drained ("mesh-b");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        await_rejected =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    assert (await_rejected);
    assert (await_calls == 0);
}

#if defined(__unix__)
int run_cross_process_delivery ()
{
    const std::map<std::string, std::string> metadata_boundary{
      {"k", std::string (1018, 'v')}};
    const auto encoded_boundary =
      zlink::framework::detail::mesh_metadata_codec_t::encode (metadata_boundary);
    assert (encoded_boundary.size () == 1024);
    std::map<std::string, std::string> decoded_boundary;
    assert (zlink::framework::detail::mesh_metadata_codec_t::decode (
      encoded_boundary, decoded_boundary));
    assert (decoded_boundary == metadata_boundary);
    bool rejected_oversize = false;
    try {
        (void) zlink::framework::detail::mesh_metadata_codec_t::encode (
          {{"k", std::string (1019, 'v')}});
    }
    catch (const zlink::framework::framework_exception_t &) {
        rejected_oversize = true;
    }
    assert (rejected_oversize);
    std::map<std::string, std::string> malformed_output;
    assert (!zlink::framework::detail::mesh_metadata_codec_t::decode (
      {1, 1, 1, 0xc0, 0, 0}, malformed_output));
    assert (!zlink::framework::detail::mesh_metadata_codec_t::decode (
      {1, 2, 1, 'k', 0, 1, 'a', 1, 'k', 0, 1, 'b'}, malformed_output));

    int endpoint_pipe[2];
    int direct_ack_pipe[2];
    int channel_ack_pipe[2];
    int request_ack_pipe[2];
    int completion_ack_pipe[2];
    int spot_request_ack_pipe[2];
    int reciprocal_ready_pipe[2];
    int reciprocal_child_ready_pipe[2];
    int reciprocal_child_stop_pipe[2];
    int formal_descriptor_pipe[2];
    int formal_ack_pipe[2];
    if (pipe (endpoint_pipe) != 0 || pipe (direct_ack_pipe) != 0
        || pipe (channel_ack_pipe) != 0
        || pipe (request_ack_pipe) != 0
        || pipe (completion_ack_pipe) != 0
        || pipe (spot_request_ack_pipe) != 0
        || pipe (reciprocal_ready_pipe) != 0
        || pipe (reciprocal_child_ready_pipe) != 0
        || pipe (reciprocal_child_stop_pipe) != 0
        || pipe (formal_descriptor_pipe) != 0
        || pipe (formal_ack_pipe) != 0)
        return 1;

    const std::string reciprocal_endpoint = reserve_loopback_endpoint ();
    const pid_t child = fork ();
    if (child < 0)
        return 1;
    if (child == 0) {
        close (endpoint_pipe[0]);
        close (direct_ack_pipe[0]);
        close (channel_ack_pipe[0]);
        close (request_ack_pipe[0]);
        close (completion_ack_pipe[1]);
        close (spot_request_ack_pipe[0]);
        close (reciprocal_ready_pipe[0]);
        close (reciprocal_child_ready_pipe[0]);
        close (reciprocal_child_ready_pipe[1]);
        close (reciprocal_child_stop_pipe[0]);
        close (reciprocal_child_stop_pipe[1]);
        close (formal_descriptor_pipe[0]);
        close (formal_ack_pipe[0]);
        auto state = make_node ("tcp://127.0.0.1:*", "vertical-b");
        state->peer_connections.push_back (
          zlink::framework::mesh_peer_connection_t{
            2, zlink::routing_id_t::from (std::string ("vertical-c")),
            reciprocal_endpoint});
        zlink::framework::detail::mesh_node_runtime_t node (state);
        node.start ();
        auto target_spot = node.get_or_create_spot ("target-spot");
        auto target_actor = node.create_actor ("vertical.actor", "target-actor", {}, 5s);
        const std::uint64_t formal_descriptors[2]{
          target_spot.status ().lifecycle_generation (), target_actor.ref ().generation ()};
        if (write (formal_descriptor_pipe[1], formal_descriptors,
                   sizeof (formal_descriptors))
            != sizeof (formal_descriptors)) {
            _exit (8);
        }
        close (formal_descriptor_pipe[1]);
        const std::string endpoint = node.status ().local_endpoint ();
        const std::uint32_t size = static_cast<std::uint32_t> (endpoint.size ());
        if (write (endpoint_pipe[1], &size, sizeof (size)) != sizeof (size)
            || write (endpoint_pipe[1], endpoint.data (), endpoint.size ())
                 != static_cast<ssize_t> (endpoint.size ())) {
            _exit (2);
        }
        close (endpoint_pipe[1]);

        const bool multi_peer_admitted = wait_until_admitted_count (node, 2);
        const char reciprocal_ready = multi_peer_admitted ? 1 : 0;
        (void) write (reciprocal_ready_pipe[1], &reciprocal_ready,
                      sizeof (reciprocal_ready));
        close (reciprocal_ready_pipe[1]);

        const std::map<std::string, std::string> metadata{
          {"trace-id", "vertical-1"}, {"tenant", "sample"}};
        const auto encoded_metadata =
          zlink::framework::detail::mesh_metadata_codec_t::encode (metadata);
        const bool admitted = wait_until_admitted (node);
        const bool direct =
          receive_one (node, zlink::framework::runtime::host::record_kind_t::node_send, "direct",
                       encoded_metadata);
        const char direct_ack = direct ? 1 : 0;
        (void) write (direct_ack_pipe[1], &direct_ack, sizeof (direct_ack));
        close (direct_ack_pipe[1]);
        const bool channel =
          receive_one (node, zlink::framework::runtime::host::record_kind_t::channel_send, "channel",
                       encoded_metadata);
        const char channel_ack = channel ? 1 : 0;
        (void) write (channel_ack_pipe[1], &channel_ack, sizeof (channel_ack));
        close (channel_ack_pipe[1]);
        const bool request =
          reply_to_one_request (node, zlink::framework::runtime::host::record_kind_t::node_request,
                                "request", "reply");
        const char request_ack = request ? 1 : 0;
        (void) write (request_ack_pipe[1], &request_ack, sizeof (request_ack));
        close (request_ack_pipe[1]);
        char completion_ack = 0;
        if (request)
            (void) read (completion_ack_pipe[0], &completion_ack, sizeof (completion_ack));
        close (completion_ack_pipe[0]);
        const bool spot =
          receive_one (node, zlink::framework::runtime::host::record_kind_t::spot_send, "spot", encoded_metadata);
        const char spot_ack = spot ? 1 : 0;
        (void) write (formal_ack_pipe[1], &spot_ack, sizeof (spot_ack));
        const bool spot_request =
          reply_to_one_request (node, zlink::framework::runtime::host::record_kind_t::spot_request,
                                "spot-request", "spot-reply");
        const char spot_request_ack = spot_request ? 1 : 0;
        (void) write (spot_request_ack_pipe[1], &spot_request_ack,
                      sizeof (spot_request_ack));
        close (spot_request_ack_pipe[1]);
        const bool actor =
          receive_one (node, zlink::framework::runtime::host::record_kind_t::actor_send, "actor",
                       encoded_metadata);
        const char actor_ack = actor ? 1 : 0;
        (void) write (formal_ack_pipe[1], &actor_ack, sizeof (actor_ack));
        close (formal_ack_pipe[1]);
        node.stop ();
        int exit_code = 0;
        if (!admitted)
            exit_code = 3;
        else if (!multi_peer_admitted)
            exit_code = 12;
        else if (!direct)
            exit_code = 4;
        else if (!channel)
            exit_code = 5;
        else if (!request)
            exit_code = 6;
        else if (completion_ack != 1)
            exit_code = 7;
        else if (!spot)
            exit_code = 9;
        else if (!spot_request)
            exit_code = 11;
        else if (!actor)
            exit_code = 10;
        _exit (exit_code);
    }

    close (endpoint_pipe[1]);
    close (direct_ack_pipe[1]);
    close (channel_ack_pipe[1]);
    close (request_ack_pipe[1]);
    close (completion_ack_pipe[0]);
    close (spot_request_ack_pipe[1]);
    close (reciprocal_ready_pipe[1]);
    close (formal_descriptor_pipe[1]);
    close (formal_ack_pipe[1]);
    std::uint32_t endpoint_size = 0;
    if (read (endpoint_pipe[0], &endpoint_size, sizeof (endpoint_size))
        != sizeof (endpoint_size)) {
        return 1;
    }
    std::string endpoint (endpoint_size, '\0');
    if (read (endpoint_pipe[0], endpoint.data (), endpoint.size ())
        != static_cast<ssize_t> (endpoint.size ())) {
        return 1;
    }
    close (endpoint_pipe[0]);
    std::uint64_t formal_descriptors[2]{};
    assert (read (formal_descriptor_pipe[0], formal_descriptors,
                  sizeof (formal_descriptors))
            == sizeof (formal_descriptors));
    close (formal_descriptor_pipe[0]);

    const pid_t reciprocal_child = fork ();
    assert (reciprocal_child >= 0);
    if (reciprocal_child == 0) {
        close (reciprocal_child_ready_pipe[0]);
        close (reciprocal_child_stop_pipe[1]);
        auto reciprocal_state = make_node (reciprocal_endpoint, "vertical-c");
        reciprocal_state->peer_connections.push_back (
          zlink::framework::mesh_peer_connection_t{
            1, zlink::routing_id_t::from (std::string ("vertical-b")), endpoint});
        zlink::framework::detail::mesh_node_runtime_t reciprocal_node (reciprocal_state);
        reciprocal_node.start ();
        const char ready = wait_until_admitted (reciprocal_node) ? 1 : 0;
        (void) write (reciprocal_child_ready_pipe[1], &ready, sizeof (ready));
        close (reciprocal_child_ready_pipe[1]);
        char stop = 0;
        (void) read (reciprocal_child_stop_pipe[0], &stop, sizeof (stop));
        close (reciprocal_child_stop_pipe[0]);
        reciprocal_node.stop ();
        _exit (ready == 1 && stop == 1 ? 0 : 13);
    }
    close (reciprocal_child_ready_pipe[1]);
    close (reciprocal_child_stop_pipe[0]);
    char reciprocal_child_ready = 0;
    assert (read (reciprocal_child_ready_pipe[0], &reciprocal_child_ready,
                  sizeof (reciprocal_child_ready))
            == sizeof (reciprocal_child_ready));
    close (reciprocal_child_ready_pipe[0]);
    assert (reciprocal_child_ready == 1);

    auto state = make_node ("tcp://127.0.0.1:*", "vertical-a");
    state->peer_connections.push_back (
      zlink::framework::mesh_peer_connection_t{
        1, {}, endpoint});
    zlink::framework::detail::mesh_node_runtime_t node (state);
    node.start ();
    assert (wait_until_admitted (node));
    char reciprocal_ready = 0;
    assert (read (reciprocal_ready_pipe[0], &reciprocal_ready,
                  sizeof (reciprocal_ready))
            == sizeof (reciprocal_ready));
    close (reciprocal_ready_pipe[0]);
    assert (reciprocal_ready == 1);

    const std::map<std::string, std::string> metadata{
      {"trace-id", "vertical-1"}, {"tenant", "sample"}};
    const std::vector<zlink::message_t> direct_parts{
      zlink::message_t::from (std::string ("direct"))};
    assert (submit_until_ok ([&] {
        return node.send_to_node (
          zlink::routing_id_t::from (std::string ("vertical-b")), direct_parts, metadata);
    }));
    char direct_ack = 0;
    assert (read (direct_ack_pipe[0], &direct_ack, sizeof (direct_ack))
            == sizeof (direct_ack));
    close (direct_ack_pipe[0]);
    assert (direct_ack == 1);
    const std::vector<zlink::message_t> channel_parts{
      zlink::message_t::from (std::string ("channel"))};
    assert (submit_until_ok (
      [&] { return node.send_to_channel ("work", channel_parts, metadata); }));
    char channel_ack = 0;
    assert (read (channel_ack_pipe[0], &channel_ack, sizeof (channel_ack))
            == sizeof (channel_ack));
    close (channel_ack_pipe[0]);
    assert (channel_ack == 1);
    const std::vector<zlink::message_t> request_parts{
      zlink::message_t::from (std::string ("request"))};
    zlink::framework::runtime::host::operation_id_t operation_id;
    assert (node.request_to_node (
              zlink::routing_id_t::from (std::string ("vertical-b")), request_parts,
              operation_id, 5s, metadata)
            == zlink::submit_result_t::ok);
    char request_ack = 0;
    assert (read (request_ack_pipe[0], &request_ack, sizeof (request_ack))
            == sizeof (request_ack));
    close (request_ack_pipe[0]);
    assert (request_ack == 1);
    assert (receive_completion (node, operation_id, "reply"));
    const char completion_ack = 1;
    assert (write (completion_ack_pipe[1], &completion_ack, sizeof (completion_ack))
            == sizeof (completion_ack));
    close (completion_ack_pipe[1]);
    const std::vector<zlink::message_t> spot_parts{
      zlink::message_t::from (std::string ("spot"))};
    assert (submit_until_ok ([&] {
        return node.send_to_spot (
          "source-spot",
          zlink::routing_id_t::from (std::string ("vertical-b")),
          "target-spot",
          formal_descriptors[0], spot_parts,
          zlink::framework::detail::mesh_metadata_codec_t::encode (metadata));
    }));
    char spot_ack = 0;
    assert (read (formal_ack_pipe[0], &spot_ack, sizeof (spot_ack))
            == sizeof (spot_ack));
    assert (spot_ack == 1);
    const std::vector<zlink::message_t> spot_request_parts{
      zlink::message_t::from (std::string ("spot-request"))};
    zlink::framework::runtime::host::operation_id_t spot_operation_id;
    assert (node.request_to_spot (
              "source-spot",
              zlink::routing_id_t::from (std::string ("vertical-b")),
              "target-spot",
              formal_descriptors[0], spot_request_parts, spot_operation_id, 5s,
              zlink::framework::detail::mesh_metadata_codec_t::encode (metadata))
            == zlink::submit_result_t::ok);
    char spot_request_ack = 0;
    assert (read (spot_request_ack_pipe[0], &spot_request_ack,
                  sizeof (spot_request_ack))
            == sizeof (spot_request_ack));
    close (spot_request_ack_pipe[0]);
    assert (spot_request_ack == 1);
    assert (receive_completion (node, spot_operation_id, "spot-reply"));
    const std::vector<zlink::message_t> actor_parts{
      zlink::message_t::from (std::string ("actor"))};
    assert (submit_until_ok ([&] {
        return node.send_to_actor (
          zlink::framework::runtime::host::public_host_runtime_t::remote_actor_ref (
            zlink::routing_id_t::from (std::string ("vertical-b")), "target-actor",
            formal_descriptors[1]),
          actor_parts,
          zlink::framework::detail::mesh_metadata_codec_t::encode (metadata));
    }));
    char actor_ack = 0;
    assert (read (formal_ack_pipe[0], &actor_ack, sizeof (actor_ack))
            == sizeof (actor_ack));
    close (formal_ack_pipe[0]);
    assert (actor_ack == 1);

    int status = 0;
    waitpid (child, &status, 0);
    node.stop ();
    const char reciprocal_stop = 1;
    assert (write (reciprocal_child_stop_pipe[1], &reciprocal_stop,
                   sizeof (reciprocal_stop))
            == sizeof (reciprocal_stop));
    close (reciprocal_child_stop_pipe[1]);
    int reciprocal_status = 0;
    waitpid (reciprocal_child, &reciprocal_status, 0);
    assert (WIFEXITED (reciprocal_status));
    assert (WEXITSTATUS (reciprocal_status) == 0);
    return WIFEXITED (status) ? WEXITSTATUS (status) : 4;
}
#endif
} // namespace

int main ()
{
    verify_public_runtime_surface ();
    verify_fixed_drain_callback_barrier ();
    verify_multi_mesh_drain_fails_before_global_callback ();
    verify_local_node_submit_bridge ();
#if defined(__unix__)
    return run_cross_process_delivery ();
#else
    auto state = make_node ("tcp://127.0.0.1:*", "vertical-a");
    zlink::framework::detail::mesh_node_runtime_t node (state);
    node.start ();
    assert (node.status ().routing_id ().to_string () == "vertical-a");
    assert (node.status ().channel_count () == 1);

    const std::vector<std::uint8_t> metadata{0x01, 0x02, 0x03};
    const std::vector<zlink::message_t> direct_parts{
      zlink::message_t::from (std::string ("direct"))};
    const auto direct_result =
      node.send_to_node (*state->routing_id, direct_parts, metadata);
    assert (direct_result == zlink::submit_result_t::invalid_argument);

    const std::vector<zlink::message_t> channel_parts{
      zlink::message_t::from (std::string ("channel"))};
    const auto channel_result = node.send_to_channel ("work", channel_parts, metadata);
    assert (channel_result == zlink::submit_result_t::invalid_argument);

    node.stop ();
    return 0;
#endif
}
