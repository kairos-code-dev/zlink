/* SPDX-License-Identifier: MPL-2.0 */
/* Raw STREAM server for the connection-memory study.
 * Usage: srv_stream <port>
 * Counts stream packets delivered through the packet handler. */

#include "mem_common.hpp"

#include <zlink.h>

#include <atomic>

static std::atomic<uint64_t> g_packets (0);

static void on_packet (void *, const zlink_routing_id_t *, zlink_msg_t *header_, zlink_msg_t *body_, void *)
{
    g_packets.fetch_add (1, std::memory_order_relaxed);
    if (header_)
        zlink_msg_close (header_);
    if (body_)
        zlink_msg_close (body_);
}

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

    void *stream = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    if (!stream)
        return 1;
    const int linger = 0;
    zlink_set_option (stream, ZLINK_OPT_LINGER, &linger, sizeof (linger));

    if (zlink_stream_packet_handler (stream, &on_packet, NULL) != ZLINK_HANDLER_OK) {
        fprintf (stderr, "packet handler attach failed\n");
        return 1;
    }

    char endpoint[64];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%s", argv[1]);
    if (zlink_bind (stream, endpoint) != ZLINK_BIND_OK) {
        fprintf (stderr, "bind failed: %s\n", endpoint);
        return 1;
    }

    printf ("READY %s\n", endpoint);
    fflush (stdout);

    study::run_command_loop ({
      {"count",
       [] (const std::string &) {
           printf ("COUNT %llu\n", static_cast<unsigned long long> (g_packets.load ()));
           fflush (stdout);
       }},
    });

    _exit (0);
}
