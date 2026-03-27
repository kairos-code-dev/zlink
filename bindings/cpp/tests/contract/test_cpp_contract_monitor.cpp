/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

namespace {

void test_socket_monitor_open_recv_snapshot ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);

    zlink::monitor_handle_t monitor = server.monitor_handle ();
    assert (monitor.valid ());

    const std::string endpoint =
      zlink_cpp_contract::unique_tcp ("monitor-pair");
    assert (server.bind (endpoint) == 0);

    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      monitor, static_cast<uint64_t> (zlink::monitor_event::listening), 2000));
    zlink_monitor_snapshot_t snapshot;
    assert (monitor.snapshot (snapshot) == 0);
}

void test_service_monitor_open_snapshot ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());

    const std::string pub_endpoint =
      zlink_cpp_contract::unique_tcp ("monitor-reg-pub");
    const std::string router_endpoint =
      zlink_cpp_contract::unique_tcp ("monitor-reg-router");
    assert (registry.bind (pub_endpoint, router_endpoint) == 0);

    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "monitor-service");
    assert (discovery.valid ());
    assert (discovery.connect_registry (router_endpoint) == 0);

    zlink::service_monitor_handle_t monitor (discovery);
    assert (monitor.valid ());
}

} // namespace

int main ()
{
    test_socket_monitor_open_recv_snapshot ();
    test_service_monitor_open_snapshot ();
    return 0;
}
