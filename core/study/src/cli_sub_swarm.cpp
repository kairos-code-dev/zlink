/* SPDX-License-Identifier: MPL-2.0 */
/* SUB swarm client for the connection-memory study.
 * Usage: cli_sub_swarm <endpoint> <count>
 * Creates <count> SUB sockets, connects each to the PUB server and subscribes
 * to topic "t". The server publishes ticks; once every socket has received at
 * least one message the client prints DONE (all connections + subscriptions
 * are then live on the server side). */

#include "mem_common.hpp"

#include <zlink.h>

#include <atomic>
#include <thread>
#include <vector>

int main (int argc, char **argv)
{
    if (argc < 3) {
        fprintf (stderr, "usage: %s <endpoint> <count>\n", argv[0]);
        return 1;
    }
    study::raise_nofile_limit ();
    const char *endpoint = argv[1];
    const int count = atoi (argv[2]);

    void *ctx = zlink_ctx_new ();
    if (!ctx)
        return 1;
    zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, count * 4 > 100000 ? count * 4 : 100000);

    std::vector<void *> subs;
    std::vector<char> seen;
    subs.reserve (count);
    seen.assign (count, 0);
    const int linger = 0;
    for (int i = 0; i < count; ++i) {
        void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
        if (!sub) {
            fprintf (stderr, "socket %d creation failed\n", i);
            return 1;
        }
        zlink_set_option (sub, ZLINK_OPT_LINGER, &linger, sizeof (linger));
        if (zlink_connect (sub, endpoint) != ZLINK_CONNECT_OK) {
            fprintf (stderr, "connect %d failed\n", i);
            return 1;
        }
        if (zlink_set_subscription (sub, "t") != ZLINK_CONFIG_OK) {
            fprintf (stderr, "subscribe %d failed\n", i);
            return 1;
        }
        subs.push_back (sub);
        if ((i + 1) % 500 == 0) {
            printf ("PROGRESS connect %d\n", i + 1);
            fflush (stdout);
        }
    }

    /* Drain until every subscriber has seen at least one tick. */
    int live = 0;
    while (live < count) {
        int drained = 0;
        for (int i = 0; i < count; ++i) {
            char topic[32];
            size_t topic_len = 0;
            zlink_msg_t part;
            zlink_msg_init (&part);
            zlink_part_flag_t more = ZLINK_PART_FINAL;
            const zlink_recv_result_t rc =
              zlink_subscribe_part (subs[i], NULL, topic, sizeof (topic), &topic_len, &part, &more,
                                    ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc == ZLINK_RECV_OK) {
                ++drained;
                if (!seen[i]) {
                    seen[i] = 1;
                    ++live;
                }
            }
            zlink_msg_close (&part);
        }
        if (!drained)
            usleep (50 * 1000);
        else {
            printf ("PROGRESS live %d\n", live);
            fflush (stdout);
        }
    }

    printf ("DONE %d\n", count);
    fflush (stdout);

    /* Keep draining so server-side pipes empty out after a publish burst;
     * otherwise queued messages would look like retained server memory. */
    std::atomic<bool> stop (false);
    std::atomic<uint64_t> drained (0);
    std::thread drain ([&] () {
        while (!stop.load (std::memory_order_relaxed)) {
            int got = 0;
            for (int i = 0; i < count; ++i) {
                char topic[32];
                size_t topic_len = 0;
                zlink_msg_t part;
                zlink_msg_init (&part);
                zlink_part_flag_t more = ZLINK_PART_FINAL;
                if (zlink_subscribe_part (subs[i], NULL, topic, sizeof (topic), &topic_len, &part,
                                          &more, ZLINK_RECV_FLAGS_DONTWAIT)
                    == ZLINK_RECV_OK)
                    ++got;
                zlink_msg_close (&part);
            }
            drained.fetch_add (got, std::memory_order_relaxed);
            if (!got)
                usleep (20 * 1000);
        }
    });

    study::run_command_loop ({
      {"drained",
       [&] (const std::string &) {
           printf ("DRAINED %llu\n", static_cast<unsigned long long> (drained.load ()));
           fflush (stdout);
       }},
    });

    stop.store (true);
    drain.join ();
    _exit (0);
}
