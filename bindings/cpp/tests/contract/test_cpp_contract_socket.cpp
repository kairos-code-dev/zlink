/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <climits>
#include <cstring>
#include <optional>
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

template<typename SocketT> class has_send_builder_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().send (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_routed_send_builder_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().send (
                      std::declval<const zlink::routing_id_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_publish_builder_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().publish (
                      std::declval<const std::string &> ()),
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
      -> decltype (std::declval<T &> ().recv (
                      std::declval<zlink::received_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_single_part_recv_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().recv (
                      std::declval<zlink::message_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_routed_single_part_recv_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().recv (
                      std::declval<zlink::routing_id_t &> (),
                      std::declval<zlink::message_t &> ()),
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
                      zlink::compat::options::socket_option::linger, 0),
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
                      zlink::compat::options::socket_option::linger,
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

template<typename SocketT> class has_connect_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().connect (
                      std::declval<const std::string &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_disconnect_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().disconnect (
                      std::declval<const std::string &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_disconnect_rid_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().disconnect_rid (
                      std::declval<const zlink::routing_id_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_recv_spot_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().recv_spot (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

static_assert (!has_routed_send_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose routed send");
static_assert (has_send_builder_t<zlink::pair_socket_t>::value,
               "pair_socket_t must expose send builder");
static_assert (has_receive_t<zlink::pair_socket_t>::value,
               "pair_socket_t must expose recv");
static_assert (has_single_part_recv_t<zlink::pair_socket_t>::value,
               "pair_socket_t must expose single-part recv");
static_assert (!has_attach_discovery_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose attach_discovery");
static_assert (!has_raw_common_option_set_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose raw common option setters");
static_assert (!has_raw_common_option_get_t<zlink::pair_socket_t>::value,
               "pair_socket_t must not expose raw common option getters");
static_assert (!has_routed_send_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must not expose routed send");
static_assert (has_send_builder_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must expose send builder");
static_assert (has_receive_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must expose recv");
static_assert (has_single_part_recv_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must expose single-part recv");
static_assert (has_attach_discovery_t<zlink::dealer_socket_t>::value,
               "dealer_socket_t must expose attach_discovery");
static_assert (!has_routed_send_t<zlink::router_socket_t>::value,
               "router_socket_t must not expose direct routed send");
static_assert (has_routed_send_builder_t<zlink::router_socket_t>::value,
               "router_socket_t must expose routed send builder");
static_assert (has_receive_t<zlink::router_socket_t>::value,
               "router_socket_t must expose recv");
static_assert (has_routed_single_part_recv_t<zlink::router_socket_t>::value,
               "router_socket_t must expose routed single-part recv");
static_assert (!has_recv_spot_t<zlink::router_socket_t>::value,
               "router_socket_t must not expose recv_spot");
static_assert (has_attach_discovery_t<zlink::router_socket_t>::value,
               "router_socket_t must expose attach_discovery");
static_assert (has_attach_discovery_t<zlink::pub_socket_t>::value,
               "pub_socket_t must expose attach_discovery");
static_assert (has_publish_builder_t<zlink::pub_socket_t>::value,
               "pub_socket_t must expose publish builder");
static_assert (has_attach_discovery_t<zlink::sub_socket_t>::value,
               "sub_socket_t must expose attach_discovery");
static_assert (!has_attach_discovery_t<zlink::xpub_socket_t>::value,
               "xpub_socket_t must not expose attach_discovery");
static_assert (has_publish_builder_t<zlink::xpub_socket_t>::value,
               "xpub_socket_t must expose publish builder");
static_assert (!has_attach_discovery_t<zlink::xsub_socket_t>::value,
               "xsub_socket_t must not expose attach_discovery");
static_assert (!has_routed_send_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose direct routed send");
static_assert (has_routed_send_builder_t<zlink::stream_socket_t>::value,
               "stream_socket_t must expose routed send builder");
static_assert (has_receive_t<zlink::stream_socket_t>::value,
               "stream_socket_t must expose recv");
static_assert (!has_attach_discovery_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose attach_discovery");
static_assert (!has_connect_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose connect");
static_assert (!has_disconnect_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose disconnect");
static_assert (!has_disconnect_rid_t<zlink::stream_socket_t>::value,
               "stream_socket_t must not expose disconnect_rid");

void test_pair_send_recv_single_part ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t left (ctx);
    zlink::pair_socket_t right (ctx);
    zlink::monitor_handle_t left_monitor = left.monitor_handle ();
    zlink::monitor_handle_t right_monitor = right.monitor_handle ();

    const std::string endpoint = zlink_cpp_contract::unique_inproc ("pair");
    left.bind (endpoint);
    right.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("ping");
    assert (right.send ().message (outbound).submit ());

    zlink::received_t inbound;
    assert (left.recv (inbound) == 0);
    assert (inbound.parts ().size () == 1);
    assert (inbound.parts ()[0].to_string () == "ping");
}

void test_pair_send_recv_single_part_direct ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t left (ctx);
    zlink::pair_socket_t right (ctx);
    zlink::monitor_handle_t left_monitor = left.monitor_handle ();
    zlink::monitor_handle_t right_monitor = right.monitor_handle ();

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("pair-direct");
    left.bind (endpoint);
    right.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("direct");
    assert (right.send ().message (outbound).submit ());

    zlink::message_t inbound;
    assert (left.recv (inbound) == 0);
    assert (inbound.to_string () == "direct");
}

void test_router_recv_single_part_direct ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::dealer_socket_t dealer (ctx);
    zlink::monitor_handle_t router_monitor = router.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer.monitor_handle ();

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("router-direct");
    const zlink::routing_id_t dealer_id =
      zlink::routing_id_t::from_bytes (
        reinterpret_cast<const uint8_t *> ("dealer-a"), 8);
    dealer.set_routing_id (dealer_id);

    router.bind (endpoint);
    dealer.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    zlink::message_t outbound = zlink_cpp_contract::make_message ("routed");
    assert (dealer.send ().message (outbound).submit ());

    zlink::routing_id_t source =
      zlink::routing_id_t::from_bytes (
        reinterpret_cast<const uint8_t *> ("placeholder"), 11);
    zlink::message_t inbound;
    assert (router.recv (source, inbound) == 0);
    assert (source == dealer_id);
    assert (inbound.to_string () == "routed");
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
    left.bind (endpoint);
    right.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    std::vector<zlink::message_t> outbound;
    outbound.push_back (zlink_cpp_contract::make_message ("one"));
    outbound.push_back (zlink_cpp_contract::make_message ("two"));
    assert (right.send ()
              .message (outbound[0])
              .message (outbound[1])
              .submit ());

    zlink::received_t inbound;
    assert (left.recv (inbound) == 0);
    assert (inbound.parts ().size () == 2);
    assert (inbound.parts ()[0].to_string () == "one");
    assert (inbound.parts ()[1].to_string () == "two");
}

void test_pair_ipc_large_message_shutdown ()
{
    zlink::context_t ctx;
    zlink::pair_socket_t left (ctx);
    zlink::pair_socket_t right (ctx);
    zlink::monitor_handle_t left_monitor = left.monitor_handle ();
    zlink::monitor_handle_t right_monitor = right.monitor_handle ();

    const std::string endpoint =
      zlink_cpp_contract::unique_ipc ("pair-large-shutdown");
    left.bind (endpoint);
    right.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      left_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      right_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    const size_t payload_size = 262144;
    zlink::message_t outbound (payload_size);
    assert (outbound.valid ());
    std::memset (outbound.data (), 0x5a, payload_size);
    assert (right.send ().message (outbound).submit ());

    zlink::received_t inbound;
    assert (left.recv (inbound) == 0);
    assert (inbound.parts ().size () == 1);
    assert (inbound.parts ()[0].size () == payload_size);
}

void test_common_auto_hwm_msg_unit_option_contract ()
{
    zlink::context_t ctx;
    zlink::stream_socket_t socket (ctx);
    zlink::stream_socket_options_t options = socket.options ();

    options.auto_hwm_msg_unit_bytes (zlink::byte_size_t::bytes (64));
    assert (options.auto_hwm_msg_unit_bytes ().bytes () == 64);

    bool rejected = false;
    try {
        options.auto_hwm_msg_unit_bytes (
          zlink::byte_size_t::bytes (
            static_cast<int64_t> (INT_MAX) + 1));
    }
    catch (const zlink::config_error_t &err) {
        rejected = err.internal_errno () == EINVAL;
    }
    assert (rejected);
}

} // namespace

int main ()
{
    test_common_auto_hwm_msg_unit_option_contract ();
    test_pair_send_recv_single_part ();
    test_pair_send_recv_single_part_direct ();
    test_router_recv_single_part_direct ();
    test_pair_send_recv_multipart ();
    test_pair_ipc_large_message_shutdown ();
    return 0;
}
