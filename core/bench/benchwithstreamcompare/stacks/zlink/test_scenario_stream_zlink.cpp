#include "../common/stream_echo_common.hpp"

#include "../../../../include/zlink.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;
static const size_t k_frame_prefix_size = 4;
static const int k_recv_batch_size = 2048;

struct stream_buffer_t
{
    std::vector<unsigned char> data;
    size_t offset;

    stream_buffer_t () : offset (0) {}

    size_t available () const { return data.size () - offset; }

    void append (const unsigned char *buf, size_t len)
    {
        if (!buf || len == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
        }

        const size_t old_size = data.size ();
        const size_t new_size = old_size + len;
        if (data.capacity () < new_size) {
            size_t next_cap = data.capacity () > 0 ? data.capacity () : 1024;
            while (next_cap < new_size)
                next_cap *= 2;
            data.reserve (next_cap);
        }
        data.resize (new_size);
        std::memcpy (&data[old_size], buf, len);
    }

    void compact ()
    {
        if (offset == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
            return;
        }
        if (offset > 4096 || offset * 2 >= data.size ()) {
            const size_t remain = data.size () - offset;
            std::memmove (&data[0], &data[offset], remain);
            data.resize (remain);
            offset = 0;
        }
    }

    void reset ()
    {
        data.clear ();
        offset = 0;
    }
};

typedef std::vector<stream_buffer_t> stream_stash_vec_t;

bool parse_routing_id_u32 (const unsigned char *rid_data_,
                           size_t rid_size_,
                           uint32_t *rid_value_out_)
{
    if (!rid_data_ || !rid_value_out_ || rid_size_ != 4)
        return false;

    *rid_value_out_ = stream_echo::load_u32_be (rid_data_);
    return *rid_value_out_ != 0;
}

struct server_options_t
{
    std::string host;
    int port;
    size_t size;
    int sndbuf;
    int rcvbuf;
    int backlog;
    int tcp_nodelay;
    int io_threads;

    server_options_t () :
        host ("0.0.0.0"),
        port (38001),
        size (1024),
        sndbuf (1024 * 1024),
        rcvbuf (1024 * 1024),
        backlog (32768),
        tcp_nodelay (1),
        io_threads (4)
    {
    }
};

static std::atomic<bool> *g_stop_flag = NULL;

void on_signal (int)
{
    if (g_stop_flag)
        g_stop_flag->store (true, std::memory_order_release);
}

std::string make_endpoint (const std::string &host, int port)
{
    char buf[256];
    std::snprintf (buf, sizeof (buf), "tcp://%s:%d", host.c_str (), port);
    return std::string (buf);
}

void apply_socket_tuning (void *socket, const server_options_t &opt)
{
    (void)zlink_setsockopt (socket, ZLINK_SNDBUF, &opt.sndbuf, sizeof (opt.sndbuf));
    (void)zlink_setsockopt (socket, ZLINK_RCVBUF, &opt.rcvbuf, sizeof (opt.rcvbuf));
    (void)zlink_setsockopt (socket, ZLINK_BACKLOG, &opt.backlog, sizeof (opt.backlog));
    (void)zlink_setsockopt (socket, ZLINK_TCP_NODELAY, &opt.tcp_nodelay,
                            sizeof (opt.tcp_nodelay));

    const int zero = 0;
    (void)zlink_setsockopt (socket, ZLINK_RCVHWM, &zero, sizeof (zero));
    (void)zlink_setsockopt (socket, ZLINK_SNDHWM, &zero, sizeof (zero));
}

class zlink_stream_echo_server_t
{
  public:
    explicit zlink_stream_echo_server_t (const server_options_t &opt_) :
        opt (opt_),
        ctx (NULL),
        server (NULL),
        recv_msgs (0),
        parse_error (0),
        protocol_error (0),
        send_error (0),
        stop (false)
    {
    }

    ~zlink_stream_echo_server_t () { cleanup (); }

