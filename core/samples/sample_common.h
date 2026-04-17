/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_C_SAMPLES_COMMON_SAMPLE_COMMON_H_INCLUDED
#define ZLINK_C_SAMPLES_COMMON_SAMPLE_COMMON_H_INCLUDED

#include <zlink.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#endif

/* ---- Constants ----------------------------------------------------------- */

static const char *const k_pair_payload = "hello-pair";
static const char *const k_dealer_router_request = "ping";
static const char *const k_dealer_router_reply = "pong";
static const char *const k_stream_payload = "hello-stream";
static const char *const k_pubsub_topic = "prices";
static const char *const k_pubsub_payload = "101.25";
static const char *const k_service_name = "sample";
static const char *const k_spot_topic = "room:lobby";
static const char *const k_spot_payload = "hello-spot";

/* ---- Message helpers ----------------------------------------------------- */

static inline void make_message (zlink_msg_t *msg, const char *text)
{
    const size_t len = strlen (text);
    int rc = zlink_msg_init_size (msg, len);
    assert (rc == 0);
    memcpy (zlink_msg_data (msg), text, len);
}

static inline uint32_t routing_id_to_u32_checked (const zlink_routing_id_t *rid)
{
    uint32_t value = 0;
    const int rc = zlink_routing_id_to_u32 (rid, &value);
    assert (rc == 0);
    return value;
}

static inline void routing_id_to_display_text (const zlink_routing_id_t *rid,
                                               char *buf,
                                               size_t buf_size)
{
    size_t text_len = buf_size;
    if (zlink_routing_id_to_text (rid, buf, &text_len) == 0)
        return;

    size_t hex_len = buf_size;
    const int rc = zlink_routing_id_to_hex (rid, buf, &hex_len);
    assert (rc == 0);
}

static inline void routing_id_copy_checked (const zlink_routing_id_t *src,
                                            zlink_routing_id_t *dst,
                                            uint8_t *storage,
                                            size_t storage_size)
{
    assert (dst != NULL);
    if (!src) {
        dst->size = 0;
        dst->data = NULL;
        return;
    }
    assert (storage != NULL || src->size == 0);
    assert (src->size <= storage_size);
    dst->size = src->size;
    if (src->size == 0) {
        dst->data = NULL;
        return;
    }
    memcpy (storage, src->data, src->size);
    dst->data = storage;
}

/* ---- Endpoint helpers ---------------------------------------------------- */

static inline void get_last_endpoint (void *socket, char *buf, size_t buf_size)
{
    size_t len = buf_size;
    int rc = zlink_get_option (socket, ZLINK_OPT_LAST_ENDPOINT, buf, &len);
    assert (rc == 0);
    assert (len > 0);
}

static inline void reserve_tcp_endpoint (char *buf, size_t buf_size)
{
#ifndef _WIN32
    int fd = socket (AF_INET, SOCK_STREAM, 0);
    assert (fd >= 0);

    int reuse = 1;
    assert (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse)) == 0);

    struct sockaddr_in addr;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = 0;
    assert (bind (fd, (const struct sockaddr *) &addr, sizeof (addr)) == 0);

    socklen_t addr_len = sizeof (addr);
    assert (getsockname (fd, (struct sockaddr *) &addr, &addr_len) == 0);
    assert (close (fd) == 0);

    snprintf (buf, buf_size, "tcp://127.0.0.1:%u", (unsigned) ntohs (addr.sin_port));
#else
    snprintf (buf, buf_size, "tcp://127.0.0.1:%u",
              (unsigned) (30000u + ((unsigned) time (NULL) % 20000u)));
#endif
}

/* ---- Monitor helpers ----------------------------------------------------- */

static inline void *open_socket_monitor (void *socket,
                                         zlink_socket_monitor_event_mask_t mask)
{
    zlink_socket_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = mask;
    void *monitor = zlink_socket_monitor_open (socket, &opts);
    assert (monitor != NULL);
    return monitor;
}

