/* SPDX-License-Identifier: MPL-2.0 */

#include "test_helpers.hpp"

#include <cstring>

static void assert_no_socket_named (void *node, const char *name)
{
    zlink_spot_node_socket_snapshot_entry_t entries[32];
    size_t count = 32;
    assert (zlink_spot_node_internal_sockets_snapshot (node, NULL, entries, &count)
            == 0);
    for (size_t i = 0; i < count; ++i)
        assert (std::strcmp (entries[i].socket_name, name) != 0);
}

static void test_spot_node_pubsub_defaults_do_not_create_default_pub()
{
    zlink::context_t ctx;
    void *node = zlink_spot_node_new (ctx.handle (), NULL);
    assert (node != NULL);

    int hwm = 32;
    assert (zlink_set_option (node, ZLINK_OPT_SNDHWM, &hwm, sizeof (hwm))
            == 0);
    assert_no_socket_named (node, "default_pub");

    assert (zlink_spot_node_destroy (&node) == 0);
}

int main()
{
    test_spot_node_pubsub_defaults_do_not_create_default_pub ();
    return 0;
}
