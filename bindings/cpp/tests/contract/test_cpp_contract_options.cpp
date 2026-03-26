/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

namespace {

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
    zlink::socket_t router (ctx, zlink::socket_type::router);

    const int linger = 0;
    assert (router.set_option (zlink::socket_options::linger, linger) == 0);

    int got_linger = -1;
    assert (router.get_option (zlink::socket_options::linger, &got_linger) == 0);
    assert (got_linger == linger);

    const int mandatory = 1;
    assert (router.set_router_option (zlink::router_option::mandatory, mandatory)
            == 0);

    int got_mandatory = 0;
    assert (router.get_router_option (zlink::router_option::mandatory,
                                      &got_mandatory)
            == 0);
    assert (got_mandatory == mandatory);

    assert (router.set_routing_id ("router-alpha") == 0);
    std::string routing_id;
    assert (router.get_routing_id (routing_id) == 0);
    assert (routing_id == "router-alpha");
}

void test_spot_options ()
{
    zlink::context_t ctx;
    zlink::service::spot_t spot (ctx);
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
