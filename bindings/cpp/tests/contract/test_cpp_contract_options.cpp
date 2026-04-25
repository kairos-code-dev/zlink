/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

namespace {

template<typename T> class has_common_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().linger (),
                    std::declval<U &> ().linger (0),
                    std::declval<U &> ().send_hwm (),
                    std::declval<U &> ().send_hwm (0),
                    std::declval<U &> ().recv_hwm (),
                    std::declval<U &> ().recv_hwm (0),
                    std::declval<U &> ().last_endpoint (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_router_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().mandatory (),
                    std::declval<U &> ().mandatory (true),
                    std::declval<U &> ().probe_router (),
                    std::declval<U &> ().probe_router (true),
                    std::declval<U &> ().connect_routing_id (),
                    std::declval<U &> ().connect_routing_id (
                      std::declval<const zlink::routing_id_t &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_dealer_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().probe_router (),
                    std::declval<U &> ().probe_router (true),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_stream_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().notify (),
                    std::declval<U &> ().notify (true), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_pub_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().verbose (),
                    std::declval<U &> ().verbose (true),
                    std::declval<U &> ().verboser (),
                    std::declval<U &> ().verboser (true),
                    std::declval<U &> ().no_drop (),
                    std::declval<U &> ().no_drop (true),
                    std::declval<U &> ().manual (),
                    std::declval<U &> ().manual (true), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_sub_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().topics_count (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_context_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().ioThreads (),
                    std::declval<U &> ().ioThreads (1),
                    std::declval<U &> ().maxSockets (),
                    std::declval<U &> ().maxSockets (1),
                    std::declval<U &> ().maxMsgSize (),
                    std::declval<U &> ().maxMsgSize (1),
                    std::declval<U &> ().threadPriority (),
                    std::declval<U &> ().threadPriority (1),
                    std::declval<U &> ().threadSchedulingPolicy (),
                    std::declval<U &> ().threadSchedulingPolicy (1),
                    std::declval<U &> ().blocky (),
                    std::declval<U &> ().blocky (true),
                    std::declval<U &> ().socketLimit (),
                    std::declval<U &> ().msgTSize (),
                    std::declval<U &> ().addThreadAffinity (0),
                    std::declval<U &> ().removeThreadAffinity (0),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_socket_options_entry_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (std::declval<U &> ().options (), std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

static_assert (has_common_socket_options_facade_t<
                 zlink::common_socket_options_t>::value,
               "common_socket_options_t must expose canonical methods");
static_assert (has_router_socket_options_facade_t<
                 zlink::router_socket_options_t>::value,
               "router_socket_options_t must expose canonical methods");
static_assert (has_dealer_socket_options_facade_t<
                 zlink::dealer_socket_options_t>::value,
               "dealer_socket_options_t must expose canonical methods");
static_assert (has_stream_socket_options_facade_t<
                 zlink::stream_socket_options_t>::value,
               "stream_socket_options_t must expose canonical methods");
static_assert (has_pub_socket_options_facade_t<
                 zlink::pub_socket_options_t>::value,
               "pub_socket_options_t must expose canonical methods");
static_assert (has_sub_socket_options_facade_t<
                 zlink::sub_socket_options_t>::value,
               "sub_socket_options_t must expose canonical methods");
static_assert (has_context_options_facade_t<
                 zlink::context_options_t>::value,
               "context_options_t must exist");
static_assert (has_socket_options_entry_t<zlink::router_socket_t>::value,
               "router_socket_t must expose options()");
static_assert (has_socket_options_entry_t<zlink::service::spot_t>::value,
               "spot_t must expose options()");

void test_context_options ()
{
    zlink::context_t ctx;
    zlink::context_options_t options = ctx.options ();
    options.blocky (false);
    assert (!options.blocky ());

    options.ioThreads (2);
    assert (options.ioThreads () == 2);
    options.maxSockets (128);
    assert (options.maxSockets () == 128);
    options.addThreadAffinity (0);
    options.removeThreadAffinity (0);
    assert (options.socketLimit () >= options.maxSockets ());
    assert (options.msgTSize () > 0);
}

void test_socket_common_and_router_options ()
{
    zlink::context_t ctx;
    zlink::router_socket_t router (ctx);
    zlink::common_socket_options_t common = router.options ();
    common.linger (0);
    assert (common.linger () == 0);

    zlink::stream_socket_t stream (ctx);
    zlink::stream_socket_options_t stream_options = stream.stream_options ();
    stream_options.notify (true);
    assert (stream_options.notify ());

    const std::string rid_text = "router-alpha";
    const zlink::routing_id_t expected_routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (rid_text.data ()), rid_text.size ());
    router.set_routing_id (expected_routing_id);
    zlink::routing_id_t routing_id;
    router.get_routing_id (routing_id);
    assert (routing_id.to_string () == "router-alpha");
  }

void test_spot_options ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    assert (spot.valid ());

    zlink::common_socket_options_t common = spot.options ();
    common.linger (0);
    assert (common.linger () == 0);
    (void) spot.publisher_options ();
    (void) spot.subscriber_options ();
}

} // namespace

int main ()
{
    test_context_options ();
    test_socket_common_and_router_options ();
    test_spot_options ();
    return 0;
}
