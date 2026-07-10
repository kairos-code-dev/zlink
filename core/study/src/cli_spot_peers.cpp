/* SPDX-License-Identifier: MPL-2.0 */
/* SpotNode peer swarm for the connection-memory study.
 * Usage: cli_spot_peers <hub_pub_endpoint> <count> [pubsub|routed|all]
 * Creates <count> SpotNodes in this process and connects each one to the hub
 * with zlink_spot_node_connect_peer(). Prints DONE once every local node
 * reports at least one connected peer. */

#include "mem_common.hpp"

#include <zlink.h>

#include <vector>

int main (int argc, char **argv)
{
    if (argc < 3) {
        fprintf (stderr, "usage: %s <hub_pub_endpoint> <count> [pubsub|routed|all]\n", argv[0]);
        return 1;
    }
    study::raise_nofile_limit ();
    const char *endpoint = argv[1];
    const int count = atoi (argv[2]);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    if (argc >= 4) {
        if (!strcmp (argv[3], "pubsub"))
            options.mode = ZLINK_SPOT_NODE_MODE_PUBSUB;
        else if (!strcmp (argv[3], "routed"))
            options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    }

    void *ctx = zlink_ctx_new ();
    if (!ctx)
        return 1;
    zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 200000);

    std::vector<void *> nodes;
    nodes.reserve (count);
    const int linger = 0;
    for (int i = 0; i < count; ++i) {
        void *node = zlink_spot_node_new (ctx, &options);
        if (!node) {
            fprintf (stderr, "node %d creation failed\n", i);
            return 1;
        }
        zlink_set_option (node, ZLINK_OPT_LINGER, &linger, sizeof (linger));
        if (zlink_spot_node_connect_peer (node, endpoint) != ZLINK_CONNECT_OK) {
            fprintf (stderr, "connect_peer %d failed\n", i);
            return 1;
        }
        nodes.push_back (node);
        if ((i + 1) % 16 == 0) {
            printf ("PROGRESS connect %d\n", i + 1);
            fflush (stdout);
        }
    }

    /* Wait until every local node reports a connected hub peer. */
    int settled = 0;
    while (settled < count) {
        settled = 0;
        for (void *node : nodes) {
            zlink_spot_node_status_t status;
            memset (&status, 0, sizeof (status));
            if (zlink_spot_node_status (node, &status) == ZLINK_CONFIG_OK
                && status.connected_peer_count >= 1)
                ++settled;
        }
        printf ("PROGRESS settled %d\n", settled);
        fflush (stdout);
        if (settled < count)
            usleep (200 * 1000);
    }

    printf ("DONE %d\n", count);
    fflush (stdout);

    study::run_command_loop ({});

    _exit (0);
}
