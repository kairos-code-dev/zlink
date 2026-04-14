/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <cstdint>
#include <type_traits>

namespace {

template<typename SpotT> class has_subscribe_result_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().subscribe (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template<typename SpotT> class has_try_subscribe_result_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().try_subscribe (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template<typename SpotT> class has_try_publish_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().try_publish (
                      std::declval<const std::string &> (),
                      std::declval<zlink::message_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template<typename SpotT> class has_filter_subscribe_alias_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().subscribe (
                      std::declval<const std::string &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template<typename SpotT> class has_filter_unsubscribe_alias_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().unsubscribe (
                      std::declval<const std::string &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template<typename T> class has_monitor_open_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().monitor_open (),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_close_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().close (),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_on_event_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().on_event (
                      static_cast<zlink::service_event_handler_fn> (NULL),
                      static_cast<void *> (NULL)),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_resolve_spot_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().resolve_spot (
                      std::declval<const zlink::routing_id_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_set_dealer_peer_mode_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().set_dealer_peer_mode (
                      zlink::service::discovery_dealer_peer_mode_t::router),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_routing_id_getter_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<const U &> ().routing_id (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_subscribe_result_t<zlink::service::spot_t>::value,
               "spot_t must expose subscribe receive");
static_assert (!has_try_subscribe_result_t<zlink::service::spot_t>::value,
               "spot_t must not expose try_subscribe");
static_assert (!has_try_publish_t<zlink::service::spot_t>::value,
               "spot_t must not expose try_publish");
static_assert (!has_filter_subscribe_alias_t<zlink::service::spot_t>::value,
               "spot_t must not expose subscribe(filter) alias");
static_assert (!has_filter_unsubscribe_alias_t<zlink::service::spot_t>::value,
               "spot_t must not expose unsubscribe(filter) alias");
static_assert (!has_monitor_open_t<zlink::service::spot_t>::value,
               "spot_t must not expose monitor_open");
static_assert (!has_monitor_open_t<zlink::service::spot_node_t>::value,
               "spot_node_t must not expose monitor_open");
static_assert (has_monitor_open_t<zlink::service::discovery_t>::value,
               "discovery_t must expose monitor_open");
static_assert (has_resolve_spot_t<zlink::service::discovery_t>::value,
               "discovery_t must expose resolve_spot");
static_assert (has_set_dealer_peer_mode_t<zlink::service::discovery_t>::value,
               "discovery_t must expose set_dealer_peer_mode");
static_assert (has_close_t<zlink::service::spot_t>::value,
               "spot_t must expose close");
static_assert (has_close_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose close");
static_assert (has_close_t<zlink::service::discovery_t>::value,
               "discovery_t must expose close");
static_assert (has_close_t<zlink::service_monitor_handle_t>::value,
               "service_monitor_handle_t must expose close");
static_assert (has_on_event_t<zlink::service_monitor_handle_t>::value,
               "service_monitor_handle_t must expose on_event");
static_assert (has_routing_id_getter_t<zlink::service::spot_t>::value,
               "spot_t must expose routing_id()");
static_assert (has_routing_id_getter_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose routing_id()");
static_assert (!std::is_constructible<zlink::service_monitor_handle_t, void *>::value,
               "service_monitor_handle_t must not expose a raw void* constructor");

void test_registry_query_and_discovery_metadata ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());

    const zlink::registry_status_t status = registry.status_snapshot ();
    assert (status.registry_id == 0);

    const std::vector<zlink::registry_topology_entry_t> topology =
      registry.topology_snapshot ();
    assert (topology.size () >= 0);

    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "orders");
    assert (discovery.valid ());

    const int64_t value = 42;
    discovery.set_value (value);

    int64_t got_value = 0;
    discovery.get_value (&got_value);
    assert (got_value == value);

    discovery.set_metadata ("meta-orders");
    zlink::message_t metadata;
    discovery.get_metadata (metadata);
    assert (metadata.to_string () == "meta-orders");

    const std::vector<zlink::member_peer_entry_t> peers =
      discovery.member_peers ();
    assert (peers.size () >= 0);

    zlink::service::discovery_t socket_discovery (
      ctx, zlink::service_type::socket, "orders-socket");
    assert (socket_discovery.valid ());
    socket_discovery.set_dealer_peer_mode (
      zlink::service::discovery_dealer_peer_mode_t::router);
    socket_discovery.set_dealer_peer_mode (
      zlink::service::discovery_dealer_peer_mode_t::dealer);
}

void test_spot_node_snapshot_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    assert (node.valid ());

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("spot-node");
    node.bind (endpoint);

    const zlink::spot_node_status_t status = node.status_snapshot ();
    assert (status.local_endpoint.empty () || status.local_endpoint == endpoint);

    const std::vector<zlink::spot_node_peer_entry_t> peers =
      node.peers_snapshot ();
    assert (peers.size () >= 0);

    const std::vector<zlink::spot_node_subject_entry_t> subjects =
      node.subjects_snapshot ();
    assert (subjects.size () >= 0);

    const zlink::routing_id_t node_rid = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> ("spot-node-rid"), 13);
    node.set_routing_id (node_rid);
    assert (node.routing_id ().to_string () == "spot-node-rid");
}

void test_unified_spot_self_delivery_recv_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    assert (spot.valid ());

    spot.set_subscription ("topic:service-self");

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("service-self");
    const zlink::routing_id_t spot_rid = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> ("spot-self-rid"), 13);
    spot.set_routing_id (spot_rid);
    assert (spot.routing_id ().to_string () == "spot-self-rid");
    spot.publish ("topic:service-self", outbound);

    const zlink::topic_message_t inbound = spot.subscribe ();
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

} // namespace

int main ()
{
    test_registry_query_and_discovery_metadata ();
    test_spot_node_snapshot_contract ();
    test_unified_spot_self_delivery_recv_contract ();
    test_unified_spot_wrap_node_surface_contract ();
    return 0;
}
