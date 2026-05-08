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

template<typename SocketT> class has_on_receive_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().on_receive (
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
static_assert (!has_on_receive_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose on_receive");
static_assert (!has_on_receive_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must not expose on_receive");
static_assert (!has_on_receive_t<zlink::router_socket_t>::value,
               "router_socket_t must not expose on_receive");
static_assert (!has_on_receive_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose raw on_receive");

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
    const std::optional<zlink::received_t> received =
      socket.recv (ZLINK_DONTWAIT);
    assert (!received.has_value ());
}

void test_sub_subscribe_nonblocking_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::sub_socket_t socket (ctx);
    const std::optional<zlink::topic_message_t> received =
      socket.subscribe (ZLINK_DONTWAIT);
    assert (!received.has_value ());
}

void test_xpub_receive_subscription_event_nonblocking_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t socket (ctx);
    const std::optional<zlink::subscription_event_t> event =
      socket.receive_subscription_event (ZLINK_DONTWAIT);
    assert (!event.has_value ());
}

void test_pair_send_without_peer_preserves_submit_surface ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t sender (ctx);

    sender.bind (zlink_cpp_contract::unique_inproc ("pair-send"));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("payload");
    (void) sender.send (outbound, ZLINK_DONTWAIT);
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
    expect_runtime_error ([&] { router.send (routing_id, outbound); });
}

void test_send_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    socket.close ();

    zlink::message_t outbound = zlink_cpp_contract::make_message ("send-error");
    expect_runtime_error ([&] { socket.send (outbound); });
}

void test_publish_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t socket (ctx);
    socket.close ();

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("publish-error");
    expect_runtime_error ([&] { socket.publish ("topic:error", outbound); });
}

void test_stream_receive_throws_in_packet_callback_mode ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t socket (ctx);

    socket.on_packet ([] (const zlink::routing_id_t &, zlink::message_t,
                          zlink::message_t) {});
    expect_runtime_error ([&] { (void) socket.recv (); });
}

void test_socket_monitor_receive_returns_empty_without_event ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    zlink::monitor_handle_t monitor = socket.monitor_handle ();

    const std::optional<zlink::monitor_event_t> event =
      monitor.recv (ZLINK_DONTWAIT);
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
    test_stream_receive_throws_in_packet_callback_mode ();
    test_socket_monitor_receive_returns_empty_without_event ();
    test_routing_id_accepts_maximum_size ();
    test_routing_id_rejects_oversize_input ();
    test_routing_id_rejects_null_pointer_for_non_empty_bytes ();
    std::quick_exit (0);
}