static inline int wait_for_socket_monitor_event (void *monitor,
                                                 uint64_t event_type,
                                                 int64_t value,
                                                 int timeout_ms)
{
    void *poller = zlink_poller_new ();
    assert (poller != NULL);
    int rc = zlink_poller_add (poller, monitor, NULL, ZLINK_POLLIN);
    assert (rc == 0);

    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    for (;;) {
        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining = (long) timeout_ms - elapsed_ms;
        if (remaining <= 0)
            break;

        zlink_poller_event_t pe;
        rc = zlink_poller_wait (poller, &pe, remaining, NULL);
        if (rc <= 0)
            continue;

        zlink_socket_monitor_event_t event;
        rc = zlink_socket_monitor_recv (monitor, &event, ZLINK_DONTWAIT);
        if (rc != 0)
            continue;
        if (event.event != event_type)
            continue;
        if (value >= 0 && (int64_t) event.value != value)
            continue;
        zlink_poller_destroy (&poller);
        return 1;
    }

    zlink_poller_destroy (&poller);
    return 0;
}

static inline int wait_connected (void *server_monitor, void *client_monitor,
                                  int timeout_ms)
{
    if (!wait_for_socket_monitor_event (
          server_monitor,
          ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY, -1,
          timeout_ms))
        return 0;
    if (!wait_for_socket_monitor_event (
          client_monitor,
          ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY, -1,
          timeout_ms))
        return 0;
    return 1;
}

static inline int wait_stream_connected (void *server_monitor, int timeout_ms)
{
    return wait_for_socket_monitor_event (
      server_monitor, ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED, -1, timeout_ms);
}

/* ---- Service monitor helpers --------------------------------------------- */

static inline void *open_service_monitor (void *target,
                                          zlink_service_monitor_event_mask_t mask)
{
    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = mask;
    void *monitor = zlink_service_monitor_open (target, &opts);
    assert (monitor != NULL);
    return monitor;
}

static inline int
wait_for_service_monitor_event (void *monitor, uint32_t event_type,
                                int64_t value, int timeout_ms)
{
    void *poller = zlink_poller_new ();
    assert (poller != NULL);
    int rc = zlink_poller_add (poller, monitor, NULL, ZLINK_POLLIN);
    assert (rc == 0);

    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    for (;;) {
        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining = (long) timeout_ms - elapsed_ms;
        if (remaining <= 0)
            break;

        zlink_poller_event_t pe;
        rc = zlink_poller_wait (poller, &pe, remaining, NULL);
        if (rc <= 0)
            continue;

        zlink_service_monitor_event_t event;
        rc = zlink_service_monitor_recv (monitor, &event, ZLINK_DONTWAIT);
        if (rc != 0)
            continue;
        if (event.event_type != event_type)
            continue;
        if (value >= 0 && (int64_t) event.value != value)
            continue;
        zlink_poller_destroy (&poller);
        return 1;
    }

    zlink_poller_destroy (&poller);
    return 0;
}

static inline int
wait_for_service_monitor_event_endpoint (void *monitor, uint32_t event_type,
                                         const char *endpoint, int timeout_ms)
{
    void *poller = zlink_poller_new ();
    assert (poller != NULL);
    int rc = zlink_poller_add (poller, monitor, NULL, ZLINK_POLLIN);
    assert (rc == 0);

    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    for (;;) {
        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining = (long) timeout_ms - elapsed_ms;
        if (remaining <= 0)
            break;

        zlink_poller_event_t pe;
        rc = zlink_poller_wait (poller, &pe, remaining, NULL);
        if (rc <= 0)
            continue;

        zlink_service_monitor_event_t event;
        rc = zlink_service_monitor_recv (monitor, &event, ZLINK_DONTWAIT);
        if (rc != 0)
            continue;
        if (event.event_type != event_type)
            continue;
        if ((event.detail_flags & ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT) == 0)
            continue;
        if (strcmp (event.endpoint, endpoint) != 0)
            continue;
        zlink_poller_destroy (&poller);
        return 1;
    }

    zlink_poller_destroy (&poller);
    return 0;
}

