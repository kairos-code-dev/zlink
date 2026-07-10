/* SPDX-License-Identifier: MPL-2.0 */
/* ROUTER server for the connection-memory study.
 * Usage: srv_router <port>
 * Prints READY after bind, then serves stdin commands (rss/trim/count/quit).
 * A background pump drains inbound parts and counts final parts as messages. */

#include "mem_common.hpp"

#include <zlink.h>

#include <atomic>
#include <thread>

int main (int argc, char **argv)
{
    if (argc < 2) {
        fprintf (stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }
    study::raise_nofile_limit ();

    void *ctx = zlink_ctx_new ();
    if (!ctx)
        return 1;
    zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, 200000);

    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    if (!router)
        return 1;
    const int linger = 0;
    zlink_set_option (router, ZLINK_OPT_LINGER, &linger, sizeof (linger));

    char endpoint[64];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%s", argv[1]);
    if (zlink_bind (router, endpoint) != ZLINK_BIND_OK) {
        fprintf (stderr, "bind failed: %s\n", endpoint);
        return 1;
    }

    std::atomic<bool> stop (false);
    std::atomic<uint64_t> messages (0);

    std::thread pump ([&] () {
        while (!stop.load (std::memory_order_relaxed)) {
            const zlink_routing_id_t *src_node = NULL;
            const zlink_routing_id_t *src_spot = NULL;
            uint64_t seq = 0;
            zlink_msg_t part;
            zlink_msg_init (&part);
            zlink_part_flag_t more = ZLINK_PART_FINAL;
            const zlink_recv_result_t rc = zlink_router_recv_part (
              router, &src_node, &src_spot, &seq, &part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc == ZLINK_RECV_OK) {
                if (more == ZLINK_PART_FINAL)
                    messages.fetch_add (1, std::memory_order_relaxed);
                zlink_msg_close (&part);
            } else {
                zlink_msg_close (&part);
                if (rc == ZLINK_RECV_TERMINATED)
                    break;
                usleep (500);
            }
        }
    });

    printf ("READY %s\n", endpoint);
    fflush (stdout);

    study::run_command_loop ({
      {"count",
       [&] (const std::string &) {
           printf ("COUNT %llu\n", static_cast<unsigned long long> (messages.load ()));
           fflush (stdout);
       }},
    });

    stop.store (true);
    pump.join ();
    /* Skip graceful ctx teardown: measurement is done and closing tens of
     * thousands of connections cleanly is not part of this study. */
    _exit (0);
}
