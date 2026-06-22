/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <type_traits>

namespace
{

template <typename SpotT> class has_subscribe_result_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().subscribe (std::declval<zlink::topic_message_t &> (),
                                                   std::declval<zlink::recv_flags_t> ()),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename SpotT> class has_subscribe_part_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().subscribe_part (std::declval<std::string &> (),
                                                        std::declval<zlink::message_t &> (),
                                                        std::declval<bool &> (),
                                                        std::declval<zlink::recv_flags_t> ()),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename SpotT> class has_try_subscribe_result_t
{
  private:
    template <typename T>
    static auto test (int) -> decltype (std::declval<T &> ().subscribe_no_wait (),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename SpotT> class has_try_publish_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().try_publish_result (std::declval<const std::string &> (),
                                                            std::declval<zlink::message_t &> ()),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename SpotT> class has_spot_publish_t
{
  private:
    template <typename T>
    static auto
    test (int) -> decltype (std::declval<T &> ().publish (std::declval<const std::string &> ()),
                            std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename SpotT> class has_topic_publish_t
{
  private:
    template <typename T>
    static auto
    test (int) -> decltype (std::declval<T &> ().publish (std::declval<const std::string &> (),
                                                          std::declval<zlink::message_t &> ()),
                            std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename SpotT> class has_receive_subscription_event_t
{
  private:
    template <typename T>
    static auto test (int) -> decltype (std::declval<T &> ().receive_subscription_event (
                                          std::declval<zlink::subscription_event_t &> (),
                                          std::declval<zlink::recv_flags_t> ()),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename NodeT> class has_attach_channel_dealer_t
{
  private:
    template <typename T>
    static auto test (int) -> decltype (std::declval<T &> ().attach_channel_dealer (
                                          std::declval<zlink::service::discovery_t &> (),
                                          std::declval<zlink::dealer_socket_t &> ()),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<NodeT> (0))::value;
};

template <typename NodeT> class has_attach_channel_dealer_manual_t
{
  private:
    template <typename T>
    static auto test (int) -> decltype (std::declval<T &> ().attach_channel_dealer_manual (
                                          std::declval<const std::string &> (),
                                          std::declval<zlink::dealer_socket_t &> ()),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<NodeT> (0))::value;
};

template <typename NodeT> class has_attach_pub_ingress_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().attach_pub_ingress (std::declval<zlink::pub_socket_t &> ()),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<NodeT> (0))::value;
};

template <typename NodeT> class has_create_route_bridge_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<zlink::service::spot_route_bridge_t &> ()
                     = std::declval<T &> ().create_route_bridge (),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<NodeT> (0))::value;
};

template <typename NodeT> class has_create_publisher_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<zlink::service::spot_node_publisher_t &> ()
                     = std::declval<T &> ().create_publisher (),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<NodeT> (0))::value;
};

template <typename BridgeT> class has_spot_route_bridge_surface_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (
        std::declval<T &> ().attach_dealer_channel (std::declval<const std::string &> (),
                                                    std::declval<zlink::dealer_socket_t &> ()),
        std::declval<T &> ().attach_router_channel (std::declval<const std::string &> (),
                                                    std::declval<zlink::router_socket_t &> ()),
        std::declval<T &> ().set_target_node (std::declval<const std::string &> (),
                                              std::declval<const zlink::routing_id_t &> ()),
        std::declval<T &> ().send (std::declval<const std::string &> (),
                                   std::declval<const zlink::routing_id_t &> (),
                                   std::declval<std::vector<zlink::message_t> &> ()),
        std::declval<T &> ().request (std::declval<const std::string &> (),
                                      std::declval<const zlink::routing_id_t &> (),
                                      std::declval<std::vector<zlink::message_t> &> (),
                                      std::declval<std::chrono::milliseconds> ()),
        std::declval<T &> ().close (), std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<BridgeT> (0))::value;
};

template <typename PublisherT> class has_spot_node_publisher_surface_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().publish (
                     std::declval<const std::string &> (),
                     std::declval<std::vector<zlink::message_t> &> ()),
                   std::declval<T &> ().close (), std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<PublisherT> (0))::value;
};

template <typename NodeT> class has_attach_router_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().attach_router (std::declval<const std::string &> (),
                                                       std::declval<zlink::router_socket_t &> ()),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<NodeT> (0))::value;
};

template <typename NodeT> class has_attach_pubsub_t
{
  private:
    template <typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().attach_pubsub (std::declval<const std::string &> (),
                                                       std::declval<zlink::pub_socket_t &> (),
                                                       std::declval<zlink::sub_socket_t &> ()),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<NodeT> (0))::value;
};

