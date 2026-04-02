/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <cstdint>

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
                      static_cast<zlink_service_monitor_handler_fn> (NULL),
                      static_cast<void *> (NULL)),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_subscribe_result_t<zlink::service::spot_t>::value,
               "spot_t must expose subscribe receive");
static_assert (has_try_subscribe_result_t<zlink::service::spot_t>::value,
               "spot_t must expose try_subscribe");
static_assert (has_try_publish_t<zlink::service::spot_t>::value,
               "spot_t must expose try_publish");
static_assert (!has_filter_subscribe_alias_t<zlink::service::spot_t>::value,
               "spot_t must not expose subscribe(filter) alias");
static_assert (!has_filter_unsubscribe_alias_t<zlink::service::spot_t>::value,
               "spot_t must not expose unsubscribe(filter) alias");
static_assert (has_monitor_open_t<zlink::service::spot_t>::value,
               "spot_t must expose monitor_open");
static_assert (has_monitor_open_t<zlink::service::spot_node_t>::value,
               "spot_node_t must expose monitor_open");
static_assert (has_monitor_open_t<zlink::service::discovery_t>::value,
               "discovery_t must expose monitor_open");
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

void test_registry_query_and_discovery_metadata ()
{
    zlink::context_t ctx;
    zlink::service::registry_t registry (ctx);
    assert (registry.valid ());

    zlink_registry_status_t status;
    assert (registry.status_snapshot (status) == 0);

    size_t topology_count = 0;
    assert (registry.topology_snapshot (NULL, &topology_count) == 0);

    zlink::service::discovery_t discovery (
      ctx, zlink::service_type::spot, "orders");
    assert (discovery.valid ());

    const int64_t value = 42;
    assert (discovery.set_value (value) == 0);

    int64_t got_value = 0;
    assert (discovery.get_value (&got_value) == 0);
    assert (got_value == value);

    assert (discovery.set_metadata ("meta-orders") == 0);
    zlink::message_t metadata;
    assert (discovery.get_metadata (metadata) == 0);
    assert (metadata.to_string () == "meta-orders");

    size_t peer_count = 0;
    assert (discovery.member_peers (NULL, &peer_count) == 0);
}

void test_spot_node_snapshot_and_service_monitor ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    assert (node.valid ());

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("spot-node");
    assert (node.bind (endpoint) == 0);

    zlink_spot_node_status_t status;
    assert (node.status_snapshot (status) == 0);

    size_t peer_count = 0;
    assert (node.peers_snapshot (NULL, &peer_count) == 0);

    size_t subject_count = 0;
    assert (node.subjects_snapshot (NULL, &subject_count) == 0);

    zlink::service_monitor_handle_t monitor = node.monitor_open ();
    assert (monitor.valid ());
}

void test_unified_spot_self_delivery_recv_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    zlink::service_monitor_handle_t sub_monitor =
      spot.monitor_open (zlink::service_monitor_event::spot_filter_applied
                           | zlink::service_monitor_event::error);
    assert (sub_monitor.valid ());
    zlink::service_monitor_handle_t pub_monitor =
      spot.monitor_open (
        zlink::service_monitor_event::spot_first_delivery_ready_changed
        | zlink::service_monitor_event::error);
    assert (pub_monitor.valid ());

    assert (spot.set_subscription ("topic:service-self") == 0);
    assert (zlink_cpp_contract::wait_for_service_monitor_event (
      sub_monitor,
      static_cast<uint32_t> (
        zlink::service_monitor_event::spot_filter_applied),
      10000));
    assert (zlink_cpp_contract::wait_for_service_monitor_state (
      pub_monitor, ZLINK_MONITOR_STATE_SEND_READY, 10000));

    zlink::message_t outbound =
      zlink_cpp_contract::make_message ("service-self");
    spot.publish ("topic:service-self", outbound);

    const zlink::subscribed_t inbound = spot.subscribe ();
    assert (inbound.topic == "topic:service-self");
    assert (inbound.parts.size () == 1);
    assert (inbound.parts[0].to_string () == "service-self");

    assert (pub_monitor.close () == 0);
    assert (sub_monitor.close () == 0);
}

void test_unified_spot_wrap_node_surface_contract ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    assert (node.valid ());

    zlink::service::spot_t spot (node);
    assert (spot.valid ());
}

} // namespace

int main ()
{
    test_registry_query_and_discovery_metadata ();
    test_spot_node_snapshot_and_service_monitor ();
    test_unified_spot_self_delivery_recv_contract ();
    test_unified_spot_wrap_node_surface_contract ();
    return 0;
}
