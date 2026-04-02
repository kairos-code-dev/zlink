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
    assert (ctx.set (zlink::context_option::blocky, 0) == 0);

    int blocky = -1;
    assert (ctx.get (zlink::context_option::blocky, blocky) == 0);
    assert (blocky == 0);
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
    assert (stream.set_option (zlink::stream_options::notify, notify) == 0);

    int got_notify = 0;
    assert (stream.get_option (zlink::stream_options::notify, &got_notify) == 0);
    assert (got_notify == notify);

    const zlink::routing_id_t expected_routing_id ("router-alpha");
    assert (router.set_routing_id (expected_routing_id) == 0);
    zlink::routing_id_t routing_id;
    assert (router.get_routing_id (routing_id) == 0);
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
