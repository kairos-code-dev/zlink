/* SPDX-License-Identifier: MPL-2.0 */
/* SpotNode hub for the connection-memory study.
 * Usage: srv_spot_hub <pub_port> <router_port> [pubsub|routed|all]
 * Peers connect with zlink_spot_node_connect_peer(). The "status" command
 * reports connected_peer_count so the runner can wait for peer settlement. */

#include "mem_common.hpp"

#include <zlink.h>

int main (int argc, char **argv)
{
    if (argc < 3) {
        fprintf (stderr, "usage: %s <pub_port> <router_port> [pubsub|routed|all]\n", argv[0]);
        return 1;
    }
    study::raise_nofile_limit ();

    void *ctx = zlink_ctx_new ();
    if (!ctx)
        return 1;
    zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 200000);

    zlink_spot_node_options_t options;
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    if (argc >= 4) {
        if (!strcmp (argv[3], "pubsub"))
            options.mode = ZLINK_SPOT_NODE_MODE_PUBSUB;
        else if (!strcmp (argv[3], "routed"))
            options.mode = ZLINK_SPOT_NODE_MODE_ROUTED;
    }

    void *node = zlink_spot_node_new (ctx, &options);
    if (!node)
        return 1;
    const int linger = 0;
    zlink_set_option (node, ZLINK_OPT_LINGER, &linger, sizeof (linger));

    char router_endpoint[64];
    snprintf (router_endpoint, sizeof (router_endpoint), "tcp://127.0.0.1:%s", argv[2]);
    if (options.mode != ZLINK_SPOT_NODE_MODE_PUBSUB) {
        if (zlink_spot_node_set_router_bind (node, router_endpoint) != ZLINK_CONFIG_OK) {
            fprintf (stderr, "router bind failed: %s\n", router_endpoint);
            return 1;
        }
    }

    char pub_endpoint[64];
    snprintf (pub_endpoint, sizeof (pub_endpoint), "tcp://127.0.0.1:%s", argv[1]);
    if (options.mode != ZLINK_SPOT_NODE_MODE_ROUTED) {
        if (zlink_spot_node_set_pub_bind (node, pub_endpoint) != ZLINK_CONFIG_OK) {
            fprintf (stderr, "pub bind failed: %s\n", pub_endpoint);
            return 1;
        }
    }

    printf ("READY %s %s\n", pub_endpoint, router_endpoint);
    fflush (stdout);

    study::run_command_loop ({
      {"status",
       [&] (const std::string &) {
           zlink_spot_node_status_t status;
           memset (&status, 0, sizeof (status));
           if (zlink_spot_node_status (node, &status) == ZLINK_CONFIG_OK) {
               printf ("STATUS connected=%u active=%u configured=%u\n",
                       status.connected_peer_count, status.active_peer_count,
                       status.configured_peer_count);
           } else {
               printf ("STATUS error\n");
           }
           fflush (stdout);
       }},
    });

    _exit (0);
}
