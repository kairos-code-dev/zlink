/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <type_traits>

namespace {

template<typename SocketT> class has_routed_send_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().send (
                      std::declval<const zlink::routing_id_t &> (),
                      std::declval<zlink::message_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_receive_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().recv (),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_raw_common_option_set_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().set_option (
                      zlink::socket_option::linger, 0),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_raw_common_option_get_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().get_option (
                      zlink::socket_option::linger,
                      static_cast<int *> (NULL)),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_attach_discovery_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().attach_discovery (
                      std::declval<zlink::service::discovery_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

static_assert (!has_routed_send_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose routed send");
static_assert (has_receive_t<zlink::pair_socket_t>::value,
               "pair_socket_t must expose recv");
static_assert (!has_attach_discovery_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose attach_discovery");
static_assert (!has_raw_common_option_set_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose raw common option setters");
static_assert (!has_raw_common_option_get_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose raw common option getters");
static_assert (!has_routed_send_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must not expose routed send");
static_assert (has_receive_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must expose recv");
static_assert (has_attach_discovery_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must expose attach_discovery");
static_assert (has_routed_send_t<zlink::router_socket_t>::value,
               "router_socket_t must expose routed send");
static_assert (has_receive_t<zlink::router_socket_t>::value,
               "router_socket_t must expose recv");
static_assert (has_attach_discovery_t<zlink::router_socket_t>::value,
               "router_socket_t must expose attach_discovery");
static_assert (has_attach_discovery_t<zlink::pub_socket_t>::value,
               "pub_socket_t must expose attach_discovery");
static_assert (has_attach_discovery_t<zlink::sub_socket_t>::value,
               "sub_socket_t must expose attach_discovery");
static_assert (!has_attach_discovery_t<zlink::xpub_socket_t>::value,
               "xpub_socket_t must not expose attach_discovery");
static_assert (!has_attach_discovery_t<zlink::xsub_socket_t>::value,
               "xsub_socket_t must not expose attach_discovery");
static_assert (has_routed_send_t<zlink::stream_socket_t>::value,
               "stream_socket_t must expose routed send");
static_assert (has_receive_t<zlink::stream_socket_t>::value,
               "stream_socket_t must expose recv");
static_assert (!has_attach_discovery_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose attach_discovery");

void test_pair_send_recv_single_part ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t left (ctx);
    zlink::pair_socket_t right (ctx);
    zlink::monitor_handle_t left_monitor = left.monitor_handle ();
    zlink::monitor_handle_t right_monitor = right.monitor_handle ();

    const std::string endpoint = zlink_cpp_contract::unique_inproc ("pair");
    assert (left.bind (endpoint) == 0);
    assert (right.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("ping");
    right.send (outbound);

    const zlink::received_t inbound = left.recv ();
    assert (inbound.parts.size () == 1);
    assert (inbound.parts[0].to_string () == "ping");
}

void test_pair_send_recv_multipart ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t left (ctx);
    zlink::pair_socket_t right (ctx);
    zlink::monitor_handle_t left_monitor = left.monitor_handle ();
    zlink::monitor_handle_t right_monitor = right.monitor_handle ();

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("pair-multipart");
    assert (left.bind (endpoint) == 0);
    assert (right.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    std::vector<zlink::message_t> outbound;
    outbound.push_back (zlink_cpp_contract::make_message ("one"));
    outbound.push_back (zlink_cpp_contract::make_message ("two"));
    right.send (outbound);

    const zlink::received_t inbound = left.recv ();
    assert (inbound.parts.size () == 2);
    assert (inbound.parts[0].to_string () == "one");
    assert (inbound.parts[1].to_string () == "two");
}

} // namespace

int main ()
{
    test_pair_send_recv_single_part ();
    test_pair_send_recv_multipart ();
    return 0;
}
