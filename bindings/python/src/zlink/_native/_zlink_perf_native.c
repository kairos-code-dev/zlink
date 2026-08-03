/* SPDX-License-Identifier: MPL-2.0 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlink.h>

typedef struct latency_samples_t
{
    double *values;
    size_t count;
    size_t capacity;
    double sum;
} latency_samples_t;
typedef struct single_socket_bench_t
{
    void *sender;
    void *receiver;
    size_t msg_size;
    int duration_s;
    int send_mode;
    int recv_mode;
    uint32_t run_id;
    zlink_routing_id_t target_rid;
    char topic[256];
    size_t topic_len;
    uint64_t sent;
    uint64_t received;
    atomic_int ok;
    atomic_int error;
    pthread_mutex_t latency_lock;
    latency_samples_t latencies;
} single_socket_bench_t;

static int single_socket_bench_is_ok (const single_socket_bench_t *bench)
{
    return atomic_load_explicit (&bench->ok, memory_order_acquire);
}

static void single_socket_bench_fail (single_socket_bench_t *bench, int error)
{
    atomic_store_explicit (&bench->error, error, memory_order_release);
    atomic_store_explicit (&bench->ok, 0, memory_order_release);
}

typedef struct routed_echo_item_t
{
    zlink_routing_id_t routing_id;
    zlink_msg_t msg;
    struct routed_echo_item_t *next;
} routed_echo_item_t;

typedef struct routed_echo_queue_t
{
    routed_echo_item_t *head;
    routed_echo_item_t *tail;
    unsigned long long pending_count;
} routed_echo_queue_t;


static int copy_routing_id (Py_buffer *view, zlink_routing_id_t *rid)
{
    if (view->len <= 0 || view->len > 255) {
        PyErr_SetString (PyExc_ValueError, "routing_id length must be between 1 and 255");
        return -1;
    }
    rid->size = (uint8_t) view->len;
    memcpy (rid->data, view->buf, (size_t) view->len);
    return 0;
}

static uint64_t now_ns (void)
{
    struct timespec ts;
    clock_gettime (CLOCK_REALTIME, &ts);
    return ((uint64_t) ts.tv_sec * 1000000000ULL) + (uint64_t) ts.tv_nsec;
}

static void write_le32 (unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char) (value & 0xffu);
    dst[1] = (unsigned char) ((value >> 8) & 0xffu);
    dst[2] = (unsigned char) ((value >> 16) & 0xffu);
    dst[3] = (unsigned char) ((value >> 24) & 0xffu);
}

static void write_le64 (unsigned char *dst, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        dst[i] = (unsigned char) ((value >> (i * 8)) & 0xffu);
}

static uint32_t read_le32 (const unsigned char *src)
{
    return ((uint32_t) src[0]) | ((uint32_t) src[1] << 8) | ((uint32_t) src[2] << 16)
           | ((uint32_t) src[3] << 24);
}

static uint64_t read_le64 (const unsigned char *src)
{
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i)
        value = (value << 8) | (uint64_t) src[i];
    return value;
}

static PyObject *py_perf_stamp_payload (PyObject *self, PyObject *args)
{
    static uint64_t seq = 0;
    PyObject *payload = NULL;
    Py_buffer view = {0};
    int phase = 0;
    unsigned int run_id = 0;
    long long requested_seq = -1;
    uint64_t header_seq = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "OiIL", &payload, &phase, &run_id, &requested_seq))
        return NULL;
    if (PyObject_GetBuffer (payload, &view, PyBUF_WRITABLE) != 0)
        return NULL;
    if (view.len < 29) {
        PyBuffer_Release (&view);
        PyErr_SetString (PyExc_ValueError, "payload is too small for perf header");
        return NULL;
    }

    header_seq = requested_seq < 0 ? seq++ : (uint64_t) requested_seq;
    unsigned char *dst = (unsigned char *) view.buf;
    write_le32 (dst, 0x5A4C4E4Bu);
    write_le32 (dst + 4, (uint32_t) run_id);
    dst[8] = (unsigned char) phase;
    write_le32 (dst + 9, (uint32_t) view.len);
    write_le64 (dst + 13, header_seq);
    write_le64 (dst + 21, now_ns ());
    PyBuffer_Release (&view);
    Py_INCREF (payload);
    return payload;
}

static PyObject *py_perf_active_latency_ns (PyObject *self, PyObject *args)
{
    static const char stop_token[] = "__zlink_perf_stop__";
    PyObject *data_obj = NULL;
    Py_buffer data = {0};
    int msg_size = 0;
    unsigned int run_id = 0;
    const unsigned char *src = NULL;
    uint32_t magic = 0;
    uint32_t header_run_id = 0;
    uint8_t phase = 0;
    uint32_t header_msg_size = 0;
    int64_t sent_ts_ns = 0;
    uint64_t now = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "OiI", &data_obj, &msg_size, &run_id))
        return NULL;
    if (PyObject_GetBuffer (data_obj, &data, PyBUF_CONTIG_RO) != 0)
        return NULL;
    if (data.len == (Py_ssize_t) (sizeof (stop_token) - 1)
        && memcmp (data.buf, stop_token, sizeof (stop_token) - 1) == 0) {
        PyBuffer_Release (&data);
        return PyFloat_FromDouble (-1.0);
    }
    if (data.len != msg_size || data.len < 29) {
        PyBuffer_Release (&data);
        return PyFloat_FromDouble (-2.0);
    }
    src = (const unsigned char *) data.buf;
    magic = read_le32 (src);
    header_run_id = read_le32 (src + 4);
    phase = src[8];
    header_msg_size = read_le32 (src + 9);
    sent_ts_ns = (int64_t) read_le64 (src + 21);
    PyBuffer_Release (&data);

    if (magic != 0x5A4C4E4Bu || phase != 1 || header_msg_size != (uint32_t) msg_size
        || header_run_id != run_id)
        return PyFloat_FromDouble (-2.0);
    now = now_ns ();
    if (sent_ts_ns > 0 && now >= (uint64_t) sent_ts_ns)
        return PyFloat_FromDouble ((double) (now - (uint64_t) sent_ts_ns));
    return PyFloat_FromDouble (0.0);
}

static uint64_t steady_ns (void)
{
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ((uint64_t) ts.tv_sec * 1000000000ULL) + (uint64_t) ts.tv_nsec;
}

static void write_u32_le (unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char) (value & 0xffU);
    dst[1] = (unsigned char) ((value >> 8) & 0xffU);
    dst[2] = (unsigned char) ((value >> 16) & 0xffU);
    dst[3] = (unsigned char) ((value >> 24) & 0xffU);
}

static void write_u64_le (unsigned char *dst, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i)
        dst[i] = (unsigned char) ((value >> (i * 8)) & 0xffU);
}

static uint32_t read_u32_le (const unsigned char *src)
{
    return (uint32_t) src[0] | ((uint32_t) src[1] << 8) | ((uint32_t) src[2] << 16)
           | ((uint32_t) src[3] << 24);
}

static uint64_t read_u64_le (const unsigned char *src)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value |= ((uint64_t) src[i]) << (i * 8);
    return value;
}

static int stamp_single_payload (
  unsigned char *payload, size_t size, uint32_t run_id, uint8_t phase, uint64_t seq)
{
    if (!payload || size < 29)
        return 0;
    write_u32_le (payload + 0, 0x5A4C4E4BU);
    write_u32_le (payload + 4, run_id);
    payload[8] = phase;
    write_u32_le (payload + 9, (uint32_t) size);
    write_u64_le (payload + 13, seq);
    write_u64_le (payload + 21, now_ns ());
    return 1;
}

static int stamp_active_payload (unsigned char *payload, size_t size, uint32_t run_id, uint64_t seq)
{
    return stamp_single_payload (payload, size, run_id, 1, seq);
}

static int payload_is_stop_token (const void *data, size_t size)
{
    static const char stop_token[] = "__zlink_perf_stop__";
    return data && size == sizeof (stop_token) - 1
           && memcmp (data, stop_token, sizeof (stop_token) - 1) == 0;
}

static int
decode_single_payload (const void *data, size_t size, uint32_t run_id, uint64_t *sent_ts_out)
{
    const unsigned char *p = (const unsigned char *) data;
    if (!p || size < 29 || read_u32_le (p + 0) != 0x5A4C4E4BU || read_u32_le (p + 4) != run_id
        || p[8] != 1 || read_u32_le (p + 9) != (uint32_t) size)
        return 0;
    *sent_ts_out = read_u64_le (p + 21);
    return 1;
}

static int latency_samples_add (latency_samples_t *samples, double value)
{
    if (samples->count == samples->capacity) {
        size_t next_capacity = samples->capacity == 0 ? 1024 : samples->capacity * 2;
        double *next = (double *) realloc (samples->values, next_capacity * sizeof (double));
        if (!next)
            return 0;
        samples->values = next;
        samples->capacity = next_capacity;
    }
    samples->values[samples->count++] = value >= 0.0 ? value : 0.0;
    samples->sum += samples->values[samples->count - 1];
    return 1;
}

static int compare_double (const void *a, const void *b)
{
    const double da = *(const double *) a;
    const double db = *(const double *) b;
    return (da > db) - (da < db);
}

static double latency_percentile_sorted (const latency_samples_t *samples, double ratio)
{
    if (!samples || samples->count == 0)
        return 0.0;
    size_t index =
      ratio >= 0.99 ? (samples->count * 99 + 99) / 100 : (samples->count * 95 + 99) / 100;
    if (index == 0)
        index = 1;
    if (index > samples->count)
        index = samples->count;
    return samples->values[index - 1];
}

static int send_owned_part_mode (single_socket_bench_t *bench, zlink_msg_t *part, int flags)
{
    int rc = ZLINK_SUBMIT_INTERNAL_ERROR;
    if (bench->send_mode == 1) {
        rc = zlink_send_part_rid (bench->sender, &bench->target_rid, part,
                                  (zlink_send_flags_t) flags, ZLINK_PART_FINAL);
    } else if (bench->send_mode == 2) {
        rc = zlink_publish_part (bench->sender, bench->topic, part, (zlink_send_flags_t) flags,
                                 ZLINK_PART_FINAL);
    } else {
        rc = zlink_send_part (bench->sender, part, (zlink_send_flags_t) flags, ZLINK_PART_FINAL);
    }
    if (rc == ZLINK_SUBMIT_OK)
        return 0;
    const int err = zlink_errno ();
    zlink_msg_close (part);
    if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT
        || err == EHOSTUNREACH || err == ENOTCONN)
        return 1;
    return -1;
}

static int send_stop_token_socket (single_socket_bench_t *bench)
{
    static const char stop_token[] = "__zlink_perf_stop__";
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, sizeof (stop_token) - 1) != 0)
        return -1;
    memcpy (zlink_msg_data (&part), stop_token, sizeof (stop_token) - 1);
    return send_owned_part_mode (bench, &part, ZLINK_SEND_FLAGS_NONE);
}

static int recv_part_mode (single_socket_bench_t *bench,
                           zlink_msg_t *part,
                           zlink_part_flag_t *has_more,
                           int flags)
{
    if (bench->recv_mode == 1) {
        const zlink_routing_id_t *source_rid = NULL;
        uint64_t request_seq = 0;
        int rc = zlink_router_recv_part (bench->receiver, &source_rid, &request_seq, part,
                                         has_more, (zlink_recv_flags_t) flags);
        if (rc != ZLINK_RECV_OK)
            return rc;
        if (!source_rid || source_rid->size == 0 || request_seq != 0)
            return ZLINK_RECV_INTERNAL_ERROR;
        return rc;
    }
    if (bench->recv_mode == 2) {
        const zlink_routing_id_t *source_rid = NULL;
        char topic[256];
        size_t topic_len = 0;
        int rc = zlink_subscribe_part (bench->receiver, &source_rid, topic, sizeof (topic),
                                       &topic_len, part, has_more, (zlink_recv_flags_t) flags);
        if (rc != ZLINK_RECV_OK)
            return rc;
        if (source_rid || topic_len != bench->topic_len
            || memcmp (topic, bench->topic, topic_len) != 0)
            return ZLINK_RECV_INTERNAL_ERROR;
        return rc;
    }
    {
        const zlink_routing_id_t *source_rid = NULL;
        int rc = zlink_recv_part (bench->receiver, &source_rid, part, has_more,
                                  (zlink_recv_flags_t) flags);
        if (rc == ZLINK_RECV_OK && source_rid)
            return ZLINK_RECV_INTERNAL_ERROR;
        return rc;
    }
}

static void *single_socket_sender_thread (void *arg)
{
    single_socket_bench_t *bench = (single_socket_bench_t *) arg;
    const uint64_t deadline = steady_ns () + ((uint64_t) bench->duration_s * 1000000000ULL);
    uint64_t seq = 1;

    while (steady_ns () < deadline) {
        zlink_msg_t part;
        if (zlink_msg_init_size (&part, bench->msg_size) != 0
            || !stamp_active_payload ((unsigned char *) zlink_msg_data (&part), bench->msg_size,
                                      bench->run_id, seq)) {
            single_socket_bench_fail (bench, zlink_errno ());
            return NULL;
        }
        int step = send_owned_part_mode (bench, &part, ZLINK_SEND_FLAGS_NONE);
        if (step < 0) {
            single_socket_bench_fail (bench, zlink_errno ());
            return NULL;
        }
        if (step > 0)
            continue;
        ++bench->sent;
        ++seq;
    }

    for (int retry = 0; retry < 100; ++retry) {
        int step = send_stop_token_socket (bench);
        if (step == 0)
            return NULL;
        if (step < 0)
            break;
        struct timespec ts = {0, 1000000L};
        nanosleep (&ts, NULL);
    }
    single_socket_bench_fail (bench, zlink_errno ());
    return NULL;
}

static void *single_socket_receiver_thread (void *arg)
{
    single_socket_bench_t *bench = (single_socket_bench_t *) arg;
    const uint64_t deadline = steady_ns () + ((uint64_t) bench->duration_s * 1000000000ULL);

    while (single_socket_bench_is_ok (bench)) {
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        if (zlink_msg_init (&part) != 0) {
            single_socket_bench_fail (bench, zlink_errno ());
            return NULL;
        }
        const int rc = recv_part_mode (bench, &part, &has_more, ZLINK_RECV_FLAGS_NONE);
        if (rc != ZLINK_RECV_OK) {
            const int err = zlink_errno ();
            zlink_msg_close (&part);
            if (err == EAGAIN || err == EINTR)
                continue;
            single_socket_bench_fail (bench, err);
            return NULL;
        }

        void *data = zlink_msg_data (&part);
        size_t size = zlink_msg_size (&part);
        if (payload_is_stop_token (data, size)) {
            zlink_msg_close (&part);
            return NULL;
        }
        if (has_more == ZLINK_PART_FINAL && size == bench->msg_size) {
            uint64_t sent_ts = 0;
            if (decode_single_payload (data, size, bench->run_id, &sent_ts)
                && steady_ns () < deadline) {
                uint64_t recv_ns = now_ns ();
                double sample = recv_ns >= sent_ts ? (double) (recv_ns - sent_ts) : 0.0;
                pthread_mutex_lock (&bench->latency_lock);
                if (!latency_samples_add (&bench->latencies, sample)) {
                    pthread_mutex_unlock (&bench->latency_lock);
                    zlink_msg_close (&part);
                    single_socket_bench_fail (bench, ENOMEM);
                    return NULL;
                }
                pthread_mutex_unlock (&bench->latency_lock);
                ++bench->received;
            }
        }
        zlink_msg_close (&part);

        for (;;) {
            has_more = ZLINK_PART_FINAL;
            if (zlink_msg_init (&part) != 0) {
                single_socket_bench_fail (bench, zlink_errno ());
                return NULL;
            }
            const int burst_rc = recv_part_mode (bench, &part, &has_more, ZLINK_DONTWAIT);
            if (burst_rc != ZLINK_RECV_OK) {
                const int err = zlink_errno ();
                zlink_msg_close (&part);
                if (err == EAGAIN || err == EINTR)
                    break;
                single_socket_bench_fail (bench, err);
                return NULL;
            }
            data = zlink_msg_data (&part);
            size = zlink_msg_size (&part);
            if (payload_is_stop_token (data, size)) {
                zlink_msg_close (&part);
                return NULL;
            }
            if (has_more == ZLINK_PART_FINAL && size == bench->msg_size) {
                uint64_t sent_ts = 0;
                if (decode_single_payload (data, size, bench->run_id, &sent_ts)
                    && steady_ns () < deadline) {
                    uint64_t recv_ns = now_ns ();
                    double sample = recv_ns >= sent_ts ? (double) (recv_ns - sent_ts) : 0.0;
                    pthread_mutex_lock (&bench->latency_lock);
                    if (!latency_samples_add (&bench->latencies, sample)) {
                        pthread_mutex_unlock (&bench->latency_lock);
                        zlink_msg_close (&part);
                        single_socket_bench_fail (bench, ENOMEM);
                        return NULL;
                    }
                    pthread_mutex_unlock (&bench->latency_lock);
                    ++bench->received;
                }
            }
            zlink_msg_close (&part);
        }
    }
    return NULL;
}

static PyObject *py_single_socket_one_way (PyObject *self, PyObject *args)
{
    unsigned long long sender_value = 0;
    unsigned long long receiver_value = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    int send_mode = 0;
    int recv_mode = 0;
    Py_buffer aux = {0};
    pthread_t sender_thread;
    pthread_t receiver_thread;
    int sender_started = 0;
    int receiver_started = 0;
    single_socket_bench_t bench;

    (void) self;
    if (!PyArg_ParseTuple (args, "KKniK|iiy*", &sender_value, &receiver_value, &msg_size,
                           &duration_s, &run_id_value, &send_mode, &recv_mode, &aux))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        if (aux.obj)
            PyBuffer_Release (&aux);
        PyErr_SetString (PyExc_ValueError, "msg_size must be >= 29 and duration_s must be > 0");
        return NULL;
    }
    if (send_mode < 0 || send_mode > 2 || recv_mode < 0 || recv_mode > 2) {
        if (aux.obj)
            PyBuffer_Release (&aux);
        PyErr_SetString (PyExc_ValueError, "invalid single benchmark mode");
        return NULL;
    }
    if ((send_mode == 1 || send_mode == 2 || recv_mode == 2) && !aux.obj) {
        PyErr_SetString (PyExc_ValueError,
                         "routed and pubsub single benchmark modes require aux bytes");
        return NULL;
    }

    memset (&bench, 0, sizeof (bench));
    bench.sender = (void *) (uintptr_t) sender_value;
    bench.receiver = (void *) (uintptr_t) receiver_value;
    bench.msg_size = (size_t) msg_size;
    bench.duration_s = duration_s;
    bench.send_mode = send_mode;
    bench.recv_mode = recv_mode;
    bench.run_id = (uint32_t) run_id_value;
    atomic_init (&bench.ok, 1);
    atomic_init (&bench.error, 0);
    if (send_mode == 1) {
        if (aux.len <= 0 || aux.len > 255) {
            PyBuffer_Release (&aux);
            PyErr_SetString (PyExc_ValueError, "routing_id length must be between 1 and 255");
            return NULL;
        }
        bench.target_rid.size = (uint8_t) aux.len;
        memcpy (bench.target_rid.data, aux.buf, (size_t) aux.len);
    }
    if (send_mode == 2 || recv_mode == 2) {
        if (aux.len <= 0 || aux.len >= (Py_ssize_t) sizeof (bench.topic)
            || memchr (aux.buf, '\0', (size_t) aux.len)) {
            PyBuffer_Release (&aux);
            PyErr_SetString (PyExc_ValueError, "topic must be a non-empty C string");
            return NULL;
        }
        memcpy (bench.topic, aux.buf, (size_t) aux.len);
        bench.topic[(size_t) aux.len] = '\0';
        bench.topic_len = (size_t) aux.len;
    }
    if (aux.obj)
        PyBuffer_Release (&aux);
    pthread_mutex_init (&bench.latency_lock, NULL);

    Py_BEGIN_ALLOW_THREADS if (pthread_create (&receiver_thread, NULL,
                                               single_socket_receiver_thread, &bench)
                               == 0)
    {
        receiver_started = 1;
        if (pthread_create (&sender_thread, NULL, single_socket_sender_thread, &bench) == 0) {
            sender_started = 1;
        } else {
            single_socket_bench_fail (&bench, errno);
        }
    }
    else
    {
        single_socket_bench_fail (&bench, errno);
    }
    if (sender_started)
        pthread_join (sender_thread, NULL);
    if (receiver_started)
        pthread_join (receiver_thread, NULL);
    Py_END_ALLOW_THREADS

      pthread_mutex_destroy (&bench.latency_lock);
    if (!single_socket_bench_is_ok (&bench) || bench.received == 0) {
        int err = atomic_load_explicit (&bench.error, memory_order_acquire);
        free (bench.latencies.values);
        return Py_BuildValue ("Kdddddi", (unsigned long long) bench.received, 0.0, 0.0, 0.0, 0.0,
                              0.0, err);
    }

    qsort (bench.latencies.values, bench.latencies.count, sizeof (double), compare_double);
    double mean_ms = (bench.latencies.sum / (double) bench.latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&bench.latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&bench.latencies, 0.99) / 1000000.0;
    double throughput = (double) bench.received / (double) duration_s;
    double bandwidth =
      ((double) bench.received * (double) bench.msg_size) / (double) duration_s / 1000000.0;
    free (bench.latencies.values);
    return Py_BuildValue ("Kdddddi", (unsigned long long) bench.received, throughput, bandwidth,
                          mean_ms, p95_ms, p99_ms, 0);
}

static int send_payload_one_part (void *handle, const void *data, size_t size, int flags)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), data, size);
    if (zlink_send_part (handle, &part, (zlink_send_flags_t) flags, ZLINK_PART_FINAL)
        == ZLINK_SUBMIT_OK)
        return 0;
    {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT
            || err == ENOTCONN || err == EHOSTUNREACH || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static int send_payload_one_part_rid (
  void *handle, const zlink_routing_id_t *rid, const void *data, size_t size, int flags)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), data, size);
    if (zlink_send_part_rid (handle, rid, &part, (zlink_send_flags_t) flags, ZLINK_PART_FINAL)
        == ZLINK_SUBMIT_OK)
        return 0;
    {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT
            || err == ENOTCONN || err == EHOSTUNREACH || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static PyObject *py_multi_send_one_way (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    void **handles = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    unsigned char *payload = NULL;
    unsigned long long sent = 0;
    int ok = 1;
    int err = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "OniK", &handles_obj, &msg_size, &duration_s, &run_id_value))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyErr_SetString (PyExc_ValueError, "invalid multi send arguments");
        return NULL;
    }
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq)
        return NULL;
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }
    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    payload = (unsigned char *) malloc ((size_t) msg_size);
    if (!handles || !payload) {
        free (handles);
        free (payload);
        Py_DECREF (handles_seq);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            free (payload);
            Py_DECREF (handles_seq);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
    }
    memset (payload, 'm', (size_t) msg_size);

    const uint64_t deadline = steady_ns () + ((uint64_t) duration_s * 1000000000ULL);
    uint64_t seq = 1;

    Py_BEGIN_ALLOW_THREADS while (steady_ns () < deadline)
    {
        int progressed = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            if (steady_ns () >= deadline)
                break;
            if (!stamp_single_payload (payload, (size_t) msg_size, (uint32_t) run_id_value, 1,
                                       seq)) {
                ok = 0;
                err = EINVAL;
                break;
            }
            int step =
              send_payload_one_part (handles[i], payload, (size_t) msg_size, ZLINK_DONTWAIT);
            if (step < 0) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            if (step == 0) {
                progressed = 1;
                ++sent;
                ++seq;
            }
        }
        if (!ok)
            break;
        if (!progressed) {
            struct timespec ts = {0, 100000L};
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

      free (handles);
    free (payload);
    Py_DECREF (handles_seq);
    return Py_BuildValue ("Ki", sent, ok ? 0 : err);
}

static int recv_echo_one_part (void *handle,
                               int router_recv,
                               uint32_t run_id,
                               size_t msg_size,
                               double latency_divisor,
                               latency_samples_t *latencies,
                               unsigned long long *received)
{
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    int rc = ZLINK_RECV_OK;
    uint64_t sent_ts = 0;

    if (zlink_msg_init (&part) != 0)
        return -1;
    if (router_recv) {
        const zlink_routing_id_t *peer_rid = NULL;
        uint64_t request_seq = 0;
        rc = zlink_router_recv_part (handle, &peer_rid, &request_seq, &part, &has_more,
                                     ZLINK_DONTWAIT);
    } else {
        const zlink_routing_id_t *source_rid = NULL;
        rc = zlink_recv_part (handle, &source_rid, &part, &has_more, ZLINK_DONTWAIT);
    }
    if (rc != ZLINK_RECV_OK) {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
            return 1;
        return -1;
    }
    if (has_more == ZLINK_PART_FINAL
        && decode_single_payload (zlink_msg_data (&part), zlink_msg_size (&part), run_id,
                                  &sent_ts)) {
        const uint64_t now = now_ns ();
        const double sample =
          sent_ts > 0 && now >= sent_ts ? ((double) (now - sent_ts)) / latency_divisor : 0.0;
        *received += 1;
        if (!latency_samples_add (latencies, sample)) {
            zlink_msg_close (&part);
            errno = ENOMEM;
            return -1;
        }
    }
    zlink_msg_close (&part);
    (void) msg_size;
    return 0;
}

static PyObject *py_multi_echo_roundtrip (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    void **handles = NULL;
    unsigned char *payloads = NULL;
    unsigned char *awaiting_reply = NULL;
    unsigned char *send_pending = NULL;
    zlink_pollitem_t *items = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    int router_mode = 0;
    Py_buffer rid_view = {0};
    zlink_routing_id_t server_rid;
    unsigned long long received = 0;
    uint64_t seq = 1;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = {0};

    (void) self;
    memset (&server_rid, 0, sizeof (server_rid));
    if (!PyArg_ParseTuple (args, "OniKiy*", &handles_obj, &msg_size, &duration_s, &run_id_value,
                           &router_mode, &rid_view))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyBuffer_Release (&rid_view);
        PyErr_SetString (PyExc_ValueError, "invalid multi echo arguments");
        return NULL;
    }
    if (copy_routing_id (&rid_view, &server_rid) != 0) {
        PyBuffer_Release (&rid_view);
        return NULL;
    }
    PyBuffer_Release (&rid_view);
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq)
        return NULL;
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }

    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    payloads = (unsigned char *) malloc ((size_t) handle_count * (size_t) msg_size);
    awaiting_reply = (unsigned char *) calloc ((size_t) handle_count, 1);
    send_pending = (unsigned char *) calloc ((size_t) handle_count, 1);
    items = (zlink_pollitem_t *) calloc ((size_t) handle_count, sizeof (zlink_pollitem_t));
    if (!handles || !payloads || !awaiting_reply || !send_pending || !items) {
        free (handles);
        free (payloads);
        free (awaiting_reply);
        free (send_pending);
        free (items);
        Py_DECREF (handles_seq);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            free (payloads);
            free (awaiting_reply);
            free (send_pending);
            free (items);
            Py_DECREF (handles_seq);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
        memset (payloads + ((size_t) i * (size_t) msg_size), 'm', (size_t) msg_size);
        send_pending[i] = 1;
    }
    Py_DECREF (handles_seq);

    const uint64_t deadline = steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS while (steady_ns () < deadline && ok)
    {
        int any_interest = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            if (awaiting_reply[i] || !send_pending[i])
                continue;
            unsigned char *payload = payloads + ((size_t) i * (size_t) msg_size);
            if (!stamp_single_payload (payload, (size_t) msg_size, (uint32_t) run_id_value, 1,
                                       seq)) {
                ok = 0;
                err = EINVAL;
                break;
            }
            int send_rc =
              router_mode
                ? send_payload_one_part_rid (handles[i], &server_rid, payload, (size_t) msg_size,
                                             ZLINK_DONTWAIT)
                : send_payload_one_part (handles[i], payload, (size_t) msg_size, ZLINK_DONTWAIT);
            if (send_rc < 0) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            if (send_rc == 0) {
                awaiting_reply[i] = 1;
                send_pending[i] = 0;
                ++seq;
            }
        }
        if (!ok)
            break;

        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            short events = 0;
            if (awaiting_reply[i])
                events |= ZLINK_POLLIN;
            else if (send_pending[i])
                events |= ZLINK_POLLOUT;
            items[i].socket = handles[i];
            items[i].fd = 0;
            items[i].events = events;
            items[i].revents = 0;
            if (events)
                any_interest = 1;
        }
        if (!any_interest) {
            for (Py_ssize_t i = 0; i < handle_count; ++i) {
                if (!awaiting_reply[i])
                    send_pending[i] = 1;
            }
            continue;
        }
        uint64_t now_steady = steady_ns ();
        if (now_steady >= deadline)
            break;
        long remaining_ms = (long) ((deadline - now_steady) / 1000000ULL);
        if (remaining_ms <= 0)
            remaining_ms = 1;
        zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
        int poll_rc = zlink_poll (items, (int) handle_count, remaining_ms, &poll_error);
        if (poll_rc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            ok = 0;
            err = zlink_errno ();
            if (err == 0 && poll_error != ZLINK_CONFIG_OK)
                err = EINVAL;
            break;
        }
        if (poll_rc == 0)
            continue;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            if ((items[i].revents & ZLINK_POLLOUT) && !awaiting_reply[i] && send_pending[i]) {
                unsigned char *payload = payloads + ((size_t) i * (size_t) msg_size);
                if (!stamp_single_payload (payload, (size_t) msg_size, (uint32_t) run_id_value, 1,
                                           seq)) {
                    ok = 0;
                    err = EINVAL;
                    break;
                }
                int send_rc = router_mode
                                ? send_payload_one_part_rid (handles[i], &server_rid, payload,
                                                             (size_t) msg_size, ZLINK_DONTWAIT)
                                : send_payload_one_part (handles[i], payload, (size_t) msg_size,
                                                         ZLINK_DONTWAIT);
                if (send_rc < 0) {
                    ok = 0;
                    err = zlink_errno ();
                    break;
                }
                if (send_rc == 0) {
                    awaiting_reply[i] = 1;
                    send_pending[i] = 0;
                    ++seq;
                }
            }
            if (!(items[i].revents & ZLINK_POLLIN))
                continue;
            while (1) {
                int recv_rc = recv_echo_one_part (handles[i], router_mode, (uint32_t) run_id_value,
                                                  (size_t) msg_size, 2.0, &latencies, &received);
                if (recv_rc == 1)
                    break;
                if (recv_rc < 0) {
                    ok = 0;
                    err = errno ? errno : zlink_errno ();
                    break;
                }
                awaiting_reply[i] = 0;
                if (steady_ns () < deadline)
                    send_pending[i] = 1;
            }
            if (!ok)
                break;
        }
    }
    Py_END_ALLOW_THREADS

      free (handles);
    free (payloads);
    free (awaiting_reply);
    free (send_pending);
    free (items);
    if (!ok || received == 0) {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0, ok ? 0 : err);
    }
    qsort (latencies.values, latencies.count, sizeof (double), compare_double);
    double mean_ms = (latencies.sum / (double) latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth =
      ((double) received * (double) msg_size * 2.0) / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms, p95_ms, p99_ms, 0);
}



static void routed_echo_free_queue (routed_echo_queue_t *queue)
{
    routed_echo_item_t *item = NULL;
    routed_echo_item_t *next = NULL;

    if (!queue)
        return;
    item = queue->head;
    while (item) {
        next = item->next;
        zlink_msg_close (&item->msg);
        free (item);
        item = next;
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->pending_count = 0;
}

static int routed_echo_enqueue (routed_echo_queue_t *queue,
                                const zlink_routing_id_t *routing_id,
                                zlink_msg_t *msg)
{
    routed_echo_item_t *item = NULL;

    if (!queue || !routing_id || !msg)
        return -1;
    item = (routed_echo_item_t *) calloc (1, sizeof (*item));
    if (!item)
        return -1;
    item->routing_id = *routing_id;
    item->msg = *msg;
    if (queue->tail)
        queue->tail->next = item;
    else
        queue->head = item;
    queue->tail = item;
    queue->pending_count++;
    return 0;
}

static int
routed_echo_try_send (void *handle, const zlink_routing_id_t *routing_id, zlink_msg_t *msg)
{
    int rc = zlink_send_part_rid (handle, routing_id, msg, ZLINK_DONTWAIT, ZLINK_PART_FINAL);
    if (rc == ZLINK_SUBMIT_OK)
        return 0;
    {
        int err = zlink_errno ();
        if (err == 0)
            err = errno;
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT
            || err == ENOTCONN || err == EHOSTUNREACH || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static int
routed_echo_drain_pending (void *handle, routed_echo_queue_t *queue, unsigned long long *sent)
{
    while (queue->head) {
        routed_echo_item_t *item = queue->head;
        int send_rc = routed_echo_try_send (handle, &item->routing_id, &item->msg);
        if (send_rc > 0)
            return 1;
        if (send_rc < 0)
            return -1;
        queue->head = item->next;
        if (!queue->head)
            queue->tail = NULL;
        queue->pending_count--;
        ++(*sent);
        zlink_msg_close (&item->msg);
        free (item);
    }
    return 0;
}

static int routed_echo_recv_and_reply (void *handle,
                                       routed_echo_queue_t *queue,
                                       unsigned long long *received,
                                       unsigned long long *sent)
{
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    const zlink_routing_id_t *peer_rid = NULL;
    uint64_t request_seq = 0;
    int rc = ZLINK_RECV_OK;

    if (zlink_msg_init (&part) != 0)
        return -1;
    rc = zlink_router_recv_part (handle, &peer_rid, &request_seq, &part, &has_more,
                                 ZLINK_DONTWAIT);
    if (rc != ZLINK_RECV_OK) {
        int err = zlink_errno ();
        if (err == 0)
            err = errno;
        zlink_msg_close (&part);
        if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
            return 1;
        return -1;
    }
    if (!peer_rid || peer_rid->size == 0 || has_more != ZLINK_PART_FINAL) {
        zlink_msg_close (&part);
        errno = EINVAL;
        return -1;
    }
    (void) request_seq;
    ++(*received);
    if (!queue->head) {
        int send_rc = routed_echo_try_send (handle, peer_rid, &part);
        if (send_rc == 0) {
            ++(*sent);
            zlink_msg_close (&part);
            return 0;
        }
        if (send_rc < 0) {
            zlink_msg_close (&part);
            return -1;
        }
    }
    if (routed_echo_enqueue (queue, peer_rid, &part) != 0) {
        zlink_msg_close (&part);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static PyObject *py_router_echo_server_loop (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    Py_buffer stop_flag = {0};
    void *handle = NULL;
    routed_echo_queue_t queue = {0};
    zlink_pollitem_t item;
    unsigned long long received = 0;
    unsigned long long sent = 0;
    int ok = 1;
    int err = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Kw*", &handle_value, &stop_flag))
        return NULL;
    if (stop_flag.len < 1) {
        PyBuffer_Release (&stop_flag);
        PyErr_SetString (PyExc_ValueError, "stop flag must not be empty");
        return NULL;
    }
    handle = (void *) (uintptr_t) handle_value;

    Py_BEGIN_ALLOW_THREADS while (!*((volatile unsigned char *) stop_flag.buf))
    {
        int progressed = 0;
        int had_pending = queue.head != NULL;
        int drain_rc = routed_echo_drain_pending (handle, &queue, &sent);
        if (drain_rc < 0) {
            ok = 0;
            err = zlink_errno ();
            if (err == 0)
                err = errno;
            break;
        }
        if (had_pending && drain_rc == 0)
            progressed = 1;

        while (!*((volatile unsigned char *) stop_flag.buf)) {
            int recv_rc = routed_echo_recv_and_reply (handle, &queue, &received, &sent);
            if (recv_rc == 1)
                break;
            if (recv_rc < 0) {
                ok = 0;
                err = zlink_errno ();
                if (err == 0)
                    err = errno;
                break;
            }
            progressed = 1;
        }
        if (!ok)
            break;
        if (progressed)
            continue;

        item.socket = handle;
        item.fd = 0;
        item.events = ZLINK_POLLIN | (queue.head ? ZLINK_POLLOUT : 0);
        item.revents = 0;
        zlink_config_result_t poll_error = ZLINK_CONFIG_OK;
        int poll_rc = zlink_poll (&item, 1, 100, &poll_error);
        if (poll_rc < 0) {
            int poll_errno = zlink_errno ();
            if (poll_errno == 0)
                poll_errno = errno;
            if (poll_errno == EINTR || poll_errno == EAGAIN || poll_errno == EWOULDBLOCK)
                continue;
            ok = 0;
            err = poll_errno;
            if (err == 0 && poll_error != ZLINK_CONFIG_OK)
                err = EINVAL;
            break;
        }
    }
    Py_END_ALLOW_THREADS

      routed_echo_free_queue (&queue);
    PyBuffer_Release (&stop_flag);
    return Py_BuildValue ("KKKi", received, sent, queue.pending_count, ok ? 0 : err);
}

static PyObject *py_recv_count_active (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    unsigned long long received = 0;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = {0};

    (void) self;
    if (!PyArg_ParseTuple (args, "KniK", &handle_value, &msg_size, &duration_s, &run_id_value))
        return NULL;
    if (msg_size < 29 || duration_s <= 0) {
        PyErr_SetString (PyExc_ValueError, "invalid receive count arguments");
        return NULL;
    }
    void *handle = (void *) (uintptr_t) handle_value;
    const uint64_t deadline = steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS while (steady_ns () < deadline)
    {
        zlink_msg_t part;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const zlink_routing_id_t *source_rid = NULL;
        if (zlink_msg_init (&part) != 0) {
            ok = 0;
            err = zlink_errno ();
            break;
        }
        int rc = zlink_recv_part (handle, &source_rid, &part, &has_more, ZLINK_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            const int recv_err = zlink_errno ();
            zlink_msg_close (&part);
            if (recv_err == EAGAIN || recv_err == EWOULDBLOCK || recv_err == EINTR) {
                struct timespec ts = {0, 100000L};
                nanosleep (&ts, NULL);
                continue;
            }
            ok = 0;
            err = recv_err;
            break;
        }
        if (has_more == ZLINK_PART_FINAL) {
            uint64_t sent_ts = 0;
            const void *data = zlink_msg_data (&part);
            const size_t size = zlink_msg_size (&part);
            if (decode_single_payload (data, size, (uint32_t) run_id_value, &sent_ts)) {
                const uint64_t now = now_ns ();
                double sample = sent_ts > 0 && now >= sent_ts ? (double) (now - sent_ts) : 0.0;
                ++received;
                if (!latency_samples_add (&latencies, sample)) {
                    ok = 0;
                    err = ENOMEM;
                    zlink_msg_close (&part);
                    break;
                }
            }
        }
        zlink_msg_close (&part);
    }
    Py_END_ALLOW_THREADS

      if (!ok || received == 0)
    {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0, ok ? 0 : err);
    }
    qsort (latencies.values, latencies.count, sizeof (double), compare_double);
    double mean_ms = (latencies.sum / (double) latencies.count) / 1000000.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth = ((double) received * (double) msg_size) / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms, p95_ms, p99_ms, 0);
}

static int
publish_payload_one_part (void *handle, const char *topic, const void *data, size_t size, int flags)
{
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    if (size > 0)
        memcpy (zlink_msg_data (&part), data, size);
    if (zlink_publish_part (handle, topic, &part, (zlink_send_flags_t) flags, ZLINK_PART_FINAL)
        == ZLINK_SUBMIT_OK)
        return 0;
    {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT
            || err == ENOTCONN || err == EHOSTUNREACH || err == ENETUNREACH)
            return 1;
    }
    return -1;
}

static PyObject *py_publish_active (PyObject *self, PyObject *args)
{
    unsigned long long handle_value = 0;
    Py_buffer topic = {0};
    Py_buffer stop_token = {0};
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    unsigned long long run_id_value = 0;
    char topic_text[256];
    unsigned char *payload = NULL;
    unsigned long long sent = 0;
    int ok = 1;
    int err = 0;

    (void) self;
    if (!PyArg_ParseTuple (args, "Ky*y*niK", &handle_value, &topic, &stop_token, &msg_size,
                           &duration_s, &run_id_value))
        return NULL;
    if (topic.len <= 0 || topic.len >= (Py_ssize_t) sizeof (topic_text)
        || memchr (topic.buf, '\0', (size_t) topic.len) || msg_size < 29 || duration_s <= 0) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_SetString (PyExc_ValueError, "invalid publish arguments");
        return NULL;
    }
    memcpy (topic_text, topic.buf, (size_t) topic.len);
    topic_text[(size_t) topic.len] = '\0';
    payload = (unsigned char *) malloc ((size_t) msg_size);
    if (!payload) {
        PyBuffer_Release (&topic);
        PyBuffer_Release (&stop_token);
        PyErr_NoMemory ();
        return NULL;
    }
    memset (payload, 'p', (size_t) msg_size);
    void *handle = (void *) (uintptr_t) handle_value;
    const uint64_t deadline = steady_ns () + ((uint64_t) duration_s * 1000000000ULL);
    uint64_t seq = 1;

    Py_BEGIN_ALLOW_THREADS while (steady_ns () < deadline)
    {
        if (!stamp_single_payload (payload, (size_t) msg_size, (uint32_t) run_id_value, 1, seq)) {
            ok = 0;
            err = EINVAL;
            break;
        }
        int step =
          publish_payload_one_part (handle, topic_text, payload, (size_t) msg_size, ZLINK_DONTWAIT);
        if (step < 0) {
            ok = 0;
            err = zlink_errno ();
            break;
        }
        if (step > 0) {
            struct timespec ts = {0, 100000L};
            nanosleep (&ts, NULL);
            continue;
        }
        ++sent;
        ++seq;
    }
    if (ok && stop_token.len > 0) {
        for (int retry = 0; retry < 1000; ++retry) {
            int step = publish_payload_one_part (handle, topic_text, stop_token.buf,
                                                 (size_t) stop_token.len, ZLINK_SEND_FLAGS_NONE);
            if (step == 0)
                break;
            if (step < 0 || retry == 999) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            struct timespec ts = {0, 100000L};
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

      free (payload);
    PyBuffer_Release (&topic);
    PyBuffer_Release (&stop_token);
    return Py_BuildValue ("Ki", sent, ok ? 0 : err);
}

static PyObject *py_subscribe_count_active (PyObject *self, PyObject *args)
{
    PyObject *handles_obj = NULL;
    PyObject *handles_seq = NULL;
    Py_buffer expected_topic = {0};
    void **handles = NULL;
    Py_ssize_t handle_count = 0;
    Py_ssize_t msg_size = 0;
    int duration_s = 0;
    int sample_stride = 32;
    unsigned long long run_id_value = 0;
    unsigned long long received = 0;
    int ok = 1;
    int err = 0;
    latency_samples_t latencies = {0};

    (void) self;
    if (!PyArg_ParseTuple (args, "Oy*niKi", &handles_obj, &expected_topic, &msg_size, &duration_s,
                           &run_id_value, &sample_stride))
        return NULL;
    if (expected_topic.len <= 0 || expected_topic.len > 255 || msg_size < 29 || duration_s <= 0) {
        PyBuffer_Release (&expected_topic);
        PyErr_SetString (PyExc_ValueError, "invalid subscribe count arguments");
        return NULL;
    }
    handles_seq = PySequence_Fast (handles_obj, "handles must be a sequence");
    if (!handles_seq) {
        PyBuffer_Release (&expected_topic);
        return NULL;
    }
    handle_count = PySequence_Fast_GET_SIZE (handles_seq);
    if (handle_count <= 0) {
        Py_DECREF (handles_seq);
        PyBuffer_Release (&expected_topic);
        PyErr_SetString (PyExc_ValueError, "handles must not be empty");
        return NULL;
    }
    handles = (void **) calloc ((size_t) handle_count, sizeof (void *));
    if (!handles) {
        Py_DECREF (handles_seq);
        PyBuffer_Release (&expected_topic);
        PyErr_NoMemory ();
        return NULL;
    }
    for (Py_ssize_t i = 0; i < handle_count; ++i) {
        PyObject *item = PySequence_Fast_GET_ITEM (handles_seq, i);
        unsigned long long value = PyLong_AsUnsignedLongLong (item);
        if (PyErr_Occurred ()) {
            free (handles);
            Py_DECREF (handles_seq);
            PyBuffer_Release (&expected_topic);
            return NULL;
        }
        handles[i] = (void *) (uintptr_t) value;
    }
    if (sample_stride <= 0)
        sample_stride = 32;
    const uint64_t deadline = steady_ns () + ((uint64_t) duration_s * 1000000000ULL);

    Py_BEGIN_ALLOW_THREADS while (steady_ns () < deadline)
    {
        int progressed = 0;
        for (Py_ssize_t i = 0; i < handle_count; ++i) {
            zlink_msg_t part;
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            const zlink_routing_id_t *source_rid = NULL;
            char topic[256];
            size_t topic_len = 0;
            if (steady_ns () >= deadline)
                break;
            if (zlink_msg_init (&part) != 0) {
                ok = 0;
                err = zlink_errno ();
                break;
            }
            int rc = zlink_subscribe_part (handles[i], &source_rid, topic, sizeof (topic),
                                           &topic_len, &part, &has_more, ZLINK_DONTWAIT);
            if (rc != ZLINK_RECV_OK) {
                const int recv_err = zlink_errno ();
                zlink_msg_close (&part);
                if (recv_err == EAGAIN || recv_err == EWOULDBLOCK || recv_err == EINTR)
                    continue;
                ok = 0;
                err = recv_err;
                break;
            }
            progressed = 1;
            if (has_more == ZLINK_PART_FINAL && topic_len == (size_t) expected_topic.len
                && memcmp (topic, expected_topic.buf, topic_len) == 0) {
                uint64_t sent_ts = 0;
                const void *data = zlink_msg_data (&part);
                const size_t size = zlink_msg_size (&part);
                if (decode_single_payload (data, size, (uint32_t) run_id_value, &sent_ts)) {
                    ++received;
                    if (received == 1 || received % (unsigned long long) sample_stride == 0) {
                        const uint64_t now = now_ns ();
                        double sample =
                          sent_ts > 0 && now >= sent_ts ? (double) (now - sent_ts) : 0.0;
                        if (!latency_samples_add (&latencies, sample)) {
                            ok = 0;
                            err = ENOMEM;
                            zlink_msg_close (&part);
                            break;
                        }
                    }
                }
            }
            zlink_msg_close (&part);
        }
        if (!ok)
            break;
        if (!progressed) {
            struct timespec ts = {0, 100000L};
            nanosleep (&ts, NULL);
        }
    }
    Py_END_ALLOW_THREADS

      free (handles);
    Py_DECREF (handles_seq);
    PyBuffer_Release (&expected_topic);
    if (!ok || received == 0) {
        free (latencies.values);
        return Py_BuildValue ("Kdddddi", received, 0.0, 0.0, 0.0, 0.0, 0.0, ok ? 0 : err);
    }
    if (latencies.count > 0)
        qsort (latencies.values, latencies.count, sizeof (double), compare_double);
    double mean_ms =
      latencies.count > 0 ? (latencies.sum / (double) latencies.count) / 1000000.0 : 0.0;
    double p95_ms = latency_percentile_sorted (&latencies, 0.95) / 1000000.0;
    double p99_ms = latency_percentile_sorted (&latencies, 0.99) / 1000000.0;
    double throughput = (double) received / (double) duration_s;
    double bandwidth = ((double) received * (double) msg_size) / (double) duration_s / 1000000.0;
    free (latencies.values);
    return Py_BuildValue ("Kdddddi", received, throughput, bandwidth, mean_ms, p95_ms, p99_ms, 0);
}



static PyMethodDef zlink_perf_native_methods[] = {
  {"single_socket_one_way", py_single_socket_one_way, METH_VARARGS,
   "Run a native single one-way socket benchmark active phase."},
  {"multi_send_one_way", py_multi_send_one_way, METH_VARARGS,
   "Run a native multi one-way send active phase."},
  {"multi_echo_roundtrip", py_multi_echo_roundtrip, METH_VARARGS,
   "Run a native multi routed echo active phase."},
  {"router_echo_server_loop", py_router_echo_server_loop, METH_VARARGS,
   "Run a native routed echo server loop until a stop flag is set."},
  {"recv_count_active", py_recv_count_active, METH_VARARGS,
   "Count active one-part received messages in a native loop."},
  {"publish_active", py_publish_active, METH_VARARGS,
   "Publish topic payloads in a native active phase."},
  {"subscribe_count_active", py_subscribe_count_active, METH_VARARGS,
   "Count active topic messages across subscribers in a native loop."},
  {"perf_stamp_payload", py_perf_stamp_payload, METH_VARARGS, "Stamp a perf payload header."},
  {"perf_active_latency_ns", py_perf_active_latency_ns, METH_VARARGS,
   "Classify a perf payload and return active latency ns."},
  {NULL, NULL, 0, NULL},
};

static struct PyModuleDef zlink_perf_native_module = {
  PyModuleDef_HEAD_INIT, "_zlink_perf_native",
  "Private native bridge for zlink Python performance harnesses.", -1,
  zlink_perf_native_methods,
};

PyMODINIT_FUNC PyInit__zlink_perf_native (void)
{
    return PyModule_Create (&zlink_perf_native_module);
}