static inline int
wait_for_service_monitor_state (void *monitor, zlink_monitor_state_mask_t state,
                                int timeout_ms)
{
    void *poller = zlink_poller_new ();
    assert (poller != NULL);
    int rc = zlink_poller_add (poller, monitor, NULL, ZLINK_POLLIN);
    assert (rc == 0);

    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    for (;;) {
        zlink_monitor_snapshot_t snapshot;
        if (zlink_monitor_snapshot (monitor, &snapshot) == 0
            && (snapshot.state_flags & state) == state) {
            zlink_poller_destroy (&poller);
            return 1;
        }

        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining = (long) timeout_ms - elapsed_ms;
        if (remaining <= 0)
            break;
        if (remaining > 200)
            remaining = 200;

        zlink_poller_event_t pe;
        rc = zlink_poller_wait (poller, &pe, remaining, NULL);
        if (rc <= 0)
            continue;

        zlink_service_monitor_event_t event;
        zlink_service_monitor_recv (monitor, &event, ZLINK_DONTWAIT);
    }

    zlink_poller_destroy (&poller);
    return 0;
}

static inline int wait_spot_ready (void *sub_monitor, void *pub_monitor,
                                   const char *endpoint, int timeout_ms)
{
    (void) endpoint;
    if (!wait_for_service_monitor_event (
          sub_monitor,
          ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED, -1,
          timeout_ms))
        return 0;
    if (!wait_for_service_monitor_event (
          pub_monitor,
          ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED, -1,
          timeout_ms))
        return 0;
    return 1;
}

static inline int wait_for_spot_node_subject_ready (void *node_,
                                                    int timeout_ms)
{
    struct timespec start;
    clock_gettime (CLOCK_MONOTONIC, &start);

    for (;;) {
        zlink_spot_node_status_t status;
        if (zlink_spot_node_status_snapshot (node_, &status) == 0
            && status.subject_count > 0
            && (status.ready_subject_count > 0
                || status.connected_peer_count > 0
                || status.active_peer_count > 0
                || status.configured_peer_count == 0)) {
            return 1;
        }

        struct timespec now;
        clock_gettime (CLOCK_MONOTONIC, &now);
        long elapsed_ms =
          (long) (now.tv_sec - start.tv_sec) * 1000
          + (long) (now.tv_nsec - start.tv_nsec) / 1000000;
        long remaining = (long) timeout_ms - elapsed_ms;
        if (remaining <= 0)
            break;
        if (remaining > 10)
            remaining = 10;

        zlink_pollitem_t item = {NULL, 0, 0, 0};
        (void) zlink_poll (&item, 0, remaining, NULL);
    }

    return 0;
}

/* ---- Callback synchronization primitives (POSIX) ------------------------- */

#ifndef _WIN32

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int ready;
} callback_signal_t;

static inline void callback_signal_init (callback_signal_t *sig)
{
    pthread_mutex_init (&sig->mutex, NULL);
    pthread_cond_init (&sig->cond, NULL);
    sig->ready = 0;
}

static inline void callback_signal_destroy (callback_signal_t *sig)
{
    pthread_cond_destroy (&sig->cond);
    pthread_mutex_destroy (&sig->mutex);
}

static inline void callback_signal_set (callback_signal_t *sig)
{
    pthread_mutex_lock (&sig->mutex);
    sig->ready = 1;
    pthread_cond_signal (&sig->cond);
    pthread_mutex_unlock (&sig->mutex);
}

static inline int callback_signal_wait (callback_signal_t *sig, int timeout_ms)
{
    struct timespec ts;
    clock_gettime (CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock (&sig->mutex);
    while (!sig->ready) {
        if (pthread_cond_timedwait (&sig->cond, &sig->mutex, &ts) != 0) {
            pthread_mutex_unlock (&sig->mutex);
            return 0;
        }
    }
    pthread_mutex_unlock (&sig->mutex);
    return 1;
}

#endif /* _WIN32 */

#endif /* ZLINK_C_SAMPLES_COMMON_SAMPLE_COMMON_H_INCLUDED */
