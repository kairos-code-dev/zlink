/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

namespace {

bool wait_for_any_socket_monitor_event (zlink::monitor_handle_t &monitor_,
                                        int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (!zlink_cpp_contract::wait_for_monitor_readable (
              monitor_.handle (), remaining_ms)) {
            continue;
        }

        if (monitor_.try_recv ())
            return true;
    }

    return false;
}

void test_socket_monitor_open_recv_snapshot ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t server (ctx);
    zlink::pair_socket_t client (ctx);

    zlink::monitor_handle_t monitor = server.monitor_handle ();
    assert (monitor.valid ());

    assert (server.bind ("tcp://127.0.0.1:*") == 0);
    std::string endpoint;
    assert (server.get_option (zlink::socket_options::last_endpoint, endpoint)
            == 0);
    assert (!endpoint.empty ());
    assert (client.connect (endpoint) == 0);

    (void) monitor.try_recv ();
    assert (wait_for_any_socket_monitor_event (monitor, 2000));
    zlink_monitor_snapshot_t snapshot;
    assert (monitor.snapshot (snapshot) == 0);
}

void test_discovery_service_monitor_open_recv ()
{
    zlink::context_t ctx;
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "monitor-contract");
    assert (discovery.valid ());

    zlink::service_monitor_handle_t monitor = discovery.monitor_open (
      zlink::service_monitor_event::discovery_service_up);
    assert (monitor.valid ());
    (void) monitor.try_recv ();
}

} // namespace

int main ()
{
    test_socket_monitor_open_recv_snapshot ();
    test_discovery_service_monitor_open_recv ();
    return 0;
}
