/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/raw_mesh_node_owner.hpp"
#include "runtime/locations/service_descriptor_registry.hpp"
#include "runtime/fanout/raw_fanout_owner.hpp"
#include "runtime/client_server/raw_client_server_owner.hpp"
#include "runtime/client_server/weighted_selector.hpp"
#include "runtime/protocol/service_wire_codec.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mesh = zlink::framework::runtime::mesh;
namespace locations = zlink::framework::runtime::locations;
namespace fanout = zlink::framework::runtime::fanout;
namespace client_server = zlink::framework::runtime::client_server;
namespace foundation = zlink::framework::runtime::foundation;
namespace protocol = zlink::framework::runtime::protocol;
using namespace std::chrono_literals;

namespace
{

std::vector<std::uint8_t> bytes (std::string value)
{
    return {value.begin (), value.end ()};
}

mesh::service_node_descriptor_t descriptor (
  std::string rid,
  std::string endpoint = "tcp://127.0.0.1:0")
{
    return mesh::service_node_descriptor_t{
      "m6a-mesh", bytes (std::move (rid)), 1, 1, std::move (endpoint),
      {{"alpha", 100}, {"beta", 50}},
      mesh::service_node_state_t::preparing};
}

void verify_topology_snapshot_and_connection_fence ()
{
    mesh::service_topology_registry_t topology (descriptor ("local"));
    auto peer = descriptor ("peer", "tcp://127.0.0.1:7001");
    peer.state = mesh::service_node_state_t::serving;
    const auto first_connection = bytes ("connection-a");
    assert (topology.admit (peer, first_connection)
            == mesh::peer_admission_result_t::admitted);
    assert (topology.select ("alpha")->descriptor.node_routing_id == bytes ("peer"));

    auto older = peer;
    older.descriptor_revision = 0;
    assert (topology.admit (older, bytes ("ignored"))
            == mesh::peer_admission_result_t::invalid_descriptor);

    const auto replacement_connection = bytes ("connection-b");
    assert (topology.admit (peer, replacement_connection)
            == mesh::peer_admission_result_t::admitted);
    assert (!topology.disconnect (bytes ("peer"), first_connection));
    assert (topology.peer (bytes ("peer"))->connection_id
            == replacement_connection);

    auto equal_revision_mutation = peer;
    equal_revision_mutation.state = mesh::service_node_state_t::retiring;
    assert (topology.admit (equal_revision_mutation, replacement_connection)
            == mesh::peer_admission_result_t::stale_descriptor);

    peer.descriptor_revision = 2;
    peer.state = mesh::service_node_state_t::retiring;
    assert (topology.admit (peer, replacement_connection)
            == mesh::peer_admission_result_t::admitted);
    assert (!topology.select ("alpha"));
}

void verify_object_client_connection_requirement ()
{
    auto local = descriptor ("client-a");
    local.object_role = mesh::service_object_role_t::client;
    local.channels.clear ();
    auto remote = descriptor (
      "client-b", "tcp://127.0.0.1:7002");
    remote.object_role = mesh::service_object_role_t::client;
    remote.channels.clear ();
    remote.state = mesh::service_node_state_t::serving;

    assert (mesh::route_mesh_connection_not_required (
      local, remote));
    mesh::service_topology_registry_t topology (local);
    assert (topology.admit (
              remote, bytes ("client-only-connection"))
            == mesh::peer_admission_result_t::not_required);
    assert (topology.peers ().empty ());
    assert (topology.not_required_peers ().size () == 1);

    auto zero_weight_server = remote;
    zero_weight_server.descriptor_revision = 2;
    zero_weight_server.channels = {{"audit", 0}};
    assert (!mesh::route_mesh_connection_not_required (
      local, zero_weight_server));
    assert (topology.admit (
              zero_weight_server, bytes ("required-connection"))
            == mesh::peer_admission_result_t::admitted);
    assert (topology.peers ().size () == 1);
    assert (topology.not_required_peers ().empty ());

    assert (topology.admit (
              remote, bytes ("stale-client-only-connection"))
            == mesh::peer_admission_result_t::stale_descriptor);
    assert (topology.peers ().size () == 1);
    assert (topology.not_required_peers ().empty ());

    auto local_server_membership = local;
    local_server_membership.descriptor_revision = 2;
    local_server_membership.channels = {{"commands", 0}};
    assert (!mesh::route_mesh_connection_not_required (
      local_server_membership, remote));
}

void verify_manual_object_client_pair_ends_not_required ()
{
    auto first_descriptor = descriptor (
      "manual-client-a", "tcp://127.0.0.1:0");
    first_descriptor.object_role =
      mesh::service_object_role_t::client;
    first_descriptor.channels.clear ();
    auto second_descriptor = descriptor (
      "manual-client-b", "tcp://127.0.0.1:0");
    second_descriptor.object_role =
      mesh::service_object_role_t::client;
    second_descriptor.channels.clear ();

    mesh::raw_mesh_node_owner_t first (
      {first_descriptor, 16, 1024, 16, 1024});
    mesh::raw_mesh_node_owner_t second (
      {second_descriptor, 16, 1024, 16, 1024});
    first.start ();
    second.start ();
    assert (first.connect_peer (second.endpoint ()));

    const auto deadline =
      mesh::service_liveness_registry_t::clock_t::now () + 2s;
    while ((first.topology ().not_required_peers ().empty ()
            || second.topology ().not_required_peers ().empty ())
           && mesh::service_liveness_registry_t::clock_t::now ()
                < deadline) {
        const auto now =
          mesh::service_liveness_registry_t::clock_t::now ();
        (void) first.drain_monitor_events (now);
        (void) second.drain_monitor_events (now);
        (void) first.pump_one (now);
        (void) second.pump_one (now);
        std::this_thread::sleep_for (1ms);
    }

    assert (first.topology ().peers ().empty ());
    assert (second.topology ().peers ().empty ());
    assert (first.topology ().not_required_peers ().size () == 1);
    assert (second.topology ().not_required_peers ().size () == 1);
    assert (first.tick_liveness (
              mesh::service_liveness_registry_t::clock_t::now ()
              + 5s).probes.empty ());
    assert (second.tick_liveness (
              mesh::service_liveness_registry_t::clock_t::now ()
              + 5s).probes.empty ());

    first.close ();
    second.close ();
}

void verify_signed_weight_contract ()
{
    auto local = descriptor ("weight-local");
    local.channels = {{"weighted", 100}};
    local.placement_weight = 100;
    mesh::service_topology_registry_t topology (local);

    auto weight_100 =
      descriptor ("weight-100", "tcp://127.0.0.1:7101");
    weight_100.channels = {{"weighted", 100}};
    weight_100.state = mesh::service_node_state_t::serving;
    auto weight_300 =
      descriptor ("weight-300", "tcp://127.0.0.1:7102");
    weight_300.channels = {{"weighted", 300}};
    weight_300.state = mesh::service_node_state_t::serving;
    auto weight_zero =
      descriptor ("weight-zero", "tcp://127.0.0.1:7103");
    weight_zero.channels = {{"weighted", 0}};
    weight_zero.state = mesh::service_node_state_t::serving;
    assert (
      topology.admit (weight_100, bytes ("weight-connection-100"))
      == mesh::peer_admission_result_t::admitted);
    assert (
      topology.admit (weight_300, bytes ("weight-connection-300"))
      == mesh::peer_admission_result_t::admitted);
    assert (
      topology.admit (weight_zero, bytes ("weight-connection-zero"))
      == mesh::peer_admission_result_t::admitted);

    std::size_t selected_100 = 0;
    std::size_t selected_300 = 0;
    for (std::size_t index = 0; index < 400; ++index) {
        const auto selected = topology.select ("weighted");
        assert (selected);
        if (selected->descriptor.node_routing_id
            == bytes ("weight-100"))
            ++selected_100;
        else if (selected->descriptor.node_routing_id
                 == bytes ("weight-300"))
            ++selected_300;
        else
            assert (false);
    }
    assert (selected_100 == 100);
    assert (selected_300 == 300);

    const auto multicast =
      topology.multicast_targets ("weighted");
    assert (multicast.size () == 2);
    assert (std::count_if (
              multicast.begin (), multicast.end (),
              [] (const auto &peer) {
                  return peer.descriptor.node_routing_id
                         == bytes ("weight-100");
              })
            == 1);
    assert (std::count_if (
              multicast.begin (), multicast.end (),
              [] (const auto &peer) {
                  return peer.descriptor.node_routing_id
                         == bytes ("weight-300");
              })
            == 1);

    auto revision = weight_100;
    revision.descriptor_revision = 2;
    revision.channels.front ().weight = 0;
    assert (
      topology.admit (
        revision, bytes ("weight-connection-100"))
      == mesh::peer_admission_result_t::admitted);
    const auto after_revision =
      topology.multicast_targets ("weighted");
    assert (after_revision.size () == 1);
    assert (after_revision.front ().descriptor.node_routing_id
            == bytes ("weight-300"));

    auto invalid_negative = local;
    invalid_negative.channels.front ().weight = -1;
    bool rejected_negative = false;
    try {
        mesh::service_topology_registry_t invalid (
          invalid_negative);
    }
    catch (const std::invalid_argument &) {
        rejected_negative = true;
    }
    assert (rejected_negative);

    auto invalid_upper = local;
    invalid_upper.placement_weight = 10001;
    bool rejected_upper = false;
    try {
        mesh::service_topology_registry_t invalid (
          invalid_upper);
    }
    catch (const std::invalid_argument &) {
        rejected_upper = true;
    }
    assert (rejected_upper);

    std::vector<int> overflow_safe_weights (
      430000, 10000);
    assert (
      mesh::sum_service_weights (overflow_safe_weights)
      == 4'300'000'000ull);
}

void verify_independent_mailbox_domains_and_claim_fence ()
{
    mesh::service_mailbox_t mailbox (4, 64, 2, 32);
    assert (mailbox.try_enqueue (
      {"owner-a", mesh::service_mailbox_domain_t::application,
       {{1, 2, 3}}}));
    assert (mailbox.try_enqueue (
      {"owner-a", mesh::service_mailbox_domain_t::application,
       {{4, 5}}}));
    assert (mailbox.try_enqueue (
      {"peer-a", mesh::service_mailbox_domain_t::infrastructure,
       {{9}}}));
    assert (mailbox.try_enqueue (
      {"owner-b", mesh::service_mailbox_domain_t::application,
       {{7}}}));

    auto owner_b = mailbox.try_claim_owner (
      mesh::service_mailbox_domain_t::application, "owner-b", 1, 64);
    assert (owner_b && owner_b->records.size () == 1);
    assert (mailbox.release (*owner_b));

    auto application =
      mailbox.try_claim (mesh::service_mailbox_domain_t::application, 1, 64);
    assert (application && application->records.size () == 1);
    assert (!mailbox.try_claim (
      mesh::service_mailbox_domain_t::application, 1, 64));

    auto infrastructure =
      mailbox.try_claim (mesh::service_mailbox_domain_t::infrastructure, 1, 32);
    assert (infrastructure && infrastructure->records.size () == 1);
    assert (mailbox.release (*infrastructure));
    assert (!mailbox.release (*infrastructure));

    assert (mailbox.release (*application));
    auto remaining =
      mailbox.try_claim (mesh::service_mailbox_domain_t::application, 2, 64);
    assert (remaining && remaining->records.size () == 1);
    assert (mailbox.pending_messages (
              mesh::service_mailbox_domain_t::application)
            == 0);
    assert (mailbox.pending_bytes (
              mesh::service_mailbox_domain_t::application)
            == 0);
    assert (mailbox.release (*remaining));

    assert (mailbox.try_enqueue (
      {"owner-large", mesh::service_mailbox_domain_t::application,
       {std::vector<std::uint8_t> (20, 7)}}));
    auto oversized =
      mailbox.try_claim (mesh::service_mailbox_domain_t::application, 1, 1);
    assert (oversized && oversized->records.size () == 1);
    assert (mailbox.release (*oversized));
}

void verify_liveness_reuses_probe_and_fences_reconnect ()
{
    mesh::service_liveness_registry_t liveness (10ms, 30ms);
    const auto node = bytes ("peer");
    const auto first_connection = bytes ("connection-a");
    const auto replacement_connection = bytes ("connection-b");
    const auto start = mesh::service_liveness_registry_t::clock_t::now ();
    liveness.admit (node, first_connection, start);

    const auto first = liveness.tick (start + 10ms);
    assert (first.probes.size () == 1);
    const auto probe_id = first.probes.front ().probe_id;
    const auto retransmit = liveness.tick (start + 20ms);
    assert (retransmit.probes.size () == 1);
    assert (retransmit.probes.front ().probe_id == probe_id);
    assert (!liveness.acknowledge (
      node, first_connection, probe_id + 1, start + 21ms));
    assert (liveness.acknowledge (
      node, first_connection, probe_id, start + 21ms));

    liveness.admit (node, replacement_connection, start + 22ms);
    assert (!liveness.disconnect (node, first_connection));
    assert (!liveness.acknowledge (
      node, first_connection, probe_id, start + 23ms));
    assert (liveness.tick (start + 53ms).timed_out_nodes
            == std::vector<std::vector<std::uint8_t>>{node});
}

void verify_location_descriptor_cas_snapshot_and_watch ()
{
    locations::service_descriptor_registry_t registry;
    std::vector<locations::service_descriptor_event_t> events;
    const auto watch = registry.watch (
      {locations::service_descriptor_kind_t::client_server, "alpha"},
      [&events] (locations::service_descriptor_event_t event) {
          events.push_back (std::move (event));
      });
    locations::service_descriptor_record_t record{
      {locations::service_descriptor_kind_t::client_server, "alpha",
       bytes ("server-a")},
      11,
      1,
      "tcp://127.0.0.1:7001",
      "security-a",
      1024 * 1024,
      mesh::service_node_state_t::serving,
      100,
      "owner-a",
      17};
    assert (registry.publish (record, std::nullopt)
            == locations::service_descriptor_publish_status_t::inserted);
    auto stale = record;
    stale.descriptor_revision = 2;
    assert (registry.publish (stale, 9)
            == locations::service_descriptor_publish_status_t::conflict);
    auto immutable_mutation = record;
    immutable_mutation.descriptor_revision = 2;
    immutable_mutation.endpoint = "tcp://127.0.0.1:7002";
    assert (registry.publish (immutable_mutation, 1)
            == locations::service_descriptor_publish_status_t::conflict);
    auto updated = record;
    updated.descriptor_revision = 2;
    updated.weight = 0;
    updated.state = mesh::service_node_state_t::draining;
    assert (registry.publish (updated, 1)
            == locations::service_descriptor_publish_status_t::updated);
    const auto snapshot = registry.snapshot (
      {locations::service_descriptor_kind_t::client_server, "alpha"});
    assert (snapshot.change_stamp == 2);
    assert (snapshot.records == std::vector{updated});
    assert (!registry.remove (
      updated.key, 1, updated.owner_id, updated.owner_lease_generation));
    assert (registry.remove (
      updated.key, 2, updated.owner_id, updated.owner_lease_generation));
    assert (events.size () == 3);
    assert (events.back ().change
            == locations::service_descriptor_change_t::removed);
    assert (events.back ().change_stamp == 3);
    assert (registry.unwatch (watch));
    assert (!registry.unwatch (watch));
}

void verify_manual_and_automatic_classic_fanout ()
{
    fanout::raw_fanout_publisher_t publisher ("tcp://127.0.0.1:0");
    publisher.start ();
    fanout::raw_fanout_subscriber_t manual;
    const auto publisher_id = bytes ("publisher-a");
    assert (manual.connect_manual (publisher_id, publisher.endpoint ()));

    auto receive_now = std::chrono::steady_clock::now ();
    bool beacon_received = false;
    const auto deadline = receive_now + 2s;
    std::size_t beacon_tick = 1;
    while (!beacon_received && std::chrono::steady_clock::now () < deadline) {
        (void) publisher.tick (
          receive_now
          + fanout::fanout_beacon_interval
              * static_cast<int> (beacon_tick++));
        const auto [status, received] = manual.try_receive (receive_now);
        static_cast<void> (received);
        beacon_received = status == fanout::fanout_receive_status_t::beacon;
        if (!beacon_received) {
            std::this_thread::sleep_for (2ms);
        }
    }
    assert (beacon_received);
    assert (manual.ready (publisher_id));
    bool application_received = false;
    for (std::size_t attempt = 0; attempt < 100 && !application_received;
         ++attempt) {
        assert (publisher.publish (
          "topic-a",
          {"FanoutProbe", "application/json", bytes ("fanout")}));
        const auto [status, received] = manual.try_receive (receive_now);
        if (status == fanout::fanout_receive_status_t::application) {
            assert (received);
            assert (received->publisher_routing_id == publisher_id);
            assert (received->topic == "topic-a");
            const protocol::application_payload_t expected{
              "FanoutProbe", "application/json", bytes ("fanout")};
            assert (received->payload == expected);
            application_received = true;
        } else {
            std::this_thread::sleep_for (2ms);
        }
    }
    assert (application_received);
    assert (manual.tick (
              receive_now + fanout::fanout_receive_deadline)
            == std::vector<std::vector<std::uint8_t>>{publisher_id});
    assert (!manual.ready (publisher_id));

    fanout::raw_fanout_subscriber_t automatic;
    fanout::fanout_publisher_intent_t automatic_descriptor{
      publisher_id,
      1,
      publisher.endpoint (),
      mesh::service_node_state_t::serving};
    automatic.reconcile_automatic ({automatic_descriptor});
    assert (automatic.publisher_count () == 1);
    bool automatic_ready = false;
    for (std::size_t attempt = 0; attempt < 100 && !automatic_ready;
         ++attempt) {
        (void) publisher.tick (
          receive_now
          + fanout::fanout_beacon_interval
              * static_cast<int> (beacon_tick++));
        const auto [status, received] =
          automatic.try_receive (receive_now);
        static_cast<void> (received);
        automatic_ready =
          status == fanout::fanout_receive_status_t::beacon;
        if (!automatic_ready) {
            std::this_thread::sleep_for (2ms);
        }
    }
    assert (automatic_ready);
    assert (automatic.ready (publisher_id));

    automatic_descriptor.lifecycle_generation = 2;
    automatic.reconcile_automatic ({automatic_descriptor});
    assert (automatic.publisher_count () == 1);
    assert (!automatic.ready (publisher_id));

    automatic_descriptor.state = mesh::service_node_state_t::draining;
    automatic.reconcile_automatic ({automatic_descriptor});
    assert (automatic.publisher_count () == 0);

    bool reserved_rejected = false;
    try {
        static_cast<void> (publisher.publish (
          fanout::raw_fanout_publisher_t::reserved_topic (),
          {"Reserved", "application/json", {}}));
    }
    catch (const std::invalid_argument &) {
        reserved_rejected = true;
    }
    assert (reserved_rejected);
}

void verify_client_server_independent_raw_path ()
{
    protocol::client_server_server_admission_t server_descriptor{
      "client-server-alpha",
      bytes ("server-a"),
      41,
      1,
      100,
      mesh::service_node_state_t::preparing,
      "security-a",
      1024 * 1024,
      "tcp://127.0.0.1:0"};
    client_server::raw_client_server_server_t server (
      {{server_descriptor}});
    server.start ();
    auto expected_server = server.descriptor ();
    auto manual_server = expected_server;
    manual_server.server_routing_id.clear ();
    manual_server.lifecycle_generation = 0;
    manual_server.descriptor_revision = 0;
    client_server::raw_client_server_client_options_t client_options{
      bytes ("client-a"),
      {expected_server.channel_name, "security-a", 1024 * 1024},
      std::move (manual_server)};
    client_server::raw_client_server_client_t client (
      std::move (client_options));
    client.start ();
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    while (!client.ready () && std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        (void) server.drain_monitor_events (now);
        (void) client.drain_monitor_events (now);
        const auto server_pump = server.pump_one (now);
        const auto client_pump = client.pump_one (now);
        assert (
          server_pump
          != client_server::client_server_pump_result_t::protocol_error);
        assert (
          client_pump
          != client_server::client_server_pump_result_t::protocol_error);
        std::this_thread::sleep_for (1ms);
    }
    assert (client.ready ());

    const auto liveness_base = std::chrono::steady_clock::now ();
    const auto first_probe =
      client.tick_liveness (liveness_base + 5s);
    assert (first_probe.probes.size () == 1);
    client_server::client_server_pump_result_t probe_pump =
      client_server::client_server_pump_result_t::no_data;
    while (probe_pump
             == client_server::client_server_pump_result_t::no_data
           && std::chrono::steady_clock::now () < deadline) {
        probe_pump = server.pump_one (std::chrono::steady_clock::now ());
    }
    assert (probe_pump
            == client_server::client_server_pump_result_t::infrastructure);
    client_server::client_server_pump_result_t ack_pump =
      client_server::client_server_pump_result_t::no_data;
    while (ack_pump
             == client_server::client_server_pump_result_t::no_data
           && std::chrono::steady_clock::now () < deadline) {
        ack_pump = client.pump_one (std::chrono::steady_clock::now ());
    }
    assert (ack_pump
            == client_server::client_server_pump_result_t::infrastructure);
    const auto next_probe =
      client.tick_liveness (liveness_base + 10s);
    assert (next_probe.probes.size () == 1);
    assert (next_probe.probes.front ().probe_id
            != first_probe.probes.front ().probe_id);

    assert (client.send (
      {"ClientServerSend", "application/json", bytes ("send")}));
    client_server::client_server_pump_result_t send_pump =
      client_server::client_server_pump_result_t::no_data;
    while (send_pump
             != client_server::client_server_pump_result_t::application
           && std::chrono::steady_clock::now () < deadline) {
        send_pump = server.pump_one (std::chrono::steady_clock::now ());
    }
    assert (send_pump
            == client_server::client_server_pump_result_t::application);
    auto send_claim = server.mailbox ().try_claim (
      mesh::service_mailbox_domain_t::application, 1, 1024);
    assert (send_claim && send_claim->records.size () == 1);
    assert (protocol::decode_channel_send_header (
              send_claim->records.front ().parts.front ())
            == expected_server.channel_name);
    assert (server.mailbox ().release (*send_claim));

    using request_result_t =
      std::pair<foundation::operation_terminal_t,
                std::vector<std::uint8_t>>;
    std::promise<request_result_t> promise;
    auto future = promise.get_future ();
    assert (client.request (
      {"ClientServerRequest", "application/json", bytes ("request")},
      2s,
      [&promise] (
        foundation::operation_terminal_t terminal,
        std::vector<std::uint8_t> payload) {
          promise.set_value ({terminal, std::move (payload)});
      }));
    client_server::client_server_pump_result_t request_pump =
      client_server::client_server_pump_result_t::no_data;
    while (request_pump
             != client_server::client_server_pump_result_t::application
           && std::chrono::steady_clock::now () < deadline) {
        request_pump = server.pump_one (std::chrono::steady_clock::now ());
    }
    assert (request_pump
            == client_server::client_server_pump_result_t::application);
    auto request_claim = server.mailbox ().try_claim (
      mesh::service_mailbox_domain_t::application, 1, 1024);
    assert (request_claim && request_claim->records.size () == 1);
    assert (request_claim->records.front ().request_sequence);
    assert (request_claim->records.front ().correlation);
    assert (server.reply (
      request_claim->records.front (),
      {"ClientServerReply", "application/json", bytes ("reply")}));
    assert (server.mailbox ().release (*request_claim));
    while (future.wait_for (0ms) != std::future_status::ready
           && std::chrono::steady_clock::now () < deadline) {
        const auto pump = client.pump_one (std::chrono::steady_clock::now ());
        assert (
          pump != client_server::client_server_pump_result_t::protocol_error);
        std::this_thread::sleep_for (1ms);
    }
    assert (future.wait_for (0ms) == std::future_status::ready);
    const auto result = future.get ();
    assert (result.first == foundation::operation_terminal_t::completed);
    const protocol::application_payload_t expected_reply{
      "ClientServerReply", "application/json", bytes ("reply")};
    assert (protocol::decode_application_payload (result.second)
            == expected_reply);

}

void verify_client_server_weighted_selection ()
{
    client_server::smooth_weighted_selector_t selector;
    const std::vector<client_server::weighted_candidate_t> weighted{
      {"api-a", 300}, {"api-b", 100}, {"disabled", 0}};
    std::map<std::string, std::size_t> selected;
    for (std::size_t index = 0; index < 400; ++index) {
        const auto key = selector.select (weighted);
        assert (key);
        ++selected[*key];
        assert (selector.state_size () == 2);
        assert (selector.maximum_absolute_credit () <= 400);
    }
    assert (selected["api-a"] == 300);
    assert (selected["api-b"] == 100);
    assert (!selected.contains ("disabled"));

    const std::vector<client_server::weighted_candidate_t>
      after_api_b_enters_draining{
      {"api-a", 300}};
    for (std::size_t index = 0; index < 32; ++index) {
        const auto key = selector.select (
          after_api_b_enters_draining);
        assert (key && *key == "api-a");
        assert (selector.state_size () == 1);
        assert (selector.maximum_absolute_credit () <= 300);
    }

    const std::vector<client_server::weighted_candidate_t> none{
      {"disabled", 0}};
    assert (!selector.select (none));
    assert (selector.state_size () == 0);
}

void verify_raw_owner_node_send_and_liveness ()
{
    mesh::raw_mesh_node_owner_t first (
      mesh::raw_mesh_node_options_t{descriptor ("raw-a")});
    mesh::raw_mesh_node_owner_t second (
      mesh::raw_mesh_node_options_t{descriptor ("raw-b")});
    first.start ();
    second.start ();
    assert (first.started () && second.started ());

    auto first_descriptor = first.topology ().local_descriptor ();
    auto second_descriptor = second.topology ().local_descriptor ();
    const auto now = mesh::service_liveness_registry_t::clock_t::now ();
    const auto deadline = now + 5s;
    assert (first.connect_peer (second.endpoint (), second_descriptor));
    while ((!first.topology ().peer (second_descriptor.node_routing_id)
            || !second.topology ().peer (first_descriptor.node_routing_id))
           && mesh::service_liveness_registry_t::clock_t::now () < deadline) {
        const auto progress_now =
          mesh::service_liveness_registry_t::clock_t::now ();
        (void) first.drain_monitor_events (progress_now);
        (void) second.drain_monitor_events (progress_now);
        (void) first.pump_one (progress_now);
        (void) second.pump_one (progress_now);
        std::this_thread::sleep_for (1ms);
    }
    assert (first.topology ().peer (second_descriptor.node_routing_id));
    assert (second.topology ().peer (first_descriptor.node_routing_id));

    bool submitted = false;
    while (!submitted && mesh::service_liveness_registry_t::clock_t::now ()
                           < deadline) {
        try {
            submitted = first.send_to_node (
              second_descriptor.node_routing_id,
              {"Probe", "application/json", bytes ("payload")});
        }
        catch (...) {
        }
        if (!submitted) {
            std::this_thread::sleep_for (5ms);
        }
    }
    assert (submitted);

    mesh::raw_mesh_pump_result_t pumped =
      mesh::raw_mesh_pump_result_t::no_data;
    while (pumped != mesh::raw_mesh_pump_result_t::application
           && mesh::service_liveness_registry_t::clock_t::now () < deadline) {
        pumped = second.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (pumped != mesh::raw_mesh_pump_result_t::protocol_error);
        if (pumped != mesh::raw_mesh_pump_result_t::application) {
            std::this_thread::sleep_for (2ms);
        }
    }
    assert (pumped == mesh::raw_mesh_pump_result_t::application);
    auto claim = second.mailbox ().try_claim (
      mesh::service_mailbox_domain_t::application, 1, 1024);
    assert (claim && claim->records.size () == 1);
    assert (protocol::decode_header (
              claim->records.front ().parts.front ())
              .kind
            == protocol::command::nodeSend);
    const protocol::application_payload_t expected_payload{
      "Probe", "application/json", bytes ("payload")};
    assert (protocol::decode_application_payload (
              claim->records.front ().parts.at (1))
            == expected_payload);
    assert (second.mailbox ().release (*claim));

    bool channel_submitted = false;
    while (!channel_submitted
           && mesh::service_liveness_registry_t::clock_t::now () < deadline) {
        try {
            channel_submitted = first.send_to_channel (
              "alpha", {"ChannelProbe", "application/json", bytes ("channel")});
        }
        catch (...) {
        }
    }
    assert (channel_submitted);
    mesh::raw_mesh_pump_result_t channel_pump =
      mesh::raw_mesh_pump_result_t::no_data;
    while (channel_pump != mesh::raw_mesh_pump_result_t::application
           && mesh::service_liveness_registry_t::clock_t::now () < deadline) {
        channel_pump = second.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (channel_pump != mesh::raw_mesh_pump_result_t::protocol_error);
    }
    assert (channel_pump == mesh::raw_mesh_pump_result_t::application);
    auto channel_claim = second.mailbox ().try_claim (
      mesh::service_mailbox_domain_t::application, 1, 1024);
    assert (channel_claim && channel_claim->owner == "channel:alpha");
    assert (protocol::decode_channel_send_header (
              channel_claim->records.front ().parts.front ())
            == "alpha");
    assert (second.mailbox ().release (*channel_claim));

    using request_result_t =
      std::pair<foundation::operation_terminal_t,
                std::vector<std::uint8_t>>;
    std::promise<request_result_t> request_promise;
    auto request_future = request_promise.get_future ();
    assert (first.request_to_node (
      second_descriptor.node_routing_id,
      {"RequestProbe", "application/json", bytes ("request")},
      2s,
      [&request_promise] (
        foundation::operation_terminal_t terminal,
        std::vector<std::uint8_t> payload) mutable {
          request_promise.set_value (
            {terminal, std::move (payload)});
      }));
    mesh::raw_mesh_pump_result_t request_pump =
      mesh::raw_mesh_pump_result_t::no_data;
    while (request_pump != mesh::raw_mesh_pump_result_t::application
           && mesh::service_liveness_registry_t::clock_t::now () < deadline) {
        request_pump = second.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (request_pump != mesh::raw_mesh_pump_result_t::protocol_error);
    }
    assert (request_pump == mesh::raw_mesh_pump_result_t::application);
    auto request_claim = second.mailbox ().try_claim (
      mesh::service_mailbox_domain_t::application, 1, 1024);
    assert (request_claim && request_claim->records.size () == 1);
    const auto &request_record = request_claim->records.front ();
    assert (request_record.request_sequence);
    assert (request_record.correlation);
    assert (protocol::decode_node_request_header (
              request_record.parts.front ())
            == *request_record.correlation);
    assert (second.reply (
      request_record,
      {"RequestReply", "application/json", bytes ("reply")}));
    assert (second.mailbox ().release (*request_claim));
    const auto request_deadline =
      mesh::service_liveness_registry_t::clock_t::now () + 2s;
    while (request_future.wait_for (0ms) != std::future_status::ready
           && mesh::service_liveness_registry_t::clock_t::now ()
                < request_deadline) {
        const auto client_pump = first.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (client_pump != mesh::raw_mesh_pump_result_t::protocol_error);
        std::this_thread::sleep_for (1ms);
    }
    assert (request_future.wait_for (0ms) == std::future_status::ready);
    auto request_result = request_future.get ();
    assert (request_result.first
            == foundation::operation_terminal_t::completed);
    const protocol::application_payload_t expected_reply{
      "RequestReply", "application/json", bytes ("reply")};
    assert (protocol::decode_application_payload (request_result.second)
            == expected_reply);

    const auto liveness_base =
      mesh::service_liveness_registry_t::clock_t::now ();
    const auto first_probe = first.tick_liveness (liveness_base + 5s);
    assert (first_probe.probes.size () == 1);
    mesh::raw_mesh_pump_result_t probe_pump =
      mesh::raw_mesh_pump_result_t::no_data;
    while (probe_pump == mesh::raw_mesh_pump_result_t::no_data
           && mesh::service_liveness_registry_t::clock_t::now () < deadline) {
        probe_pump = second.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
    }
    assert (probe_pump == mesh::raw_mesh_pump_result_t::infrastructure);

    mesh::raw_mesh_pump_result_t ack_pump =
      mesh::raw_mesh_pump_result_t::no_data;
    while (ack_pump == mesh::raw_mesh_pump_result_t::no_data
           && mesh::service_liveness_registry_t::clock_t::now () < deadline) {
        ack_pump = first.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
    }
    assert (ack_pump == mesh::raw_mesh_pump_result_t::infrastructure);
    const auto next_probe = first.tick_liveness (liveness_base + 10s);
    assert (next_probe.probes.size () == 1);
    assert (next_probe.probes.front ().probe_id
            != first_probe.probes.front ().probe_id);

    first.close ();
    second.close ();
}

} // namespace

int main ()
{
    verify_topology_snapshot_and_connection_fence ();
    verify_object_client_connection_requirement ();
    verify_manual_object_client_pair_ends_not_required ();
    verify_signed_weight_contract ();
    verify_independent_mailbox_domains_and_claim_fence ();
    verify_liveness_reuses_probe_and_fences_reconnect ();
    verify_location_descriptor_cas_snapshot_and_watch ();
    verify_manual_and_automatic_classic_fanout ();
    verify_client_server_independent_raw_path ();
    verify_client_server_weighted_selection ();
    verify_raw_owner_node_send_and_liveness ();
    return 0;
}
