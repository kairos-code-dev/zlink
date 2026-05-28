/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <cstdlib>
#include <functional>
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

template<typename SocketT> class has_set_receive_handler_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().set_receive_handler (
                      std::function<void(zlink::received_t)> ()),
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
static_assert (!has_set_receive_handler_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose set_receive_handler");
static_assert (!has_set_receive_handler_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must not expose set_receive_handler");
static_assert (!has_set_receive_handler_t<zlink::router_socket_t>::value,
               "router_socket_t must not expose set_receive_handler");
static_assert (!has_set_receive_handler_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose raw set_receive_handler");

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

void test_pair_recv_nonblocking_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    zlink::received_t received;
    const int rc = socket.recv (received, zlink::recv_flags_t::dontwait);
    assert (rc == static_cast<int>(zlink::recv_result_t::no_data) || rc == -1);
    if (rc == -1)
        assert (errno == EAGAIN || errno == EWOULDBLOCK);
}

void test_sub_subscribe_nonblocking_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::sub_socket_t socket (ctx);
    zlink::topic_message_t received;
    const int rc = socket.subscribe (received, zlink::recv_flags_t::dontwait);
    assert (rc == static_cast<int> (zlink::recv_result_t::no_data));
}

void test_xpub_receive_subscription_event_nonblocking_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t socket (ctx);
    zlink::subscription_event_t event;
    const int rc = socket.receive_subscription_event (event, zlink::recv_flags_t::dontwait);
    assert (rc == static_cast<int> (zlink::recv_result_t::no_data));
}

void test_pair_send_without_peer_preserves_submit_surface ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t sender (ctx);

    sender.bind (zlink_cpp_contract::unique_inproc ("pair-send"));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("payload");
    (void) sender.send ().message (outbound).flags (zlink::recv_flags_t::dontwait).submit ();
}

void test_router_send_throws_for_closed_socket ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    router.close ();

    const std::string rid_text = "UNKNOWN";
    zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (rid_text.data ()), rid_text.size ());
    zlink::message_t outbound = zlink_cpp_contract::make_message ("no-route");
    expect_runtime_error ([&] {
        router.send (routing_id).message (outbound).submit ();
    });
    assert (outbound.valid ());
}

void test_send_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    socket.close ();

    zlink::message_t outbound = zlink_cpp_contract::make_message ("send-error");
    expect_runtime_error ([&] {
        socket.send ().message (outbound).submit ();
    });
    assert (outbound.valid ());
}

void test_publish_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t socket (ctx);
    socket.close ();

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("publish-error");
    expect_runtime_error ([&] {
        socket.publish ("topic:error").message (outbound).submit ();
    });
    assert (outbound.valid ());
}

void test_stream_receive_returns_busy_in_packet_callback_mode ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t socket (ctx);

    socket.set_packet_handler ([] (const zlink::routing_id_t &, zlink::message_t,
                          zlink::message_t) {});
    zlink::received_t received;
    const int rc = socket.recv (received);
    assert (rc == static_cast<int>(zlink::recv_result_t::busy) || rc == -1);
    if (rc == -1)
        assert (errno == EBUSY);
}

void test_socket_monitor_receive_returns_empty_without_event ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    zlink::monitor_handle_t monitor = socket.monitor_handle ();

    const std::optional<zlink::monitor_event_t> event =
      monitor.recv (zlink::recv_flags_t::dontwait);
    assert (!event);
    monitor.close ();
}

void test_routing_id_accepts_maximum_size ()
{
    const std::string bytes (255, 'r');

    const zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (bytes.data ()), bytes.size ());
    assert (routing_id.size () == bytes.size ());
    assert (routing_id.to_bytes ()
            == std::vector<uint8_t> (bytes.begin (), bytes.end ()));
}

void test_routing_id_rejects_oversize_input ()
{
    const std::string bytes (256, 'r');

    bool threw = false;
    try {
        (void) zlink::routing_id_t::from_bytes (
          reinterpret_cast<const uint8_t *> (bytes.data ()), bytes.size ());
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    assert (threw);
}

void test_routing_id_rejects_null_pointer_for_non_empty_bytes ()
{
    bool threw = false;
    try {
        (void) zlink::routing_id_t::from_bytes (NULL, 1);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    assert (threw);
}

void test_routing_id_copy_assignment_preserves_short_value ()
{
    const std::string long_bytes (128, 'L');
    const std::string short_bytes ("rid");

    zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (long_bytes.data ()),
      long_bytes.size ());
    const zlink::routing_id_t short_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (short_bytes.data ()),
      short_bytes.size ());

    routing_id = short_id;
    assert (routing_id.size () == short_bytes.size ());
    assert (routing_id.to_bytes ()
            == std::vector<uint8_t> (short_bytes.begin (), short_bytes.end ()));
    assert (routing_id == short_id);
}

} // namespace

int main ()
{
    test_pair_recv_nonblocking_returns_empty_without_data ();
    test_sub_subscribe_nonblocking_returns_empty_without_data ();
    test_xpub_receive_subscription_event_nonblocking_returns_empty_without_data ();
    test_pair_send_without_peer_preserves_submit_surface ();
    test_router_send_throws_for_closed_socket ();
    test_send_throws_on_general_error ();
    test_publish_throws_on_general_error ();
    test_stream_receive_returns_busy_in_packet_callback_mode ();
    test_socket_monitor_receive_returns_empty_without_event ();
    test_routing_id_accepts_maximum_size ();
    test_routing_id_rejects_oversize_input ();
    test_routing_id_rejects_null_pointer_for_non_empty_bytes ();
    test_routing_id_copy_assignment_preserves_short_value ();
    std::quick_exit (0);
}
