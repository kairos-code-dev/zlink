/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <stdexcept>
#include <type_traits>

namespace {

template<typename SocketT> class has_send_flags_overload_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().send (
                      std::declval<zlink::message_t &> (),
                      zlink::send_flag::dontwait),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_receive_flags_overload_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().recv(zlink::recv_flag::dontwait),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_publish_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().publish (
                      std::declval<const std::string &> (),
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
      -> decltype (std::declval<T &> ().try_subscribe (),
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

static_assert (!has_send_flags_overload_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose public send flag overloads");
static_assert (!has_receive_flags_overload_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose public receive flag overloads");
static_assert (!has_publish_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose publish");
static_assert (has_try_subscribe_t<zlink::sub_socket_t>::value,
               "sub_socket_t must expose try_subscribe");
static_assert (!has_try_subscribe_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose try_subscribe");
static_assert (
  has_try_receive_subscription_event_t<zlink::xpub_socket_t>::value,
  "xpub_socket_t must expose try_receive_subscription_event");
static_assert (
  !has_try_receive_subscription_event_t<zlink::pair_socket_t>::value,
  "pair_socket_t must not expose subscription-event receive");

template<typename Fn> void expect_runtime_error (Fn fn_)
{
    bool threw = false;
    try {
        fn_ ();
    } catch (const std::runtime_error &) {
        threw = true;
    }

    assert (threw);
}

void discard_pair_parts (const zlink_routing_id_t *,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         void *)
{
    zlink_multipart_close (parts_, part_count_);
}

void test_pair_try_receive_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);

    const zlink::maybe_t<zlink::received_t> received = socket.try_recv ();
    assert (!received);
}

void test_sub_try_subscribe_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::sub_socket_t socket (ctx);

    const zlink::maybe_t<zlink::subscribed_t> subscribed =
      socket.try_subscribe ();
    assert (!subscribed);
}

void test_xpub_try_receive_subscription_event_returns_empty_without_data ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t socket (ctx);

    const zlink::maybe_t<zlink::subscription_event_t> event =
      socket.try_receive_subscription_event ();
    assert (!event);
}

void test_pair_try_send_reports_backpressured_without_peer ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t sender (ctx);

    assert (sender.bind (zlink_cpp_contract::unique_inproc ("pair-backpressure"))
            == 0);

    zlink::message_t outbound = zlink_cpp_contract::make_message ("payload");
    const zlink::send_result_t result = sender.try_send (outbound);

    assert (result == zlink::send_result_t::backpressured);
    assert (outbound.valid ());
    assert (outbound.to_string () == "payload");
}

void test_router_try_send_reports_not_ready_for_unknown_peer ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    assert (router.set_option (zlink::router_options::mandatory, 1) == 0);

    zlink::routing_id_t routing_id;
    assert (zlink::routing_id_from ("UNKNOWN", &routing_id) == 0);

    zlink::message_t outbound = zlink_cpp_contract::make_message ("no-route");
    const zlink::send_result_t result = router.try_send (routing_id, outbound);

    assert (result == zlink::send_result_t::not_ready);
    assert (outbound.valid ());
    assert (outbound.to_string () == "no-route");
}

void test_blocking_send_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    assert (socket.close () == 0);

    zlink::message_t outbound = zlink_cpp_contract::make_message ("send-error");
    expect_runtime_error ([&] { socket.send (outbound); });
    assert (outbound.valid ());
    assert (outbound.to_string () == "send-error");
}

void test_try_send_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);
    assert (socket.close () == 0);

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("try-send-error");
    expect_runtime_error ([&] { (void) socket.try_send (outbound); });
    assert (outbound.valid ());
    assert (outbound.to_string () == "try-send-error");
}

void test_blocking_publish_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t socket (ctx);
    assert (socket.close () == 0);

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("publish-error");
    expect_runtime_error ([&] { socket.publish ("topic:error", outbound); });
    assert (outbound.valid ());
    assert (outbound.to_string () == "publish-error");
}

void test_try_publish_throws_on_general_error ()
{
    zlink::context_t ctx;
    zlink::pub_socket_t socket (ctx);
    assert (socket.close () == 0);

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("try-publish-error");
    expect_runtime_error ([&] { (void) socket.try_publish ("topic:error", outbound); });
    assert (outbound.valid ());
    assert (outbound.to_string () == "try-publish-error");
}

void test_pair_receive_throws_in_callback_mode ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);

    assert (socket.on_receive (&discard_pair_parts, NULL) == 0);
    expect_runtime_error ([&] { (void) socket.recv (); });
    assert (zlink_errno () == EBUSY);
}

void test_pair_try_receive_throws_in_callback_mode ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);

    assert (socket.on_receive (&discard_pair_parts, NULL) == 0);
    expect_runtime_error ([&] { (void) socket.try_recv (); });
    assert (zlink_errno () == EBUSY);
}

void test_socket_monitor_try_receive_returns_empty_without_event ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t socket (ctx);

    zlink::monitor_handle_t monitor = socket.monitor_handle ();
    assert (monitor.valid ());
    const zlink::maybe_t<zlink_socket_monitor_event_t> event =
      monitor.try_recv ();
    assert (!event);
    assert (monitor.close () == 0);
}

void test_service_monitor_try_receive_returns_empty_without_event ()
{
    zlink::context_t ctx;
    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "behavior-monitor");
    assert (discovery.valid ());

    zlink::service_monitor_handle_t monitor (
      discovery, zlink::service_monitor_event::discovery_service_up);
    assert (monitor.valid ());
    const zlink::maybe_t<zlink_service_monitor_event_t> event =
      monitor.try_recv ();
    assert (!event);
    assert (monitor.close () == 0);
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
    test_pair_try_receive_returns_empty_without_data ();
    test_sub_try_subscribe_returns_empty_without_data ();
    test_xpub_try_receive_subscription_event_returns_empty_without_data ();
    test_pair_try_send_reports_backpressured_without_peer ();
    test_router_try_send_reports_not_ready_for_unknown_peer ();
    test_blocking_send_throws_on_general_error ();
    test_try_send_throws_on_general_error ();
    test_blocking_publish_throws_on_general_error ();
    test_try_publish_throws_on_general_error ();
    test_pair_receive_throws_in_callback_mode ();
    test_pair_try_receive_throws_in_callback_mode ();
    test_socket_monitor_try_receive_returns_empty_without_event ();
    test_service_monitor_try_receive_returns_empty_without_event ();
    test_routing_id_from_accepts_maximum_size ();
    test_routing_id_from_rejects_oversize_input ();
    test_routing_id_from_rejects_null_pointer_for_non_empty_bytes ();
    return 0;
}
