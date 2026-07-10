/* SPDX-License-Identifier: MPL-2.0 */
/* PUB server for the connection-memory study.
 * Usage: srv_pub <port>
 * A background thread publishes a small tick message every 200 ms so that
 * subscriber clients can confirm their subscription is live. */

#include "mem_common.hpp"

#include <zlink.h>

#include <atomic>
#include <cstring>
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

    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    if (!pub)
        return 1;
    const int linger = 0;
    zlink_set_option (pub, ZLINK_OPT_LINGER, &linger, sizeof (linger));

    char endpoint[64];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%s", argv[1]);
    if (zlink_bind (pub, endpoint) != ZLINK_BIND_OK) {
        fprintf (stderr, "bind failed: %s\n", endpoint);
        return 1;
    }

    std::atomic<bool> stop (false);
    std::thread ticker ([&] () {
        const char *payload = "tick";
        while (!stop.load (std::memory_order_relaxed)) {
            zlink_msg_t part;
            if (zlink_msg_init_size (&part, strlen (payload)) == ZLINK_CONFIG_OK) {
                memcpy (zlink_msg_data (&part), payload, strlen (payload));
                if (zlink_publish_part (pub, "t", &part, ZLINK_SEND_FLAGS_DONTWAIT,
                                        ZLINK_PART_FINAL)
                    != ZLINK_SUBMIT_OK)
                    zlink_msg_close (&part);
            }
            usleep (200 * 1000);
        }
    });

    printf ("READY %s\n", endpoint);
    fflush (stdout);

    study::run_command_loop ({
      {"burst",
       [&] (const std::string &arg_) {
           int n = 1;
           int bytes = 64;
           sscanf (arg_.c_str (), "%d %d", &n, &bytes);
           for (int round = 0; round < n; ++round) {
               zlink_msg_t part;
               if (zlink_msg_init_size (&part, static_cast<size_t> (bytes)) != ZLINK_CONFIG_OK)
                   break;
               memset (zlink_msg_data (&part), 'x', static_cast<size_t> (bytes));
               if (zlink_publish_part (pub, "t", &part, ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL)
                   != ZLINK_SUBMIT_OK)
                   zlink_msg_close (&part);
           }
           printf ("BURST_DONE %d %d\n", n, bytes);
           fflush (stdout);
       }},
    });

    stop.store (true);
    ticker.join ();
    _exit (0);
}
