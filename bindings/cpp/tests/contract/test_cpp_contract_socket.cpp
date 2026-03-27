/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

namespace {

void test_pair_send_recv_single_part ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t left (ctx);
    zlink::pair_socket_t right (ctx);
    zlink::monitor_handle_t left_monitor = left.monitor_handle ();
    zlink::monitor_handle_t right_monitor = right.monitor_handle ();

    const std::string endpoint = zlink_cpp_contract::unique_tcp ("pair");
    assert (left.bind (endpoint) == 0);
    assert (right.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed), 2000,
      1));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed), 2000,
      1));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("ping");
    assert (right.send (outbound, zlink::send_flag::dontwait) == 0);

    zlink::message_t inbound;
    assert (left.recv (inbound) == 0);
    assert (inbound.to_string () == "ping");
}

void test_pair_send_recv_multipart ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t left (ctx);
    zlink::pair_socket_t right (ctx);
    zlink::monitor_handle_t left_monitor = left.monitor_handle ();
    zlink::monitor_handle_t right_monitor = right.monitor_handle ();

    const std::string endpoint =
      zlink_cpp_contract::unique_tcp ("pair-multipart");
    assert (left.bind (endpoint) == 0);
    assert (right.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed), 2000,
      1));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed), 2000,
      1));

    std::vector<zlink::message_t> outbound;
    outbound.push_back (zlink_cpp_contract::make_message ("one"));
    outbound.push_back (zlink_cpp_contract::make_message ("two"));
    assert (right.send (outbound, zlink::send_flag::dontwait) == 0);

    std::vector<zlink::message_t> inbound;
    assert (left.recv (inbound) == 0);
    assert (inbound.size () == 2);
    assert (inbound[0].to_string () == "one");
    assert (inbound[1].to_string () == "two");
}

} // namespace

int main ()
{
    test_pair_send_recv_single_part ();
    test_pair_send_recv_multipart ();
    return 0;
}
