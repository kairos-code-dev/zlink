/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <cstdint>

namespace {

void test_registry_query_and_discovery_metadata ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());

    const std::string pub_endpoint =
      zlink_cpp_contract::unique_tcp ("registry-pub");
    const std::string router_endpoint =
      zlink_cpp_contract::unique_tcp ("registry-router");
    assert (registry.bind (pub_endpoint, router_endpoint) == 0);

    zlink_registry_status_t status;
    assert (registry.status_snapshot (status) == 0);

    size_t topology_count = 0;
    assert (registry.topology_snapshot (NULL, &topology_count) == 0);

    zlink::service::registry_query_client_t query (ctx);
    assert (query.valid ());
    assert (query.connect (router_endpoint) == 0);
    assert (query.snapshot (NULL, &topology_count) == 0);

    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "orders");
    assert (discovery.valid ());
    assert (discovery.connect_registry (router_endpoint) == 0);

    const int64_t value = 42;
    assert (discovery.set_value (value) == 0);

    int64_t got_value = 0;
    assert (discovery.get_value (&got_value) == 0);
    assert (got_value == value);

    assert (discovery.set_metadata ("meta-orders") == 0);
    zlink::message_t metadata;
    assert (discovery.get_metadata (metadata) == 0);
    assert (metadata.to_string () == "meta-orders");

    size_t peer_count = 0;
    assert (discovery.member_peers (NULL, &peer_count) == 0);
}

void test_spot_node_snapshot_and_service_monitor ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());

    const std::string pub_endpoint =
      zlink_cpp_contract::unique_tcp ("service-reg-pub");
    const std::string router_endpoint =
      zlink_cpp_contract::unique_tcp ("service-reg-router");
    assert (registry.bind (pub_endpoint, router_endpoint) == 0);

    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "service-monitor");
    assert (discovery.valid ());
    assert (discovery.connect_registry (router_endpoint) == 0);

    zlink::service::spot_node_t node (ctx);
    assert (node.valid ());

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("spot-node");
    assert (node.bind (endpoint) == 0);

    zlink_spot_node_status_t status;
    assert (node.status_snapshot (status) == 0);

    size_t peer_count = 0;
    assert (node.peers_snapshot (NULL, &peer_count) == 0);

    size_t subject_count = 0;
    assert (node.subjects_snapshot (NULL, &subject_count) == 0);

    zlink::service_monitor_handle_t monitor (discovery);
    assert (monitor.valid ());
}

void test_unified_spot_self_delivery_recv_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_t spot (ctx);
    assert (spot.valid ());

    zlink::service_monitor_handle_t sub_monitor (
      spot, zlink::service_monitor_event::spot_filter_applied
              | zlink::service_monitor_event::error);
    assert (sub_monitor.valid ());
    zlink::service_monitor_handle_t pub_monitor (
      spot.handle (), zlink::service_monitor_event::spot_first_delivery_ready_changed
                        | zlink::service_monitor_event::error);
    assert (pub_monitor.valid ());

    assert (spot.subscribe ("topic:service-self") == 0);
    assert (zlink_cpp_contract::wait_for_service_monitor_event (
      sub_monitor,
      static_cast<uint32_t> (
        zlink::service_monitor_event::spot_filter_applied),
      10000));
    assert (zlink_cpp_contract::wait_for_service_monitor_state (
      pub_monitor, ZLINK_MONITOR_STATE_SEND_READY, 10000));

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("service-self");
    assert (spot.publish ("topic:service-self", outbound) == 0);

    zlink::message_t inbound;
    std::string topic;
    assert (spot.recv (inbound, topic) == 0);
    assert (topic == "topic:service-self");
    assert (inbound.to_string () == "service-self");

    assert (pub_monitor.close () == 0);
    assert (sub_monitor.close () == 0);
}

} // namespace

int main ()
{
    test_registry_query_and_discovery_metadata ();
    test_spot_node_snapshot_and_service_monitor ();
    test_unified_spot_self_delivery_recv_contract ();
    return 0;
}
