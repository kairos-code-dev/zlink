/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

namespace {

template<typename T> class has_common_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (U::linger, U::sndhwm, U::rcvhwm, std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_router_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (U::mandatory, U::handover, U::probe,
                    U::connect_routing_id, std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_dealer_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (U::probe, std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_stream_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (U::notify, std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_pub_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (U::verbose, U::verboser, U::nodrop, U::manual,
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<T> (0))::value;
};

template<typename T> class has_sub_socket_options_facade_t
{
  private:
    template<typename U>
    static auto test (int)
      -> decltype (U::topics_count, std::true_type ());

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

template<typename SpotT> class has_typed_pub_option_set_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().set (zlink::pub_options::verbose, 1),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

template<typename SpotT> class has_typed_sub_option_get_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<const T &> ().get (
                      zlink::sub_options::topics_count,
                      std::declval<int &> ()),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SpotT> (0))::value;
};

static_assert (has_typed_pub_option_set_t<zlink::service::spot_t>::value,
               "spot_t must expose typed pub option setters");
static_assert (has_typed_sub_option_get_t<zlink::service::spot_t>::value,
               "spot_t must expose typed sub option getters");
static_assert (has_common_socket_options_facade_t<
                 zlink::common_socket_options_t>::value,
               "common_socket_options_t must exist");
static_assert (has_router_socket_options_facade_t<
                 zlink::router_socket_options_t>::value,
               "router_socket_options_t must exist");
static_assert (has_dealer_socket_options_facade_t<
                 zlink::dealer_socket_options_t>::value,
               "dealer_socket_options_t must exist");
static_assert (has_stream_socket_options_facade_t<
                 zlink::stream_socket_options_t>::value,
               "stream_socket_options_t must exist");
static_assert (has_pub_socket_options_facade_t<
                 zlink::pub_socket_options_t>::value,
               "pub_socket_options_t must exist");
static_assert (has_sub_socket_options_facade_t<
                 zlink::sub_socket_options_t>::value,
               "sub_socket_options_t must exist");
static_assert (has_context_options_facade_t<
                 zlink::context_options_t>::value,
               "context_options_t must exist");

template<typename SocketT> class has_typed_router_option_set_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<T &> ().set_option (
                      zlink::router_options::mandatory, 1),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

template<typename SocketT> class has_typed_router_option_get_t
{
  private:
    template<typename T>
    static auto test (int)
      -> decltype (std::declval<const T &> ().get_option (
                      zlink::router_options::mandatory,
                      static_cast<int *> (NULL)),
                    std::true_type ());

    template<typename> static std::false_type test (...);

  public:
    static const bool value = decltype (test<SocketT> (0))::value;
};

static_assert (has_typed_router_option_set_t<zlink::router_socket_t>::value,
               "router_socket_t must expose typed router option setters");
static_assert (has_typed_router_option_get_t<zlink::router_socket_t>::value,
               "router_socket_t must expose typed router option getters");

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

    const int linger = 0;
    assert (router.set_option (zlink::socket_options::linger, linger) == 0);

    int got_linger = -1;
    assert (router.get_option (zlink::socket_options::linger, &got_linger) == 0);
    assert (got_linger == linger);

    zlink::stream_socket_t stream (ctx);
    const int notify = 1;
    stream.set_option (zlink::stream_options::notify, notify);

    int got_notify = 0;
    stream.get_option (zlink::stream_options::notify, &got_notify);
    assert (got_notify == notify);

    const zlink::routing_id_t expected_routing_id ("router-alpha");
    router.set_routing_id (expected_routing_id);
    zlink::routing_id_t routing_id;
    router.get_routing_id (routing_id);
    assert (routing_id.to_string () == "router-alpha");
}

void test_spot_options ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot (node);
    assert (spot.valid ());

    const int linger = 0;
    assert (spot.set (zlink::socket_options::linger, linger) == 0);

    int got_linger = -1;
    assert (spot.get (zlink::socket_options::linger, got_linger) == 0);
    assert (got_linger == linger);
}

} // namespace

int main ()
{
    test_context_options ();
    test_socket_common_and_router_options ();
    test_spot_options ();
    return 0;
}
