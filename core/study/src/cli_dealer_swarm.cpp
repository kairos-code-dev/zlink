/* SPDX-License-Identifier: MPL-2.0 */
/* DEALER swarm client for the connection-memory study.
 * Usage: cli_dealer_swarm <endpoint> <count>
 * Creates <count> DEALER sockets, connects each to <endpoint> and sends one
 * small message per socket so the server can count completed handshakes.
 * Prints DONE, then waits for commands (burst/rss/quit).
 *   burst <n> <bytes>  -> every dealer sends n messages of the given size */

#include "mem_common.hpp"

#include <zlink.h>

#include <chrono>
#include <string>
#include <vector>

static void send_text (void *socket_, const char *payload_, size_t size_)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size_) != ZLINK_CONFIG_OK)
        return;
    memset (zlink_msg_data (&part), 'x', size_);
    if (size_ >= 2)
        memcpy (zlink_msg_data (&part), payload_, size_ < strlen (payload_) ? size_ : strlen (payload_));
    /* Blocking send keeps the swarm simple; HWM stalls just slow the burst. */
    if (zlink_send_part (socket_, &part, ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL)
        != ZLINK_SUBMIT_OK)
        zlink_msg_close (&part);
}

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

    std::vector<void *> dealers;
    dealers.reserve (count);
    const int linger = 0;
    for (int i = 0; i < count; ++i) {
        void *dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
        if (!dealer) {
            fprintf (stderr, "socket %d creation failed\n", i);
            return 1;
        }
        zlink_set_option (dealer, ZLINK_OPT_LINGER, &linger, sizeof (linger));
        if (zlink_connect (dealer, endpoint) != ZLINK_CONNECT_OK) {
            fprintf (stderr, "connect %d failed\n", i);
            return 1;
        }
        send_text (dealer, "hi", 2);
        dealers.push_back (dealer);
        if ((i + 1) % 500 == 0) {
            printf ("PROGRESS %d\n", i + 1);
            fflush (stdout);
        }
    }

    printf ("DONE %d\n", count);
    fflush (stdout);

    study::run_command_loop ({
      {"burst",
       [&] (const std::string &arg_) {
           int n = 1;
           int bytes = 64;
           sscanf (arg_.c_str (), "%d %d", &n, &bytes);
           const auto start = std::chrono::steady_clock::now ();
           for (int round = 0; round < n; ++round)
               for (void *dealer : dealers)
                   send_text (dealer, "bb", static_cast<size_t> (bytes));
           const double elapsed_ms =
             std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - start)
               .count ();
           printf ("BURST_DONE %d %d elapsed_ms=%.1f\n", n, bytes, elapsed_ms);
           fflush (stdout);
       }},
    });

    _exit (0);
}