template <typename SpotT> class has_filter_subscribe_alias_t
{
  private:
    template <typename T>
    static auto
    test (int) -> decltype (std::declval<T &> ().subscribe (std::declval<const std::string &> ()),
                            std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename SpotT> class has_filter_unsubscribe_alias_t
{
  private:
    template <typename T>
    static auto
    test (int) -> decltype (std::declval<T &> ().unsubscribe (std::declval<const std::string &> ()),
                            std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template <typename T> class has_monitor_open_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().monitor_open (), std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_close_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().close (), std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_resolve_spot_t
{
  private:
    template <typename U>
    static auto test (int)
      -> decltype (std::declval<zlink::spot_route_t &> () = std::declval<U &> ().resolve_spot (
                     std::declval<const zlink::routing_id_t &> ()),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_spot_owner_sync_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().set_spot_owner_sync_enabled (true),
                                        std::declval<const U &> ().spot_owner_sync_enabled (),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_actor_route_sync_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().set_actor_route_sync_enabled (true),
                                        std::declval<const U &> ().actor_route_sync_enabled (),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_routing_id_getter_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<const U &> ().routing_id (),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_spot_node_hwm_options_t
{
  private:
    template <typename U>
    static auto test (int)
      -> decltype (std::declval<const U &> ().router_admission_hwm_profile (),
                   std::declval<U &> ().router_admission_hwm_profile (
                     zlink::auto_hwm_profile::balanced),
                   std::declval<const U &> ().router_admission_hwm (),
                   std::declval<U &> ().router_admission_hwm (zlink::message_count_t::value (1)),
                   std::declval<const U &> ().pubsub_admission_hwm_profile (),
                   std::declval<U &> ().pubsub_admission_hwm_profile (
                     zlink::auto_hwm_profile::balanced),
                   std::declval<const U &> ().pubsub_admission_hwm (),
                   std::declval<U &> ().pubsub_admission_hwm (zlink::message_count_t::value (1)),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_auto_hwm_msg_unit_setter_t
{
  private:
    template <typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().auto_hwm_msg_unit_bytes (zlink::byte_size_t::bytes (64)),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_subscribe_result_t<zlink::service::spot_t>::value,
               "spot_t must expose subscribe receive");
static_assert (has_subscribe_part_t<zlink::service::spot_t>::value,
               "spot_t must expose single part subscribe receive");
static_assert (!has_try_subscribe_result_t<zlink::service::spot_t>::value,
               "spot_t must not expose subscribe_no_wait");
static_assert (!has_try_publish_t<zlink::service::spot_t>::value,
               "spot_t must not expose try_publish_result");
static_assert (has_spot_publish_t<zlink::service::spot_t>::value,
               "spot_t must expose topic publish");
static_assert (!has_topic_publish_t<zlink::service::spot_t>::value,
               "spot_t must not expose direct message publish");
static_assert (has_receive_subscription_event_t<zlink::service::spot_t>::value,
               "spot_t must expose receive_subscription_event");
static_assert (!has_filter_subscribe_alias_t<zlink::service::spot_t>::value,
               "spot_t must not expose subscribe(filter) alias");
static_assert (!has_filter_unsubscribe_alias_t<zlink::service::spot_t>::value,
               "spot_t must not expose unsubscribe(filter) alias");
static_assert (!has_monitor_open_t<zlink::service::spot_t>::value,
               "spot_t must not expose monitor_open");
static_assert (!has_monitor_open_t<zlink::service::spot_node_t>::value,
               "spot_node_t must not expose monitor_open");
static_assert (has_attach_channel_dealer_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose attach_channel_dealer");
static_assert (has_attach_channel_dealer_manual_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose attach_channel_dealer_manual");
static_assert (has_attach_pub_ingress_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose attach_pub_ingress");
static_assert (has_create_route_bridge_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose create_route_bridge");
static_assert (has_create_publisher_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose create_publisher");
static_assert (has_spot_route_bridge_surface_t<zlink::service::spot_route_bridge_t>::value,
               "spot_route_bridge_t must expose bridge send/request surface");
static_assert (has_spot_node_publisher_surface_t<zlink::service::spot_node_publisher_t>::value,
               "spot_node_publisher_t must expose publish surface");
static_assert (!has_attach_router_t<zlink::service::spot_node_t>::value,
               "spot_node_t must not expose attach_router");
static_assert (!has_attach_pubsub_t<zlink::service::spot_node_t>::value,
               "spot_node_t must not expose attach_pubsub");
static_assert (!has_monitor_open_t<zlink::service::discovery_t>::value,
               "discovery_t must not expose monitor_open");
static_assert (has_resolve_spot_t<zlink::service::discovery_t>::value,
               "discovery_t must expose resolve_spot");
static_assert (
  std::is_same_v<decltype (std::declval<zlink::service::discovery_t &> ().resolve_route (
                   zlink::route_kind_t::actor_session, std::declval<std::span<const uint8_t>> ())),
                 zlink::discovery_route_t>,
  "discovery_t must expose resolve_route");
static_assert (has_spot_owner_sync_t<zlink::service::discovery_t>::value,
               "discovery_t must expose spot owner sync option");
static_assert (has_actor_route_sync_t<zlink::service::discovery_t>::value,
               "discovery_t must expose actor route sync option");
static_assert (has_close_t<zlink::service::spot_t>::value, "spot_t must expose close");
static_assert (has_close_t<zlink::service::spot_node_t>::value, "spot_node_t must expose close");
static_assert (has_close_t<zlink::service::discovery_t>::value, "discovery_t must expose close");
static_assert (has_routing_id_getter_t<zlink::service::spot_t>::value,
               "spot_t must expose routing_id()");
static_assert (has_routing_id_getter_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose routing_id()");
static_assert (has_spot_node_hwm_options_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose typed admission HWM options");
static_assert (!has_auto_hwm_msg_unit_setter_t<zlink::service::spot_node_t>::value,
               "spot_node_t must not expose auto-HWM message unit setter");
static_assert (!has_auto_hwm_msg_unit_setter_t<zlink::service::spot_t>::value,
               "spot_t must not expose auto-HWM message unit setter");
void test_registry_query_and_discovery_metadata ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());

    const zlink::registry_status_t status = registry.status ();
    assert (status.registry_id () == 0);

    const std::vector<zlink::registry_topology_entry_t> topology = registry.topology ();
    assert (topology.size () >= 0);

    zlink::service::discovery_t discovery (ctx, zlink::auto_connect_type::spot_mesh, "orders");
    assert (discovery.valid ());

    const int64_t value = 42;
    discovery.set_value (value);

    int64_t got_value = 0;
    discovery.get_value (&got_value);
    assert (got_value == value);

    const std::vector<zlink::member_peer_entry_t> peers = discovery.member_peers ();
    assert (peers.size () >= 0);
    auto bind_route_surface = static_cast<void (zlink::service::discovery_t::*) (
      zlink::route_kind_t, std::span<const uint8_t>, std::span<const uint8_t>)> (
      &zlink::service::discovery_t::bind_route);
    auto unbind_route_surface = static_cast<void (zlink::service::discovery_t::*) (
      zlink::route_kind_t, std::span<const uint8_t>)> (&zlink::service::discovery_t::unbind_route);
    auto resolve_route_surface = static_cast<zlink::discovery_route_t (
      zlink::service::discovery_t::*) (zlink::route_kind_t, std::span<const uint8_t>)> (
      &zlink::service::discovery_t::resolve_route);
    (void) bind_route_surface;
    (void) unbind_route_surface;
    (void) resolve_route_surface;

    zlink::service::discovery_t socket_discovery (ctx, zlink::auto_connect_type::client_server,
                                                  "orders-socket");
    assert (socket_discovery.valid ());
}

void test_spot_node_snapshot_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    assert (node.valid ());

    const std::string endpoint = zlink_cpp_contract::unique_inproc ("spot-node");
    node.set_pub_bind (endpoint);

    const zlink::spot_node_status_t status = node.status ();
    assert (status.local_endpoint ().empty () || status.local_endpoint () == endpoint);

    const std::vector<zlink::spot_node_peer_entry_t> peers = node.peers ();
    assert (peers.size () >= 0);
    if (!peers.empty ())
        assert (peers[0].kind () == zlink::spot_peer_kind::spot_mesh
                || peers[0].kind () == zlink::spot_peer_kind::router_channel);

    const std::vector<zlink::spot_node_subject_entry_t> subjects = node.subjects ();
    assert (subjects.size () >= 0);
    assert (node.routing_id ().size () == 16);

    const zlink::routing_id_t node_rid =
      zlink::routing_id_t::from (reinterpret_cast<const uint8_t *> ("spot-node-rid"), 13);
    node.set_routing_id (node_rid);
    assert (
      node.routing_id ().to_bytes ()
      == std::vector<uint8_t> ({'s', 'p', 'o', 't', '-', 'n', 'o', 'd', 'e', '-', 'r', 'i', 'd'}));

    bool rejected_empty_channel = false;
    try {
        node.connect_router_channel_peer ("", "tcp://127.0.0.1:1");
    }
    catch (const zlink::connect_error_t &err) {
        rejected_empty_channel = err.result () == zlink::connect_result_t::invalid_argument;
    }
    assert (rejected_empty_channel);
    auto connect_rid_surface = static_cast<void (zlink::service::spot_node_t::*) (
      const std::string &, const zlink::routing_id_t &, const std::string &)> (
      &zlink::service::spot_node_t::connect_router_channel_peer_rid);
    auto disconnect_rid_surface = static_cast<void (zlink::service::spot_node_t::*) (
      const std::string &, const zlink::routing_id_t &)> (
      &zlink::service::spot_node_t::disconnect_router_channel_peer_rid);
    (void) connect_rid_surface;
    (void) disconnect_rid_surface;

    node.router_admission_hwm (zlink::message_count_t::value (2));
    node.pubsub_admission_hwm (zlink::message_count_t::value (3));
    assert (node.router_admission_hwm ().value () == 2);
    assert (node.pubsub_admission_hwm ().value () == 3);
    node.router_admission_hwm_profile (zlink::auto_hwm_profile::throughput);
    node.pubsub_admission_hwm_profile (zlink::auto_hwm_profile::low_latency);
    assert (node.router_admission_hwm_profile () == zlink::auto_hwm_profile::throughput);
    assert (node.pubsub_admission_hwm_profile () == zlink::auto_hwm_profile::low_latency);
}

void test_unified_spot_self_delivery_recv_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    const std::string channel_name = "spot-self";
    zlink::service::registry_t registry (ctx);
    const std::string registry_pub = zlink_cpp_contract::unique_tcp ("spot-self-reg-pub");
    const std::string registry_router = zlink_cpp_contract::unique_tcp ("spot-self-reg-router");
    registry.bind (registry_pub, registry_router);
    zlink::service::discovery_t discovery (ctx, zlink::auto_connect_type::spot_mesh, channel_name);
    assert (discovery.valid ());
    discovery.connect_registry (registry_router);
    node.attach_discovery (discovery);
    node.set_pub_bind (zlink_cpp_contract::unique_inproc ("spot-self"));

    zlink::service::spot_t spot = node.create_spot ();
    assert (spot.valid ());
    assert (spot.routing_id ().size () == 16);

    spot.set_subscription ("topic:service-self");

    zlink::message_t outbound = zlink_cpp_contract::make_message ("service-self");
    const zlink::routing_id_t spot_rid =
      zlink::routing_id_t::from (reinterpret_cast<const uint8_t *> ("spot-self-rid"), 13);
    spot.set_routing_id (spot_rid);
    assert (
      spot.routing_id ().to_bytes ()
      == std::vector<uint8_t> ({'s', 'p', 'o', 't', '-', 's', 'e', 'l', 'f', '-', 'r', 'i', 'd'}));
    spot.publish ("topic:service-self").message (outbound).submit ();

    zlink::topic_message_t inbound;
    bool received = false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        const int rc = spot.subscribe (inbound, zlink::recv_flags_t::dontwait);
        if (rc == static_cast<int> (zlink::recv_result_t::ok)) {
            received = true;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (received);
    assert (inbound.topic () == "topic:service-self");
    assert (inbound.parts ().size () == 1);
    assert (inbound.parts ()[0].to_string () == "service-self");
}

void test_unified_spot_wrap_node_surface_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    assert (node.valid ());

    zlink::service::spot_t spot = node.create_spot ();
    assert (spot.valid ());
}

void test_spot_node_get_or_create_spot_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    const zlink::routing_id_t spot_rid =
      zlink::routing_id_t::from (reinterpret_cast<const uint8_t *> ("cpp-room"), 8);

    std::pair<zlink::service::spot_t, bool> first = node.get_or_create_spot (spot_rid);
    std::pair<zlink::service::spot_t, bool> second = node.get_or_create_spot (spot_rid);

    assert (first.first.valid ());
    assert (second.first.valid ());
    assert (first.second);
    assert (!second.second);
    assert (first.first.routing_id ().to_bytes () == spot_rid.to_bytes ());
    assert (second.first.routing_id ().to_bytes () == spot_rid.to_bytes ());
}

void test_entry_spot_join_request_reply_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx, zlink::spot_node_mode_t::all);
    zlink::service::spot_t user_spot = node.create_spot ();
    zlink::service::spot_t entry_spot = node.entry_spot ();
    zlink::service::actor_t actor = node.create_actor ("cpp-entry-join");

    std::promise<std::string> user_request;
    std::future<std::string> user_request_future = user_request.get_future ();
    std::atomic_bool user_request_done{false};
    user_spot.set_dispatch_handler (
      [&user_request, &user_request_done] (zlink::service::spot_t &spot_,
                                           const zlink::spot_dispatch_info_t &info_) mutable {
          if (info_.event != zlink::spot_dispatch_event_t::actor_join_readable)
              return;
          std::optional<zlink::actor_join_request_t> request =
            spot_.recv_actor_join (zlink::recv_flags_t::dontwait);
          if (!request.has_value ())
              return;
          if (!user_request_done.exchange (true))
              user_request.set_value (request->message ().to_string ());
          zlink::message_t reply = zlink_cpp_contract::make_message ("user-reply");
          std::move (spot_.reply_actor_join (*request, 0)).message (reply).submit ();
      });

    std::promise<std::string> entry_request;
    std::future<std::string> entry_request_future = entry_request.get_future ();
    std::atomic_bool entry_request_done{false};
    bool reject_entry_join = false;
    entry_spot.set_dispatch_handler (
      [&entry_request, &entry_request_done, &reject_entry_join] (
        zlink::service::spot_t &spot_, const zlink::spot_dispatch_info_t &info_) mutable {
          if (info_.event != zlink::spot_dispatch_event_t::actor_join_readable)
              return;
          std::optional<zlink::actor_join_request_t> request =
            spot_.recv_actor_join (zlink::recv_flags_t::dontwait);
          if (!request.has_value ())
              return;
          if (!entry_request_done.exchange (true))
              entry_request.set_value (request->message ().to_string ());
          zlink::message_t reply =
            zlink_cpp_contract::make_message (reject_entry_join ? "entry-rejected" : "entry-reply");
          std::move (spot_.reply_actor_join (*request, reject_entry_join ? 7 : 0))
            .message (reply)
            .submit ();
      });

    zlink::message_t user_join = zlink_cpp_contract::make_message ("user-join-request");
    zlink::actor_join_result_t user_result =
      std::move (actor.join (user_spot)).message (user_join).async ().get ();
    assert (user_result.result == zlink::request_result_t::ok);
    assert (zlink_cpp_contract::wait_future (user_request_future, 2000) == "user-join-request");

    zlink::message_t entry_join = zlink_cpp_contract::make_message ("entry-join-request");
    zlink::actor_join_entry_spot_result_t entry_result =
      std::move (node.join_actor_entry_spot (actor.ref (), node.routing_id (), entry_join))
        .timeout (std::chrono::milliseconds (1000))
        .async ()
        .get ();
    assert (entry_result.result == zlink::request_result_t::ok);
    assert (entry_result.join_result_code == 0);
    assert (zlink_cpp_contract::wait_future (entry_request_future, 2000) == "entry-join-request");
    assert (entry_result.reply_parts.size () == 1);
    assert (entry_result.reply_parts[0].to_string () == "entry-reply");
    assert (!entry_join.valid ());

    std::promise<std::string> rejected_entry_request;
    entry_request_future = rejected_entry_request.get_future ();
    entry_request = std::move (rejected_entry_request);
    entry_request_done.store (false);
    reject_entry_join = true;

    zlink::message_t return_to_user = zlink_cpp_contract::make_message ("return-to-user");
    zlink::actor_join_result_t return_result =
      std::move (actor.join (user_spot)).message (return_to_user).async ().get ();
    assert (return_result.result == zlink::request_result_t::ok);

    zlink::message_t rejected_join = zlink_cpp_contract::make_message ("entry-reject-request");
    zlink::actor_join_entry_spot_result_t rejected_result =
      std::move (node.join_actor_entry_spot (actor.ref (), node.routing_id (), rejected_join))
        .timeout (std::chrono::milliseconds (1000))
        .async ()
        .get ();
    assert (rejected_result.result == zlink::request_result_t::ok);
    assert (rejected_result.join_result_code == 7);
    assert (zlink_cpp_contract::wait_future (entry_request_future, 2000) == "entry-reject-request");
    assert (rejected_result.reply_parts.size () == 1);
    assert (rejected_result.reply_parts[0].to_string () == "entry-rejected");
    std::vector<zlink::actor_ref_t> user_actors = user_spot.actors ();
    assert (user_actors.size () == 1);
    assert (user_actors[0].actor_id () == actor.ref ().actor_id ());
}

} // namespace

int main ()
{
    test_registry_query_and_discovery_metadata ();
    test_spot_node_snapshot_contract ();
    test_unified_spot_self_delivery_recv_contract ();
    test_unified_spot_wrap_node_surface_contract ();
    test_spot_node_get_or_create_spot_contract ();
    test_entry_spot_join_request_reply_contract ();
    return 0;
}
