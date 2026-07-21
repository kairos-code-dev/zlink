/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

namespace
{

template <typename T> class has_common_socket_options_facade_t
{
  private:
    template <typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().linger (),
                   std::declval<U &> ().linger (std::chrono::milliseconds (0)),
                   std::declval<U &> ().submit_retry_mode (),
                   std::declval<U &> ().submit_retry_mode (
                     zlink::submit_retry_mode_t::local_failure),
                   std::declval<U &> ().submit_retry_timeout (),
                   std::declval<U &> ().submit_retry_timeout (std::chrono::milliseconds (0)),
                   std::declval<U &> ().submit_retry_attempts (),
                   std::declval<U &> ().submit_retry_attempts (0),
                   std::declval<U &> ().send_hwm (),
                   std::declval<U &> ().send_hwm (zlink::message_count_t::value (0)),
                   std::declval<U &> ().recv_hwm (),
                   std::declval<U &> ().recv_hwm (zlink::message_count_t::value (0)),
                   std::declval<U &> ().last_endpoint (),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_router_socket_options_facade_t
{
  private:
    template <typename U>
    static auto
    test (int) -> decltype (std::declval<U &> ().mandatory (),
                            std::declval<U &> ().mandatory (true),
                            std::declval<U &> ().probe (),
                            std::declval<U &> ().probe (true),
                            std::declval<U &> ().connect_routing_id (),
                            std::declval<U &> ().connect_routing_id (
                              std::declval<const zlink::routing_id_t &> ()),
                            std::declval<U &> ().peer_weight (),
                            std::declval<U &> ().peer_weight (zlink::peer_weight_t::value (1)),
                            std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_dealer_socket_options_facade_t
{
  private:
    template <typename U>
    static auto
    test (int) -> decltype (std::declval<U &> ().probe (),
                            std::declval<U &> ().probe (true),
                            std::declval<U &> ().peer_weight (),
                            std::declval<U &> ().peer_weight (zlink::peer_weight_t::value (1)),
                            std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_stream_socket_options_facade_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().notify (),
                                        std::declval<U &> ().notify (true),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_pub_socket_options_facade_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().verbose (),
                                        std::declval<U &> ().verbose (true),
                                        std::declval<U &> ().verboser (),
                                        std::declval<U &> ().verboser (true),
                                        std::declval<U &> ().no_drop (),
                                        std::declval<U &> ().no_drop (true),
                                        std::declval<U &> ().manual (),
                                        std::declval<U &> ().manual (true),
                                        std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_sub_socket_options_facade_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().topics_count (), std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_context_options_facade_t
{
  private:
    template <typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().io_threads (),
                   std::declval<U &> ().io_threads (zlink::io_thread_count_t::value (1)),
                   std::declval<U &> ().max_sockets (),
                   std::declval<U &> ().max_sockets (zlink::socket_count_t::value (1)),
                   std::declval<U &> ().max_msg_size (),
                   std::declval<U &> ().max_msg_size (zlink::byte_size_t::bytes (1)),
                   std::declval<U &> ().thread_priority (),
                   std::declval<U &> ().thread_priority (zlink::thread_priority_t::value (1)),
                   std::declval<U &> ().thread_scheduling_policy (),
                   std::declval<U &> ().thread_scheduling_policy (
                     zlink::thread_scheduling_policy_t::other),
                   std::declval<U &> ().blocky (),
                   std::declval<U &> ().blocky (true),
                   std::declval<U &> ().auto_hwm_profile (),
                   std::declval<U &> ().auto_hwm_profile (zlink::auto_hwm_profile::balanced),
                   std::declval<U &> ().auto_hwm_msg_unit_bytes (),
                   std::declval<U &> ().auto_hwm_msg_unit_bytes (zlink::byte_size_t::bytes (64)),
                   std::declval<U &> ().socket_limit (),
                   std::declval<U &> ().msg_t_size (),
                   std::declval<U &> ().add_thread_affinity (zlink::cpu_index_t::value (0)),
                   std::declval<U &> ().remove_thread_affinity (zlink::cpu_index_t::value (0)),
                   std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template <typename T> class has_socket_options_entry_t
{
  private:
    template <typename U>
    static auto test (int) -> decltype (std::declval<U &> ().options (), std::true_type ());

    template <typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_common_socket_options_facade_t<zlink::common_socket_options_t>::value,
               "common_socket_options_t must expose canonical methods");
static_assert (has_router_socket_options_facade_t<zlink::router_socket_options_t>::value,
               "router_socket_options_t must expose canonical methods");
static_assert (has_dealer_socket_options_facade_t<zlink::dealer_socket_options_t>::value,
               "dealer_socket_options_t must expose canonical methods");
static_assert (has_stream_socket_options_facade_t<zlink::stream_socket_options_t>::value,
               "stream_socket_options_t must expose canonical methods");
static_assert (has_pub_socket_options_facade_t<zlink::pub_socket_options_t>::value,
               "pub_socket_options_t must expose canonical methods");
static_assert (has_sub_socket_options_facade_t<zlink::sub_socket_options_t>::value,
               "sub_socket_options_t must expose canonical methods");
static_assert (has_context_options_facade_t<zlink::context_options_t>::value,
               "context_options_t must exist");
static_assert (has_socket_options_entry_t<zlink::router_socket_t>::value,
               "router_socket_t must expose options()");
static_assert (!has_socket_options_entry_t<zlink::service::spot_t>::value,
               "spot_t must not expose raw socket options()");

void test_context_options ()
{
    zlink::context_t ctx;
    zlink::context_options_t options = ctx.options ();
    options.blocky (false);
    assert (!options.blocky ());
    options.auto_hwm_profile (zlink::auto_hwm_profile::compact);
    assert (options.auto_hwm_profile () == zlink::auto_hwm_profile::compact);
    options.auto_hwm_profile (zlink::auto_hwm_profile::throughput);
    assert (options.auto_hwm_profile () == zlink::auto_hwm_profile::throughput);
    options.auto_hwm_msg_unit_bytes (zlink::byte_size_t::bytes (64));
    assert (options.auto_hwm_msg_unit_bytes ().bytes () == 64);
    options.auto_hwm_msg_unit_bytes (zlink::byte_size_t::bytes (0));
    assert (options.auto_hwm_msg_unit_bytes ().bytes () == 0);

    options.io_threads (zlink::io_thread_count_t::value (2));
    assert (options.io_threads ().value () == 2);
    options.max_sockets (zlink::socket_count_t::value (128));
    assert (options.max_sockets ().value () == 128);
    try {
        options.add_thread_affinity (zlink::cpu_index_t::value (0));
        options.remove_thread_affinity (zlink::cpu_index_t::value (0));
    }
    catch (const zlink::config_error_t &err) {
        assert (err.result () == zlink::config_result_t::not_supported);
    }
    assert (options.socket_limit ().value () >= options.max_sockets ().value ());
    assert (options.msg_t_size ().bytes () > 0);
}

void test_socket_common_and_router_options ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::common_socket_options_t common = router.options ();
    common.linger (std::chrono::milliseconds (0));
    assert (common.linger () == std::chrono::milliseconds (0));
    assert (common.submit_retry_mode () == zlink::submit_retry_mode_t::off);
    assert (common.submit_retry_timeout () == std::chrono::milliseconds (0));
    assert (common.submit_retry_attempts () == 0);
    common.submit_retry_mode (zlink::submit_retry_mode_t::local_failure);
    common.submit_retry_timeout (std::chrono::milliseconds (42));
    common.submit_retry_attempts (2);
    assert (common.submit_retry_mode () == zlink::submit_retry_mode_t::local_failure);
    assert (common.submit_retry_timeout () == std::chrono::milliseconds (42));
    assert (common.submit_retry_attempts () == 2);

    zlink::stream_socket_t stream (ctx);
    zlink::stream_socket_options_t stream_options = stream.options ();
    stream_options.notify (true);
    assert (stream_options.notify ());

    const std::string rid_text = "router-alpha";
    const zlink::routing_id_t expected_routing_id = zlink::routing_id_t::from (
      reinterpret_cast<const uint8_t *> (rid_text.data ()), rid_text.size ());
    router.set_routing_id (expected_routing_id);
    zlink::routing_id_t routing_id =
      zlink::routing_id_t::from (reinterpret_cast<const uint8_t *> ("x"), 1);
    router.get_routing_id (routing_id);
    assert (routing_id.to_bytes () == std::vector<uint8_t> (rid_text.begin (), rid_text.end ()));
}

void test_mesh_node_options ()
{
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (
      ctx, zlink::service::mesh_node_options_t{.mesh_name = "contract-options"});
    node.set_router_hwm (25);
    assert (node.router_hwm () == 25);
    node.set_bind ("inproc://contract-options");
    node.set_routing_id (zlink::routing_id_t::from (std::string ("contract-options")));
    node.add_channel_name ("contract-options");
    node.start ();
    node.set_max_message_size (4096);
    assert (node.max_message_size () == 4096);
    node.set_max_message_size (-1);
    assert (node.max_message_size () == -1);
    assert (node.shutdown (std::chrono::seconds (1)) == zlink::request_result_t::ok);
    assert (node.close () == zlink::close_result_t::ok);
}

void test_mesh_node_zero_membership_lifecycle ()
{
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (
      ctx, zlink::service::mesh_node_options_t{.mesh_name = "contract-zero-membership"});
    node.set_bind (zlink_cpp_contract::unique_inproc ("contract-zero-membership"));
    node.set_routing_id (zlink::routing_id_t::from (std::string ("zero-node")));

    //  No add_channel_name() call: a caller-only Node is a complete setup.
    node.start ();
    const zlink::mesh_node_status_t ready = node.status ();
    assert (ready.state () == zlink::mesh_node_state_t::ready);
    assert (ready.channel_count () == 0);

    const std::string missing_endpoint =
      zlink_cpp_contract::unique_inproc ("contract-zero-peer");
    const uint64_t intent = node.connect_peer (missing_endpoint);
    assert (intent != 0);
    const std::vector<zlink::mesh_peer_entry_t> peers = node.peers ();
    assert (peers.size () == 1);
    assert (peers[0].connection_intent_id () == intent);
    assert (peers[0].channel_count () == 0);

    const zlink::routing_id_t missing_rid =
      zlink::routing_id_t::from (std::string ("missing-node"));
    std::vector<zlink::message_t> parts;
    parts.push_back (zlink_cpp_contract::make_message ("zero-direct"));
    assert (node.send_to_node (missing_rid, parts)
            == zlink::submit_result_t::not_connected);

    assert (node.shutdown (std::chrono::seconds (1)) == zlink::request_result_t::ok);
    assert (node.close () == zlink::close_result_t::ok);
}

} // namespace

int main ()
{
    test_context_options ();
    test_socket_common_and_router_options ();
    test_mesh_node_options ();
    test_mesh_node_zero_membership_lifecycle ();
    return 0;
}