    int run ()
    {
        ctx = zlink_ctx_new ();
        if (!ctx) {
            std::fprintf (stderr, "zlink stream: zlink_ctx_new failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        (void)zlink_ctx_set (ctx, ZLINK_IO_THREADS, std::max (1, opt.io_threads));

        server = zlink_socket (ctx, ZLINK_STREAM);
        if (!server) {
            std::fprintf (stderr, "zlink stream: zlink_socket failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        apply_socket_tuning (server, opt);

        const std::string endpoint = make_endpoint (opt.host, opt.port);
        if (zlink_bind (server, endpoint.c_str ()) != 0) {
            std::fprintf (
              stderr, "zlink stream: bind failed: %s endpoint=%s\n",
              zlink_strerror (zlink_errno ()), endpoint.c_str ());
            return 2;
        }

        std::signal (SIGINT, on_signal);
        std::signal (SIGTERM, on_signal);
        g_stop_flag = &stop;

        while (!stop.load (std::memory_order_acquire)) {
            zlink_pollitem_t items[] = {{server, 0, ZLINK_POLLIN, 0}};
            const int prc = zlink_poll (items, 1, 1000);
            if (prc < 0) {
                const int err = zlink_errno ();
                if (err == EINTR || err == ETERM)
                    break;
                std::fprintf (stderr, "zlink stream: poll failed: %s\n",
                              zlink_strerror (err));
                break;
            }
            if (prc == 0)
                continue;

            if ((items[0].revents & ZLINK_POLLIN) != 0) {
                for (;;) {
                    const int rc = receive_and_process_batch (k_recv_batch_size);
                    if (rc < k_recv_batch_size)
                        break;
                }
            }
        }

        std::printf ("%s\n",
                     stream_echo::make_metric_line ("zlink", opt.size, recv_msgs,
                                                    parse_error, protocol_error,
                                                    send_error, 0)
                       .c_str ());

        return 0;
    }

  private:
    bool send_copy_reply (const unsigned char *rid_data_,
                          size_t rid_size_,
                          const unsigned char *payload_data_,
                          size_t payload_size_)
    {
        if (!rid_data_ || rid_size_ == 0 || !payload_data_ || payload_size_ == 0)
            return false;
        const int sent_rid =
          zlink_send (server, rid_data_, rid_size_, ZLINK_SNDMORE);
        if (sent_rid != static_cast<int> (rid_size_))
            return false;
        const int sent_payload =
          zlink_send (server, payload_data_, payload_size_, 0);
        return sent_payload == static_cast<int> (payload_size_);
    }

    bool valid_body_size (size_t body_size) const
    {
        return body_size >= k_min_payload_size && body_size <= k_max_payload_size;
    }

    void record_frame_metrics (size_t body_size)
    {
        if (body_size > opt.size)
            ++protocol_error;
        else
            ++recv_msgs;
    }

    void record_invalid_frame (stream_buffer_t &stash)
    {
        stash.reset ();
        ++parse_error;
        ++protocol_error;
    }

    stream_buffer_t *find_stash (size_t routing_idx)
    {
        if (routing_idx >= _frame_stashes.size ())
            return NULL;
        return &_frame_stashes[routing_idx];
    }

    stream_buffer_t &ensure_stash (size_t routing_idx)
    {
        if (routing_idx >= _frame_stashes.size ())
            _frame_stashes.resize (routing_idx + 1);
        return _frame_stashes[routing_idx];
    }

    void emit_complete_frames_from_stash (stream_buffer_t &stash,
                                          const unsigned char *rid_data,
                                          size_t rid_size,
                                          bool &invalid_frame)
    {
        size_t consumed = 0;
        const size_t available = stash.available ();
        while (consumed + k_frame_prefix_size <= available) {
            const unsigned char *frame = &stash.data[stash.offset + consumed];
            const size_t body_size = static_cast<size_t> (
              stream_echo::load_u32_be (frame));
            if (!valid_body_size (body_size)) {
                invalid_frame = true;
                break;
            }

            const size_t frame_size = k_frame_prefix_size + body_size;
            if (consumed + frame_size > available)
                break;

            record_frame_metrics (body_size);
            if (!send_copy_reply (rid_data, rid_size, frame, frame_size))
                ++send_error;

            consumed += frame_size;
        }

        if (consumed > 0) {
            stash.offset += consumed;
            stash.compact ();
        }
    }

    // Raw STREAM mode can split one frame across many chunks. This routine
    // keeps per-RID reassembly state and emits only complete frames.
    int receive_and_process_batch (int max_batch)
    {
        int processed = 0;
        if (max_batch <= 0)
            return 0;

        for (; processed < max_batch; ++processed) {
            zlink_msg_t rid_msg;
            zlink_msg_t payload_msg;

            if (zlink_msg_init (&rid_msg) != 0)
                return -1;

            int rc = zlink_msg_recv (&rid_msg, server, ZLINK_DONTWAIT);
            if (rc < 0) {
                zlink_msg_close (&rid_msg);
                const int err = zlink_errno ();
                if (err == EAGAIN)
                    break;
                if (err != EINTR && err != ETERM)
                    ++parse_error;
                return processed > 0 ? processed : -1;
            }

            if (zlink_msg_init (&payload_msg) != 0) {
                zlink_msg_close (&rid_msg);
                return processed > 0 ? processed : -1;
            }

            if (!zlink_msg_more (&rid_msg)) {
                ++parse_error;
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            rc = zlink_msg_recv (&payload_msg, server, ZLINK_DONTWAIT);
            if (rc < 0) {
                const int err = zlink_errno ();
                if (err != EAGAIN && err != EINTR && err != ETERM)
                    ++parse_error;
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                return processed > 0 ? processed : -1;
            }

            const unsigned char *rid_data =
              static_cast<const unsigned char *> (zlink_msg_data (&rid_msg));
            const size_t rid_size = zlink_msg_size (&rid_msg);
            const unsigned char *payload = static_cast<const unsigned char *> (
              zlink_msg_data (&payload_msg));
            const size_t payload_size = zlink_msg_size (&payload_msg);

            uint32_t routing_id_value = 0;
            if (!parse_routing_id_u32 (rid_data, rid_size, &routing_id_value)) {
                ++parse_error;
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            const size_t routing_idx = static_cast<size_t> (routing_id_value);
            stream_buffer_t *stash_ptr = find_stash (routing_idx);

            if (payload_size == 0) {
                if (stash_ptr)
                    stash_ptr->reset ();
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            bool invalid_frame = false;
            stream_buffer_t &stash = ensure_stash (routing_idx);
            if (payload && payload_size > 0)
                stash.append (payload, payload_size);

            emit_complete_frames_from_stash (stash, rid_data, rid_size,
                                             invalid_frame);
            if (invalid_frame)
                record_invalid_frame (stash);

            zlink_msg_close (&payload_msg);
            zlink_msg_close (&rid_msg);
        }

        return processed;
    }

    void cleanup ()
    {
        _frame_stashes.clear ();

        if (server) {
            zlink_close (server);
            server = NULL;
        }

        if (ctx) {
            zlink_ctx_term (ctx);
            ctx = NULL;
        }
    }

    server_options_t opt;
    void *ctx;
    void *server;

    long recv_msgs;
    long parse_error;
    long protocol_error;
    long send_error;
    stream_stash_vec_t _frame_stashes;

    std::atomic<bool> stop;
};

bool parse_options (int argc, char **argv, server_options_t &opt)
{
    const stream_echo::arg_reader_t args (argc, argv);

    opt.host = args.get_string ("--host", opt.host.c_str ());
    opt.port = args.get_int ("--port", opt.port, 1);
    opt.size = args.get_size ("--size", opt.size, k_min_payload_size);
    opt.sndbuf = args.get_int ("--sndbuf", opt.sndbuf, 1);
    opt.rcvbuf = args.get_int ("--rcvbuf", opt.rcvbuf, 1);
    opt.backlog = args.get_int ("--backlog", opt.backlog, 1);
    opt.tcp_nodelay = args.get_int ("--tcp-nodelay", opt.tcp_nodelay, 0);
    opt.io_threads = args.get_int ("--io-threads", opt.io_threads, 1);

    if (opt.size > k_max_payload_size) {
        std::fprintf (stderr, "zlink stream: size too large %zu\n", opt.size);
        return false;
    }

    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("test_scenario_stream_zlink: no args -> skip\n");
        return 0;
    }

    server_options_t opt;
    if (!parse_options (argc, argv, opt))
        return 2;

    zlink_stream_echo_server_t server (opt);
    return server.run ();
}
