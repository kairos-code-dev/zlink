/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace {

template<typename SocketT> class has_try_send_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().try_send (
                      std::declval<zlink::message_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_try_subscribe_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().subscribe_no_wait (),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_try_receive_subscription_event_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().try_receive_subscription_event (),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

static_assert (!has_try_send_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose try_send");
static_assert (!has_try_subscribe_t<zlink::sub_socket_t>::value,
               "sub_socket_t must not expose subscribe_no_wait");
static_assert (!has_try_receive_subscription_event_t<zlink::xpub_socket_t>::value,
               "xpub_socket_t must not expose try_receive_subscription_event");

template<typename Fn> void expect_runtime_error (Fn fn_)
{
    bool threw = false;
    try {
        fn_ ();
    }
    catch (const std::runtime_error &) {
        threw = true;
    }
    assert (threw);
}

void discard_stream_parts (const zlink_routing_id_t *,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void test_pair_recv_nonblocking_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    const std::optional<zlink::received_t> received =
      socket.recv (zlink::recv_flags_t::dontwait);
    assert (!received.has_value ());
}

void test_sub_subscribe_nonblocking_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::sub_socket_t socket (ctx);
    const std::optional<zlink::topic_message_t> received =
      socket.subscribe (zlink::recv_flags_t::dontwait);
    assert (!received.has_value ());
}

void test_xpub_receive_subscription_event_nonblocking_throws_without_data ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t socket (ctx);
    expect_runtime_error ([&] {
        (void) socket.receive_subscription_event (zlink::recv_flags_t::dontwait);
    });
}

void test_pair_send_without_peer_preserves_submit_surface ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t sender (ctx);

    sender.bind (zlink_cpp_contract::unique_inproc ("pair-send"));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("payload");
    (void) sender.send (outbound, zlink::send_flags_t::dontwait);
}

void test_router_send_throws_for_closed_socket ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    assert (router.close () == 0);

    zlink::routing_id_t routing_id;
    assert (zlink::routing_id_from ("UNKNOWN", &routing_id) == 0);
    zlink::message_t outbound = zlink_cpp_contract::make_message ("no-route");
    expect_runtime_error ([&] { router.send (routing_id, outbound); });
}

void test_send_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    assert (socket.close () == 0);

    zlink::message_t outbound = zlink_cpp_contract::make_message ("send-error");
    expect_runtime_error ([&] { socket.send (outbound); });
}

void test_publish_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t socket (ctx);
    assert (socket.close () == 0);

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("publish-error");
    expect_runtime_error ([&] { socket.publish ("topic:error", outbound); });
}

void test_stream_receive_throws_in_callback_mode ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t socket (ctx);

    socket.on_receive (&discard_stream_parts, NULL);
    expect_runtime_error ([&] { (void) socket.recv (); });
}

void test_socket_monitor_receive_returns_empty_without_event ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    zlink::monitor_handle_t monitor = socket.monitor_handle ();

    const zlink::maybe_t<zlink::monitor_event_t> event =
      monitor.recv (zlink::non_blocking_t {});
    assert (!event);
    monitor.close ();
}

void test_service_monitor_receive_returns_empty_without_event ()
{
    zlink::context_t ctx;
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "behavior-monitor");
    assert (discovery.valid ());

    zlink::service_monitor_handle_t monitor = discovery.monitor_open (
      zlink::service_monitor_event::discovery_service_up);
    assert (monitor.valid ());

    const zlink::maybe_t<zlink::service_event_t> event =
      monitor.recv (zlink::non_blocking_t {});
    assert (!event);
    monitor.close ();
}

void test_routing_id_from_accepts_maximum_size ()
{
    zlink::routing_id_t routing_id;
    const std::string bytes (255, 'r');

    assert (zlink::routing_id_from (bytes, &routing_id) == 0);
    assert (routing_id.size () == bytes.size ());
    assert (zlink::routing_id_to_string (routing_id) == bytes);
}

void test_routing_id_from_rejects_oversize_input ()
{
    zlink::routing_id_t routing_id;
    const std::string bytes (256, 'r');

    assert (zlink::routing_id_from (bytes, &routing_id) == -1);
    assert (errno == EMSGSIZE);
}

void test_routing_id_from_rejects_null_pointer_for_non_empty_bytes ()
{
    zlink::routing_id_t routing_id;

    assert (zlink::routing_id_from (NULL, 1, &routing_id) == -1);
    assert (errno == EINVAL);
}

} // namespace

int main ()
{
    test_pair_recv_nonblocking_returns_empty_without_data ();
    test_sub_subscribe_nonblocking_returns_empty_without_data ();
    test_xpub_receive_subscription_event_nonblocking_throws_without_data ();
    test_pair_send_without_peer_preserves_submit_surface ();
    test_router_send_throws_for_closed_socket ();
    test_send_throws_on_general_error ();
    test_publish_throws_on_general_error ();
    test_stream_receive_throws_in_callback_mode ();
    test_socket_monitor_receive_returns_empty_without_event ();
    test_service_monitor_receive_returns_empty_without_event ();
    test_routing_id_from_accepts_maximum_size ();
    test_routing_id_from_rejects_oversize_input ();
    test_routing_id_from_rejects_null_pointer_for_non_empty_bytes ();
    std::quick_exit (0);
}
