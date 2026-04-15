/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

typedef struct
{
    callback_signal_t signal;
    zlink_routing_id_t routing_id;
    char payload[256];
    size_t payload_len;
} stream_callback_ctx_t;

static int parse_tcp_endpoint (const char *endpoint, char *host,
                               size_t host_size, int *port)
{
    char proto[8] = {0};
    char addr[64] = {0};
    int p = 0;
    if (sscanf (endpoint, "%7[^:]://%63[^:]:%d", proto, addr, &p) != 3)
        return 0;
    if (strcmp (proto, "tcp") != 0 || p <= 0 || p > 65535)
        return 0;
    strncpy (host, addr, host_size - 1);
    host[host_size - 1] = '\0';
    *port = p;
    return 1;
}

static int raw_tcp_connect (const char *endpoint)
{
    char host[64];
    int port;
    assert (parse_tcp_endpoint (endpoint, host, sizeof (host), &port));

    struct sockaddr_in addr;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons ((uint16_t) port);
    int rc = inet_pton (AF_INET, host, &addr.sin_addr);
    assert (rc == 1);

    int fd = socket (AF_INET, SOCK_STREAM, 0);
    assert (fd >= 0);
    rc = connect (fd, (struct sockaddr *) &addr, sizeof (addr));
    assert (rc == 0);
    return fd;
}

static void stream_callback (const zlink_routing_id_t *source_rid,
                             zlink_msg_t *parts, size_t part_count,
                             void *userdata)
{
    stream_callback_ctx_t *cb = (stream_callback_ctx_t *) userdata;
    assert (cb != NULL);
    assert (source_rid != NULL);
    assert (part_count == 1);

    cb->routing_id = *source_rid;
    cb->payload_len = zlink_msg_size (&parts[0]);
    assert (cb->payload_len < sizeof (cb->payload));
    memcpy (cb->payload, zlink_msg_data (&parts[0]), cb->payload_len);
    cb->payload[cb->payload_len] = '\0';

    zlink_multipart_close (parts, part_count);
    callback_signal_set (&cb->signal);
}

int main (void)
{
    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    void *server = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    assert (server != NULL);

    int notify_off = 0;
    int rc = zlink_set_stream_option (server, ZLINK_STREAM_OPT_NOTIFY,
                                      &notify_off, sizeof (notify_off));
    assert (rc == 0);

    void *server_monitor = open_socket_monitor (
      server, ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED);

    rc = zlink_bind (server, "tcp://127.0.0.1:0");
    assert (rc == 0);
    char endpoint[256];
    get_last_endpoint (server, endpoint, sizeof (endpoint));

    stream_callback_ctx_t cb_ctx;
    callback_signal_init (&cb_ctx.signal);
    cb_ctx.payload_len = 0;
    rc = zlink_recv_handler (server, &stream_callback, &cb_ctx);
    assert (rc == 0);

    int client_fd = raw_tcp_connect (endpoint);
    assert (wait_stream_connected (server_monitor, 2000));

    const size_t request_size = strlen (k_stream_payload);
    ssize_t sent = send (client_fd, k_stream_payload, request_size, 0);
    assert (sent == (ssize_t) request_size);

    assert (callback_signal_wait (&cb_ctx.signal, 2000));
    assert (strcmp (cb_ctx.payload, k_stream_payload) == 0);

    zlink_msg_t reply;
    make_message (&reply, k_stream_payload);
    rc = zlink_send_rid (server, &cb_ctx.routing_id, &reply, 1, 0);
    assert (rc == 0);

    char response[64];
    ssize_t received = 0;
    while ((size_t) received < request_size) {
        ssize_t n =
          recv (client_fd, response + received, request_size - (size_t) received, 0);
        assert (n > 0);
        received += n;
    }
    assert ((size_t) received == request_size);
    assert (memcmp (response, k_stream_payload, request_size) == 0);
    printf ("[stream/callback] send: \"%s\" → recv: \"%.*s\"\n",
            k_stream_payload, (int) received, response);

    close (client_fd);
    callback_signal_destroy (&cb_ctx.signal);
    zlink_monitor_close (&server_monitor);
    zlink_close (server);
    zlink_ctx_term (ctx);
    return 0;
}
