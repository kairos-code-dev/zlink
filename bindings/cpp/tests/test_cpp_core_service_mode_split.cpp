/* SPDX-License-Identifier: MPL-2.0 */

#include "test_helpers.hpp"

static void test_spot_node_default_facades_created()
{
    zlink::context_t ctx;
    void *node = zlink_spot_node_new (ctx.handle (), NULL);
    assert (node != NULL);

    assert (zlink_spot_node_default_pub (node) != NULL);
    assert (zlink_spot_node_default_sub (node) != NULL);

    assert (zlink_spot_node_destroy (&node) == 0);
}

int main()
{
    test_spot_node_default_facades_created ();
    return 0;
}
