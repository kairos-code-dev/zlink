/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

typedef struct
{
    void *server;
    void *server_monitor;
    callback_signal_t send_signal;
    char endpoint[256];
    char payload[64];
    size_t payload_len;
} stream_recv_sample_t;

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

static void stream_server_thread (void *arg_)
{
    stream_recv_sample_t *sample = (stream_recv_sample_t *) arg_;
    zlink_routing_id_t rid;
    zlink_msg_t *parts = NULL;
    size_t count = 0;

    memset (&rid, 0, sizeof (rid));
    assert (zlink_recv (sample->server, &rid, &parts, &count, 0)
            == ZLINK_RECV_OK);
    assert (rid.size > 0);
    assert (count == 1);

    sample->payload_len = zlink_msg_size (&parts[0]);
    assert (sample->payload_len == strlen (k_stream_payload));
    memcpy (sample->payload, zlink_msg_data (&parts[0]), sample->payload_len);
    sample->payload[sample->payload_len] = '\0';
    zlink_multipart_close (parts, count);
}

static void stream_client_thread (void *arg_)
{
    stream_recv_sample_t *sample = (stream_recv_sample_t *) arg_;
    const size_t request_size = strlen (k_stream_payload);
    int client_fd = raw_tcp_connect (sample->endpoint);
    ssize_t sent;

    assert (callback_signal_wait (&sample->send_signal, 2000));
    sent = send (client_fd, k_stream_payload, request_size, 0);
    assert (sent == (ssize_t) request_size);
    close (client_fd);
}

int main (void)
{
    stream_recv_sample_t sample;
    memset (&sample, 0, sizeof (sample));

    void *ctx = zlink_ctx_new ();
    assert (ctx != NULL);
    sample.server = zlink_socket (ctx, ZLINK_SOCKET_STREAM);
    assert (sample.server != NULL);

    int notify_off = 0;
    assert (zlink_set_stream_option (sample.server, ZLINK_STREAM_OPT_NOTIFY,
                                     &notify_off, sizeof (notify_off))
            == 0);

    sample.server_monitor = open_socket_monitor (
      sample.server, ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED);

    assert (zlink_bind (sample.server, "tcp://127.0.0.1:0") == ZLINK_BIND_OK);
    get_last_endpoint (sample.server, sample.endpoint, sizeof (sample.endpoint));

    callback_signal_init (&sample.send_signal);

    void *receiver = zlink_thread_start (&stream_server_thread, &sample);
    void *client = zlink_thread_start (&stream_client_thread, &sample);
    assert (receiver != NULL);
    assert (client != NULL);

    assert (wait_stream_connected (sample.server_monitor, 2000));
    callback_signal_set (&sample.send_signal);

    zlink_thread_join (client);
    zlink_thread_join (receiver);

    assert (strcmp (sample.payload, k_stream_payload) == 0);
    printf ("[stream/recv] send: \"%s\" -> recv: \"%.*s\"\n",
            k_stream_payload, (int) sample.payload_len, sample.payload);

    callback_signal_destroy (&sample.send_signal);
    zlink_monitor_close (&sample.server_monitor);
    zlink_close (sample.server);
    zlink_ctx_term (ctx);
    return 0;
}
