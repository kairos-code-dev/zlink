/* SPDX-License-Identifier: MPL-2.0 */

#include "test_helpers.hpp"

static void test_gateway_lifecycle_smoke()
{
    zlink::context_t ctx;
    void *discovery =
      zlink_discovery_new_typed (ctx.handle (), ZLINK_SERVICE_TYPE_GATEWAY);
    assert (discovery != NULL);

    void *gateway = zlink_gateway_new (ctx.handle (), discovery, NULL);
    assert (gateway != NULL);

    assert (zlink_gateway_destroy (&gateway) == 0);
    assert (zlink_discovery_destroy (&discovery) == 0);
}

static void test_spot_node_default_facades_created()
{
    zlink::context_t ctx;
    void *node = zlink_spot_node_new (ctx.handle ());
    assert (node != NULL);

    assert (zlink_spot_node_default_pub (node) != NULL);
    assert (zlink_spot_node_default_sub (node) != NULL);

    assert (zlink_spot_node_destroy (&node) == 0);
}

int main()
{
    test_gateway_lifecycle_smoke ();
    test_spot_node_default_facades_created ();
    return 0;
}
