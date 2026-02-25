#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using boost::asio::ip::tcp;

static const int k_connect_batch = 1024;
static const int k_connect_timeout_s = 90;
static const int k_connect_retry_delay_ms = 25;
static const int k_resize_timeout_s = 30;
static const int k_socket_sndbuf = 1024 * 1024;
static const int k_socket_rcvbuf = 1024 * 1024;
static const int k_socket_tcp_nodelay = 1;
static const size_t k_rtt_sample_capacity = 1024 * 1024;
static const size_t k_min_chunk_size = 16;
static const size_t k_max_chunk_size = 4 * 1024 * 1024;
static const size_t k_loopback_port_headroom = 64;

enum phase_mode_t
{
    phase_idle = 0,
    phase_warmup = 1,
    phase_measure = 2,
};

uint64_t now_ns ()
{
    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now ();
    return static_cast<uint64_t> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        now.time_since_epoch ())
        .count ());
}

uint32_t load_u32_be (const unsigned char *p)
{
    return (static_cast<uint32_t> (p[0]) << 24)
           | (static_cast<uint32_t> (p[1]) << 16)
           | (static_cast<uint32_t> (p[2]) << 8)
           | static_cast<uint32_t> (p[3]);
}

void store_u32_be (unsigned char *p, uint32_t v)
{
    p[0] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    p[1] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    p[2] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    p[3] = static_cast<unsigned char> (v & 0xFF);
}

uint64_t load_u64_be (const unsigned char *p)
{
    return (static_cast<uint64_t> (p[0]) << 56)
           | (static_cast<uint64_t> (p[1]) << 48)
           | (static_cast<uint64_t> (p[2]) << 40)
           | (static_cast<uint64_t> (p[3]) << 32)
           | (static_cast<uint64_t> (p[4]) << 24)
           | (static_cast<uint64_t> (p[5]) << 16)
           | (static_cast<uint64_t> (p[6]) << 8)
           | static_cast<uint64_t> (p[7]);
}

void store_u64_be (unsigned char *p, uint64_t v)
{
    p[0] = static_cast<unsigned char> ((v >> 56) & 0xFF);
    p[1] = static_cast<unsigned char> ((v >> 48) & 0xFF);
    p[2] = static_cast<unsigned char> ((v >> 40) & 0xFF);
    p[3] = static_cast<unsigned char> ((v >> 32) & 0xFF);
    p[4] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    p[5] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    p[6] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    p[7] = static_cast<unsigned char> (v & 0xFF);
}

double percentile_from_sorted (const std::vector<double> &samples, double q)
{
    if (samples.empty ())
        return 0.0;

    if (q < 0.0)
        q = 0.0;
    if (q > 1.0)
        q = 1.0;

    const size_t last = samples.size () - 1;
    const double idx = q * static_cast<double> (last);
    const size_t lo = static_cast<size_t> (idx);
    const size_t hi = std::min<size_t> (last, lo + 1);
    const double frac = idx - static_cast<double> (lo);
    if (lo == hi)
        return samples[lo];
    return samples[lo] + (samples[hi] - samples[lo]) * frac;
}

class arg_reader_t
{
  public:
    arg_reader_t (int argc_, char **argv_) : argc (argc_), argv (argv_) {}

    std::string get_string (const char *key, const char *fallback) const
    {
        if (!key)
            return fallback ? std::string (fallback) : std::string ();

        for (int i = 1; i + 1 < argc; ++i) {
            if (std::strcmp (argv[i], key) == 0)
                return std::string (argv[i + 1]);
        }
        return fallback ? std::string (fallback) : std::string ();
    }

    int get_int (const char *key, int fallback, int min_value) const
    {
        const std::string v = get_string (key, "");
        if (v.empty ())
            return fallback;

        char *end = NULL;
        const long parsed = std::strtol (v.c_str (), &end, 10);
        if (end == v.c_str ())
            return fallback;
        if (parsed < static_cast<long> (min_value))
            return min_value;
        if (parsed > static_cast<long> (std::numeric_limits<int>::max ()))
            return std::numeric_limits<int>::max ();
        return static_cast<int> (parsed);
    }

  private:
    int argc;
    char **argv;
};

bool parse_size_list (const std::string &text, std::vector<size_t> &out)
{
    out.clear ();
    std::stringstream ss (text);
    std::string item;
    while (std::getline (ss, item, ',')) {
        if (item.empty ())
            continue;
        char *end = NULL;
        const unsigned long parsed = std::strtoul (item.c_str (), &end, 10);
        if (end == item.c_str ())
            return false;
        const size_t size = static_cast<size_t> (parsed);
        if (size < k_min_chunk_size || size > k_max_chunk_size)
            return false;
        out.push_back (size);
    }
    return !out.empty ();
}

std::string lower_copy (const std::string &text)
{
    std::string out = text;
    std::transform (out.begin (), out.end (), out.begin (),
                    [](unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return out;
}

bool parse_endpoint (const std::string &endpoint,
                     std::string *transport_out,
                     std::string *host_out,
                     int *port_out)
{
    const size_t scheme_pos = endpoint.find ("://");
    if (scheme_pos == std::string::npos)
        return false;

    const std::string scheme = lower_copy (endpoint.substr (0, scheme_pos));
    const std::string rest = endpoint.substr (scheme_pos + 3);
    if (rest.empty ())
        return false;

    std::string host;
    std::string port_text;
    if (!rest.empty () && rest[0] == '[') {
        const size_t close = rest.find (']');
        if (close == std::string::npos || close + 2 > rest.size () || rest[close + 1] != ':')
            return false;
        host = rest.substr (1, close - 1);
        port_text = rest.substr (close + 2);
    } else {
        const size_t colon = rest.rfind (':');
        if (colon == std::string::npos)
            return false;
        host = rest.substr (0, colon);
        port_text = rest.substr (colon + 1);
    }
    if (host.empty () || port_text.empty ())
        return false;

    char *end = NULL;
    const long port = std::strtol (port_text.c_str (), &end, 10);
    if (!end || *end != '\0' || port <= 0 || port > 65535)
        return false;

    if (transport_out)
        *transport_out = scheme;
    if (host_out)
        *host_out = host;
    if (port_out)
        *port_out = static_cast<int> (port);
    return true;
}

struct client_options_t
{
    std::string transport;
    std::string pattern;
    std::string host;
    int port;
    int ccu;
    std::vector<size_t> sizes;
    int runs;
    int warmup;
    int duration;
    int drain_ms;
    int size_transition_drain_ms;
    int inflight;
    int io_threads;
    int latency_sample_rate;
    int warmup_count;
    int lat_count;
    int msg_count;
    int print_perf_result;
    std::string stop_token;
    int send_stop_token;

    client_options_t ()
        : transport ("tcp"),
          pattern ("STREAM"),
          host ("127.0.0.1"),
          port (38001),
          ccu (10000),
          sizes (),
          runs (1),
          warmup (2),
          duration (10),
          drain_ms (300),
          size_transition_drain_ms (300),
          inflight (10),
          io_threads (4),
          latency_sample_rate (0),
          warmup_count (0),
          lat_count (0),
          msg_count (0),
          print_perf_result (0),
          stop_token ("__zlink_perf_stop__"),
          send_stop_token (0)
    {
        sizes.push_back (64);
        sizes.push_back (1024);
        sizes.push_back (65536);
    }
};

struct loopback_bind_plan_t
{
    size_t port_capacity;
    std::vector<boost::asio::ip::address_v4> source_addrs;

    loopback_bind_plan_t () : port_capacity (0), source_addrs () {}
};

size_t read_ipv4_ephemeral_port_capacity ()
{
    std::ifstream in ("/proc/sys/net/ipv4/ip_local_port_range");
    if (!in.is_open ())
        return 0;

    int low = 0;
    int high = 0;
    in >> low >> high;
    if (!in.good () && !in.eof ())
        return 0;
    if (low <= 0 || high <= 0 || high < low)
        return 0;
    return static_cast<size_t> (high - low + 1);
}

boost::asio::ip::address_v4 make_loopback_shard_addr (size_t idx)
{
    const size_t block = idx / 254;
    const size_t tail = (idx % 254) + 1;
    boost::asio::ip::address_v4::bytes_type bytes;
    bytes[0] = 127;
    bytes[1] = 0;
    bytes[2] = static_cast<unsigned char> (block & 0xFF);
    bytes[3] = static_cast<unsigned char> (tail);
    return boost::asio::ip::address_v4 (bytes);
}

loopback_bind_plan_t make_loopback_bind_plan (const tcp::endpoint &endpoint,
                                              int ccu)
{
    loopback_bind_plan_t plan;
    if (ccu <= 0)
        return plan;
    if (!endpoint.address ().is_v4 ())
        return plan;
    if (!endpoint.address ().to_v4 ().is_loopback ())
        return plan;

    plan.port_capacity = read_ipv4_ephemeral_port_capacity ();
    if (plan.port_capacity == 0)
        return plan;

    const size_t usable_ports =
      plan.port_capacity > k_loopback_port_headroom
        ? plan.port_capacity - k_loopback_port_headroom
        : plan.port_capacity;
    if (usable_ports == 0)
        return plan;

    const size_t required =
      static_cast<size_t> (ccu + static_cast<int> (usable_ports) - 1)
      / usable_ports;
    if (required <= 1)
        return plan;

    plan.source_addrs.reserve (required);
    for (size_t i = 0; i < required; ++i)
        plan.source_addrs.push_back (make_loopback_shard_addr (i));
    return plan;
}

struct case_metrics_t
{
    long connect_ok;
    long connect_fail;
    double throughput_bps;
    double throughput_mib_s;
    double p50_us;
    double p95_us;
    double p99_us;
    long send_error;
    long recv_error;
    long timeout_error;
    long size_mismatch;
    bool pass;

    case_metrics_t ()
        : connect_ok (0),
          connect_fail (0),
          throughput_bps (0.0),
          throughput_mib_s (0.0),
          p50_us (0.0),
          p95_us (0.0),
          p99_us (0.0),
          send_error (0),
          recv_error (0),
          timeout_error (0),
          size_mismatch (0),
          pass (false)
    {
    }
};

struct resize_latch_t
{
    explicit resize_latch_t (size_t pending_) : pending (pending_) {}

    std::mutex mu;
    std::condition_variable cv;
    size_t pending;
};

class bench_client_t;

class client_session_t : public std::enable_shared_from_this<client_session_t>
{
  public:
    client_session_t (bench_client_t &owner_,
                      boost::asio::io_context &io_,
                      const tcp::endpoint &source_bind_ep_);

    void begin_connect (const tcp::endpoint &endpoint_);
    void set_chunk_size (size_t size,
                         const std::shared_ptr<resize_latch_t> &latch);
    void start_traffic ();
    void request_close ();

    bool connected () const;

  private:
    void do_connect ();
    void on_connect (const boost::system::error_code &ec);
    void maybe_send_more ();
    void send_one ();
    void on_write (const boost::system::error_code &ec, size_t bytes);
    void start_read_header ();
    void start_read_payload ();
    void on_read_header (const boost::system::error_code &ec, size_t bytes);
    void on_read_payload (const boost::system::error_code &ec, size_t bytes);
    void close_internal ();
    void apply_socket_tuning ();

    bench_client_t &owner;
    tcp::socket socket;
    boost::asio::steady_timer retry_timer;
    boost::asio::strand<boost::asio::io_context::executor_type> strand;

    bool closed;
    bool connected_flag;
    bool connect_started;
    bool connect_reported;
    bool write_inflight;
    bool read_inflight;
    size_t outstanding;

    size_t chunk_size;
    std::chrono::steady_clock::time_point connect_deadline;
    tcp::endpoint endpoint;
    tcp::endpoint source_bind_ep;

    std::vector<unsigned char> write_buf;
    unsigned char read_header[4];
    uint32_t read_declared;
    std::vector<unsigned char> read_buf;
};

class bench_client_t
{
  public:
    explicit bench_client_t (const client_options_t &opt_)
        : opt (opt_),
          io (),
          work_guard (boost::asio::make_work_guard (io)),
          next_connect_idx (0),
          connect_inflight (0),
          connect_success (0),
          connect_fail (0),
          connect_completed (0),
          mode (phase_idle),
          phase_end_ns (0),
          phase_size (opt.sizes.empty () ? 64 : opt.sizes[0]),
          outstanding_total (0),
          seq_gen (0),
          bytes_recv_measure (0),
          send_error_measure (0),
          recv_error_measure (0),
          timeout_error_measure (0),
          size_mismatch_measure (0),
          collect_metrics (false),
          sample_overwrite_idx (0),
          endpoint (boost::asio::ip::make_address (opt.host),
                    static_cast<unsigned short> (opt.port))
    {
        rtt_samples_us.assign (k_rtt_sample_capacity, 0.0);
        loopback_bind_plan = make_loopback_bind_plan (endpoint, opt.ccu);
    }

    int run ()
    {
        const int worker_count = std::max (1, opt.io_threads);
        for (int i = 0; i < worker_count; ++i)
            workers.push_back (std::thread ([this] () { io.run (); }));

        sessions.reserve (static_cast<size_t> (std::max (1, opt.ccu)));
        for (int i = 0; i < opt.ccu; ++i) {
            sessions.push_back (std::make_shared<client_session_t> (
              *this, io, source_bind_endpoint_for (static_cast<size_t> (i))));
        }

        schedule_connects ();

        const auto connect_deadline = std::chrono::steady_clock::now ()
                                      + std::chrono::seconds (
                                        k_connect_timeout_s);
        {
            std::unique_lock<std::mutex> lk (connect_mu);
            while (connect_completed.load (std::memory_order_acquire)
                     < static_cast<long> (sessions.size ())) {
                if (connect_cv.wait_until (lk, connect_deadline)
                    == std::cv_status::timeout)
                    break;
            }
        }

        const long completed = connect_completed.load (std::memory_order_relaxed);
        if (completed < static_cast<long> (sessions.size ())) {
            const long unresolved =
              static_cast<long> (sessions.size ()) - completed;
            connect_fail.fetch_add (unresolved, std::memory_order_relaxed);
            connect_completed.store (static_cast<long> (sessions.size ()),
                                     std::memory_order_release);
        }

        if (connect_success.load (std::memory_order_relaxed) <= 0) {
            shutdown_all_sessions ();
            join_workers ();
            return 2;
        }

        bool all_pass = true;
        for (int run_idx = 1; run_idx <= std::max (1, opt.runs); ++run_idx) {
            for (size_t i = 0; i < opt.sizes.size (); ++i) {
                const size_t size = opt.sizes[i];
                case_metrics_t m = run_case (size);
                const char *pass_text = m.pass ? "PASS" : "FAIL";
                if (opt.print_perf_result <= 1) {
                    std::printf (
                      "RESULT size=%zu run=%d throughput_bps=%.2f throughput_mib_s=%.2f "
                      "p50_us=%.2f p95_us=%.2f p99_us=%.2f connect_ok=%ld "
                      "connect_fail=%ld send_err=%ld recv_err=%ld timeout=%ld "
                      "size_mismatch=%ld "
                      "pass_fail=%s\n",
                      size, run_idx, m.throughput_bps, m.throughput_mib_s, m.p50_us,
                      m.p95_us, m.p99_us, m.connect_ok, m.connect_fail, m.send_error,
                      m.recv_error, m.timeout_error, m.size_mismatch, pass_text);
                }
                if (opt.print_perf_result > 0 && m.pass) {
                    const double throughput =
                      size > 0
                        ? (m.throughput_bps / static_cast<double> (size))
                        : 0.0;
                    const double bandwidth = m.throughput_bps / 1000000.0;
                    const double latency = m.p50_us * 0.5;
                    std::printf ("RESULT,current,%s,%s,%zu,throughput,%.6f\n",
                                 opt.pattern.c_str (), opt.transport.c_str (),
                                 size, throughput);
                    std::printf ("RESULT,current,%s,%s,%zu,bandwidth,%.6f\n",
                                 opt.pattern.c_str (), opt.transport.c_str (),
                                 size, bandwidth);
                    std::printf ("RESULT,current,%s,%s,%zu,latency,%.6f\n",
                                 opt.pattern.c_str (), opt.transport.c_str (),
                                 size, latency);
                }
                if (!m.pass)
                    all_pass = false;
                if ((i + 1) < opt.sizes.size ())
                    run_size_transition_drain ();
            }
        }

        shutdown_all_sessions ();
        join_workers ();
        return all_pass ? 0 : 2;
    }

    void schedule_connects ()
    {
        std::lock_guard<std::mutex> lk (connect_sched_mu);
        while (connect_inflight.load (std::memory_order_relaxed) < k_connect_batch) {
            const size_t idx =
              next_connect_idx.fetch_add (1, std::memory_order_relaxed);
            if (idx >= sessions.size ())
                break;
            connect_inflight.fetch_add (1, std::memory_order_relaxed);
            sessions[idx]->begin_connect (endpoint);
        }
    }

    void on_connect_result (bool success,
                            const std::shared_ptr<client_session_t> &session)
    {
        if (success) {
            connect_success.fetch_add (1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lk (connected_mu);
            connected_sessions.push_back (session);
        } else {
            connect_fail.fetch_add (1, std::memory_order_relaxed);
        }

        connect_completed.fetch_add (1, std::memory_order_relaxed);
        connect_inflight.fetch_sub (1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lk (connect_mu);
            connect_cv.notify_all ();
        }

        schedule_connects ();
    }

    bool allow_send () const
    {
        if (mode.load (std::memory_order_acquire) == phase_idle)
            return false;
        return now_ns () < phase_end_ns.load (std::memory_order_relaxed);
    }

    size_t current_phase_size () const
    {
        return phase_size.load (std::memory_order_relaxed);
    }

    bool latency_sampling_enabled () const
    {
        return opt.latency_sample_rate > 0;
    }

    uint64_t next_seq ()
    {
        return seq_gen.fetch_add (1, std::memory_order_relaxed) + 1;
    }

    void on_send_begin (size_t)
    {
        outstanding_total.fetch_add (1, std::memory_order_relaxed);
    }

    void on_recv_done (size_t bytes, uint64_t seq, uint64_t sent_ns)
    {
        outstanding_total.fetch_sub (1, std::memory_order_relaxed);
        if (!collect_metrics.load (std::memory_order_acquire))
            return;

        bytes_recv_measure.fetch_add (
          static_cast<long long> (bytes), std::memory_order_relaxed);

        const int sample_rate = opt.latency_sample_rate;
        if (sample_rate > 0 && seq > 0 && sent_ns > 0
            && (seq % static_cast<uint64_t> (sample_rate)) == 0) {
            const uint64_t now = now_ns ();
            if (now > sent_ns) {
                const double us = static_cast<double> (now - sent_ns) / 1000.0;
                add_rtt_sample (us);
            }
        }
    }

    void on_send_error ()
    {
        if (!collect_metrics.load (std::memory_order_acquire))
            return;
        send_error_measure.fetch_add (1, std::memory_order_relaxed);
    }

    void on_recv_error ()
    {
        if (!collect_metrics.load (std::memory_order_acquire))
            return;
        recv_error_measure.fetch_add (1, std::memory_order_relaxed);
    }

    void on_timeout (long count)
    {
        if (count <= 0)
            return;
        timeout_error_measure.fetch_add (count, std::memory_order_relaxed);
    }

    void on_abandon (long count)
    {
        if (count > 0)
            outstanding_total.fetch_sub (count, std::memory_order_relaxed);
    }

    void on_size_mismatch ()
    {
        if (!collect_metrics.load (std::memory_order_acquire))
            return;
        size_mismatch_measure.fetch_add (1, std::memory_order_relaxed);
    }

    int inflight_limit () const { return std::max (1, opt.inflight); }

  private:
    tcp::endpoint source_bind_endpoint_for (size_t idx) const
    {
        if (loopback_bind_plan.source_addrs.empty ())
            return tcp::endpoint ();
        const boost::asio::ip::address_v4 &addr =
          loopback_bind_plan.source_addrs[idx
                                          % loopback_bind_plan.source_addrs.size ()];
        return tcp::endpoint (addr, 0);
    }

    bool set_phase_size_for_connected (size_t size)
    {
        phase_size.store (size, std::memory_order_release);
        std::vector<std::shared_ptr<client_session_t> > copy;
        {
            std::lock_guard<std::mutex> lk (connected_mu);
            copy = connected_sessions;
        }
        if (copy.empty ())
            return false;

        const std::shared_ptr<resize_latch_t> latch =
          std::make_shared<resize_latch_t> (copy.size ());
        for (size_t i = 0; i < copy.size (); ++i)
            copy[i]->set_chunk_size (size, latch);

        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (k_resize_timeout_s);
        std::unique_lock<std::mutex> lk (latch->mu);
        while (latch->pending > 0) {
            if (latch->cv.wait_until (lk, deadline) == std::cv_status::timeout)
                return false;
        }
        return true;
    }

    void kick_phase_for_connected ()
    {
        std::vector<std::shared_ptr<client_session_t> > copy;
        {
            std::lock_guard<std::mutex> lk (connected_mu);
            copy = connected_sessions;
        }
        for (size_t i = 0; i < copy.size (); ++i)
            copy[i]->start_traffic ();
    }

    bool run_window (int duration_s, bool measure)
    {
        if (duration_s <= 0)
            return true;

        if (measure)
            reset_measurement_counters ();

        collect_metrics.store (measure, std::memory_order_release);
        const uint64_t end_ns =
          now_ns ()
          + static_cast<uint64_t> (duration_s) * 1000ULL * 1000ULL * 1000ULL;
        phase_end_ns.store (end_ns, std::memory_order_release);
        mode.store (measure ? phase_measure : phase_warmup,
                    std::memory_order_release);

        kick_phase_for_connected ();
        std::this_thread::sleep_for (std::chrono::seconds (duration_s));

        mode.store (phase_idle, std::memory_order_release);
        collect_metrics.store (false, std::memory_order_release);

        // Multi benchmark behavior: bounded drain wait before timeout accounting.
        const auto drain_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (std::max (0, opt.drain_ms));
        while (std::chrono::steady_clock::now () < drain_deadline) {
            if (outstanding_total.load (std::memory_order_relaxed) <= 0)
                break;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }

        const long remaining = outstanding_total.load (std::memory_order_relaxed);
        if (remaining > 0) {
            on_timeout (remaining);
            on_abandon (remaining);
        }

        return true;
    }

    void run_size_transition_drain ()
    {
        const int drain_ms = std::max (0, opt.size_transition_drain_ms);
        if (drain_ms <= 0)
            return;

        const auto drain_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (drain_ms);
        while (std::chrono::steady_clock::now () < drain_deadline) {
            if (outstanding_total.load (std::memory_order_relaxed) <= 0)
                break;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    void reset_measurement_counters ()
    {
        bytes_recv_measure.store (0, std::memory_order_relaxed);
        send_error_measure.store (0, std::memory_order_relaxed);
        recv_error_measure.store (0, std::memory_order_relaxed);
        timeout_error_measure.store (0, std::memory_order_relaxed);
        size_mismatch_measure.store (0, std::memory_order_relaxed);
        sample_overwrite_idx.store (0, std::memory_order_relaxed);
    }

    void add_rtt_sample (double us)
    {
        if (rtt_samples_us.empty ())
            return;
        const size_t idx =
          sample_overwrite_idx.fetch_add (1, std::memory_order_relaxed)
          % rtt_samples_us.size ();
        rtt_samples_us[idx] = us;
    }

    case_metrics_t run_case (size_t size)
    {
        const bool resize_ok = set_phase_size_for_connected (size);
        if (!resize_ok) {
            case_metrics_t failed;
            failed.connect_ok = connect_success.load (std::memory_order_relaxed);
            failed.connect_fail = connect_fail.load (std::memory_order_relaxed);
            failed.timeout_error = 1;
            failed.pass = false;
            return failed;
        }

        if (opt.warmup > 0)
            (void)run_window (opt.warmup, false);

        const bool window_ok = run_window (std::max (1, opt.duration), true);

        case_metrics_t out;
        out.connect_ok = connect_success.load (std::memory_order_relaxed);
        out.connect_fail = connect_fail.load (std::memory_order_relaxed);

        const long long bytes_recv =
          bytes_recv_measure.load (std::memory_order_relaxed);
        const double duration_s = static_cast<double> (std::max (1, opt.duration));
        out.throughput_bps = duration_s > 0.0
                               ? static_cast<double> (bytes_recv) / duration_s
                               : 0.0;
        out.throughput_mib_s = out.throughput_bps / (1024.0 * 1024.0);

        const size_t sample_count = std::min<size_t> (
          sample_overwrite_idx.load (std::memory_order_relaxed),
          rtt_samples_us.size ());
        if (sample_count > 0 && latency_sampling_enabled ()) {
            std::vector<double> snapshot;
            snapshot.reserve (sample_count);
            for (size_t i = 0; i < sample_count; ++i)
                snapshot.push_back (rtt_samples_us[i]);
            std::sort (snapshot.begin (), snapshot.end ());
            out.p50_us = percentile_from_sorted (snapshot, 0.50);
            out.p95_us = percentile_from_sorted (snapshot, 0.95);
            out.p99_us = percentile_from_sorted (snapshot, 0.99);
        }

        out.send_error = send_error_measure.load (std::memory_order_relaxed);
        out.recv_error = recv_error_measure.load (std::memory_order_relaxed);
        out.timeout_error = timeout_error_measure.load (std::memory_order_relaxed);
        out.size_mismatch = size_mismatch_measure.load (std::memory_order_relaxed);

        out.pass = window_ok && out.connect_fail == 0 && out.send_error == 0
                   && out.recv_error == 0
                   && out.throughput_bps > 0.0;
        return out;
    }

    void shutdown_all_sessions ()
    {
        for (size_t i = 0; i < sessions.size (); ++i)
            sessions[i]->request_close ();
    }

    void join_workers ()
    {
        work_guard.reset ();
        io.stop ();
        for (size_t i = 0; i < workers.size (); ++i) {
            if (workers[i].joinable ())
                workers[i].join ();
        }
    }

    client_options_t opt;
    boost::asio::io_context io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard;

    std::vector<std::thread> workers;
    std::vector<std::shared_ptr<client_session_t> > sessions;
    std::vector<std::shared_ptr<client_session_t> > connected_sessions;

    std::mutex connected_mu;

    std::atomic<size_t> next_connect_idx;
    std::atomic<long> connect_inflight;
    std::atomic<long> connect_success;
    std::atomic<long> connect_fail;
    std::atomic<long> connect_completed;

    std::mutex connect_sched_mu;
    std::mutex connect_mu;
    std::condition_variable connect_cv;

    std::atomic<int> mode;
    std::atomic<uint64_t> phase_end_ns;
    std::atomic<size_t> phase_size;
    std::atomic<long> outstanding_total;

    std::atomic<uint64_t> seq_gen;

    std::atomic<long long> bytes_recv_measure;
    std::atomic<long> send_error_measure;
    std::atomic<long> recv_error_measure;
    std::atomic<long> timeout_error_measure;
    std::atomic<long> size_mismatch_measure;

    std::atomic<bool> collect_metrics;

    std::vector<double> rtt_samples_us;
    std::atomic<size_t> sample_overwrite_idx;

    tcp::endpoint endpoint;
    loopback_bind_plan_t loopback_bind_plan;
};

client_session_t::client_session_t (bench_client_t &owner_,
                                    boost::asio::io_context &io_,
                                    const tcp::endpoint &source_bind_ep_)
    : owner (owner_),
      socket (io_),
      retry_timer (io_),
      strand (boost::asio::make_strand (io_)),
      closed (false),
      connected_flag (false),
      connect_started (false),
      connect_reported (false),
      write_inflight (false),
      read_inflight (false),
      outstanding (0),
      chunk_size (64),
      connect_deadline (),
      endpoint (),
      source_bind_ep (source_bind_ep_),
      write_buf (),
      read_declared (0),
      read_buf ()
{
    std::memset (read_header, 0, sizeof (read_header));
}

bool client_session_t::connected () const
{
    return connected_flag && !closed;
}

void client_session_t::begin_connect (const tcp::endpoint &endpoint_)
{
    endpoint = endpoint_;
    const std::shared_ptr<client_session_t> self = shared_from_this ();
    boost::asio::post (strand, [self] () {
        if (self->closed)
            return;
        if (!self->connect_started)
            self->do_connect ();
    });
}

void client_session_t::set_chunk_size (size_t size,
                                       const std::shared_ptr<resize_latch_t> &latch)
{
    const std::shared_ptr<client_session_t> self = shared_from_this ();
    boost::asio::post (strand, [self, size, latch] () {
        if (!self->closed) {
            self->chunk_size = size;
        }
        if (latch) {
            {
                std::lock_guard<std::mutex> lk (latch->mu);
                if (latch->pending > 0)
                    --latch->pending;
            }
            latch->cv.notify_one ();
        }
    });
}

void client_session_t::start_traffic ()
{
    const std::shared_ptr<client_session_t> self = shared_from_this ();
    boost::asio::post (strand, [self] () {
        if (self->closed || !self->connected ())
            return;
        self->maybe_send_more ();
    });
}

void client_session_t::do_connect ()
{
    connect_started = true;
    if (connect_deadline.time_since_epoch ().count () == 0) {
        connect_deadline = std::chrono::steady_clock::now ()
                           + std::chrono::seconds (k_connect_timeout_s);
    }

    boost::system::error_code ec;
    if (socket.is_open ())
        socket.close (ec);

    socket.open (tcp::v4 (), ec);
    if (ec) {
        on_connect (ec);
        return;
    }

    if (source_bind_ep.address ().is_v4 ()
        && !source_bind_ep.address ().to_v4 ().is_unspecified ()) {
        socket.bind (source_bind_ep, ec);
        if (ec)
            ec.clear ();
    }

    const std::shared_ptr<client_session_t> self = shared_from_this ();
    socket.async_connect (
      endpoint,
      boost::asio::bind_executor (
        strand, [self] (const boost::system::error_code &e) { self->on_connect (e); }));
}

void client_session_t::on_connect (const boost::system::error_code &ec)
{
    if (closed || connect_reported)
        return;

    if (!ec) {
        connected_flag = true;
        connect_reported = true;
        retry_timer.cancel ();
        apply_socket_tuning ();
        owner.on_connect_result (true, shared_from_this ());
        return;
    }

    if (std::chrono::steady_clock::now () < connect_deadline) {
        retry_timer.expires_after (
          std::chrono::milliseconds (k_connect_retry_delay_ms));
        const std::shared_ptr<client_session_t> self = shared_from_this ();
        retry_timer.async_wait (boost::asio::bind_executor (
          strand, [self] (const boost::system::error_code &wait_ec) {
              if (wait_ec || self->closed)
                  return;
              self->do_connect ();
          }));
        return;
    }

    connect_reported = true;
    owner.on_connect_result (false, shared_from_this ());
    close_internal ();
}

void client_session_t::apply_socket_tuning ()
{
    boost::system::error_code ec;
    socket.set_option (
      boost::asio::ip::tcp::no_delay (k_socket_tcp_nodelay != 0), ec);
    boost::asio::socket_base::send_buffer_size snd (k_socket_sndbuf);
    socket.set_option (snd, ec);
    boost::asio::socket_base::receive_buffer_size rcv (k_socket_rcvbuf);
    socket.set_option (rcv, ec);
}

void client_session_t::maybe_send_more ()
{
    if (closed || !connected ())
        return;
    if (write_inflight)
        return;
    if (!owner.allow_send ())
        return;
    if (outstanding >= static_cast<size_t> (owner.inflight_limit ()))
        return;

    send_one ();
}

void client_session_t::send_one ()
{
    const size_t size = owner.current_phase_size ();
    if (size < k_min_chunk_size || size > k_max_chunk_size)
        return;

    chunk_size = size;
    const size_t wire_size = size + 4;
    if (write_buf.size () != wire_size)
        write_buf.resize (wire_size);
    store_u32_be (&write_buf[0], static_cast<uint32_t> (size));
    unsigned char *payload_write = write_buf.size () > 4 ? &write_buf[4] : NULL;

    if (owner.latency_sampling_enabled () && payload_write && size >= 16) {
        const uint64_t seq = owner.next_seq ();
        const uint64_t sent_ns = now_ns ();
        store_u64_be (payload_write, seq);
        store_u64_be (payload_write + 8, sent_ns);
    }

    owner.on_send_begin (size);
    ++outstanding;
    write_inflight = true;

    const std::shared_ptr<client_session_t> self = shared_from_this ();
    boost::asio::async_write (
      socket, boost::asio::buffer (write_buf),
      boost::asio::bind_executor (
        strand,
        [self] (const boost::system::error_code &ec, size_t bytes) {
            self->on_write (ec, bytes);
        }));
}

void client_session_t::on_write (const boost::system::error_code &ec, size_t bytes)
{
    write_inflight = false;

    if (closed)
        return;

    if (ec || bytes != write_buf.size ()) {
        owner.on_send_error ();
        if (outstanding > 0) {
            --outstanding;
            owner.on_abandon (1);
        }
        close_internal ();
        return;
    }

    start_read_header ();
    maybe_send_more ();
}

void client_session_t::start_read_header ()
{
    if (closed || !connected ())
        return;
    if (read_inflight)
        return;

    read_inflight = true;
    const std::shared_ptr<client_session_t> self = shared_from_this ();
    boost::asio::async_read (
      socket, boost::asio::buffer (read_header),
      boost::asio::bind_executor (
        strand,
        [self] (const boost::system::error_code &ec, size_t bytes) {
            self->on_read_header (ec, bytes);
        }));
}

void client_session_t::start_read_payload ()
{
    if (closed || !connected ())
        return;
    if (read_declared > static_cast<uint32_t> (k_max_chunk_size)) {
        owner.on_recv_error ();
        if (outstanding > 0) {
            const long dropped = static_cast<long> (outstanding);
            outstanding = 0;
            owner.on_abandon (dropped);
        }
        close_internal ();
        return;
    }

    if (read_buf.size () != static_cast<size_t> (read_declared))
        read_buf.resize (static_cast<size_t> (read_declared));

    const std::shared_ptr<client_session_t> self = shared_from_this ();
    boost::asio::async_read (
      socket, boost::asio::buffer (read_buf),
      boost::asio::bind_executor (
        strand,
        [self] (const boost::system::error_code &ec, size_t bytes) {
            self->on_read_payload (ec, bytes);
        }));
}

void client_session_t::on_read_header (const boost::system::error_code &ec,
                                       size_t bytes)
{
    if (closed)
        return;

    if (ec || bytes != sizeof (read_header)) {
        owner.on_recv_error ();
        if (outstanding > 0) {
            const long dropped = static_cast<long> (outstanding);
            outstanding = 0;
            owner.on_abandon (dropped);
        }
        close_internal ();
        return;
    }

    read_declared = load_u32_be (read_header);
    start_read_payload ();
}

void client_session_t::on_read_payload (const boost::system::error_code &ec,
                                        size_t bytes)
{
    read_inflight = false;

    if (closed)
        return;

    if (ec || bytes != static_cast<size_t> (read_declared)) {
        owner.on_recv_error ();
        if (outstanding > 0) {
            const long dropped = static_cast<long> (outstanding);
            outstanding = 0;
            owner.on_abandon (dropped);
        }
        close_internal ();
        return;
    }

    if (read_declared != static_cast<uint32_t> (chunk_size)) {
        owner.on_size_mismatch ();
        if (outstanding > 0) {
            --outstanding;
            owner.on_abandon (1);
        }
        maybe_send_more ();
        if (outstanding > 0)
            start_read_header ();
        return;
    }

    const size_t payload_bytes = bytes;
    const unsigned char *payload_ptr = payload_bytes > 0 ? &read_buf[0] : NULL;

    uint64_t seq = 0;
    uint64_t sent_ns = 0;
    if (owner.latency_sampling_enabled () && payload_ptr && payload_bytes >= 16) {
        seq = load_u64_be (payload_ptr);
        sent_ns = load_u64_be (payload_ptr + 8);
    }

    if (outstanding > 0) {
        --outstanding;
        owner.on_recv_done (payload_bytes, seq, sent_ns);
    }

    maybe_send_more ();
    if (outstanding > 0)
        start_read_header ();
}

void client_session_t::request_close ()
{
    const std::shared_ptr<client_session_t> self = shared_from_this ();
    boost::asio::post (strand, [self] () { self->close_internal (); });
}

void client_session_t::close_internal ()
{
    if (closed)
        return;
    closed = true;

    if (outstanding > 0) {
        const long dropped = static_cast<long> (outstanding);
        outstanding = 0;
        owner.on_abandon (dropped);
    }

    boost::system::error_code ec;
    retry_timer.cancel ();
    socket.cancel (ec);
    socket.shutdown (tcp::socket::shutdown_both, ec);
    socket.close (ec);
}

enum simple_transport_mode_t
{
    simple_transport_tcp = 0,
    simple_transport_tls = 1,
    simple_transport_ws = 2,
    simple_transport_wss = 3,
};

simple_transport_mode_t parse_simple_transport_mode (const std::string &transport)
{
    const std::string t = lower_copy (transport);
    if (t == "tls")
        return simple_transport_tls;
    if (t == "ws")
        return simple_transport_ws;
    if (t == "wss")
        return simple_transport_wss;
    return simple_transport_tcp;
}

class simple_transport_client_t
{
  public:
    simple_transport_client_t (const std::string &transport_,
                               const std::string &host_,
                               int port_)
        : mode (parse_simple_transport_mode (transport_)),
          host (host_),
          port (port_),
          io (),
          tls_ctx (boost::asio::ssl::context::tls_client),
          tcp_socket (),
          tls_socket (),
          ws_socket (),
          wss_socket ()
    {
        tls_ctx.set_verify_mode (boost::asio::ssl::verify_none);
    }

    bool connect ()
    {
        boost::system::error_code ec;
        tcp::resolver resolver (io);
        const tcp::resolver::results_type endpoints =
          resolver.resolve (host, std::to_string (port), ec);
        if (ec)
            return false;

        if (mode == simple_transport_tcp) {
            tcp_socket.reset (new tcp::socket (io));
            boost::asio::connect (*tcp_socket, endpoints, ec);
            return !ec;
        }

        if (mode == simple_transport_tls) {
            tls_socket.reset (
              new boost::asio::ssl::stream<tcp::socket> (io, tls_ctx));
            boost::asio::connect (tls_socket->next_layer (), endpoints, ec);
            if (ec)
                return false;
            tls_socket->handshake (boost::asio::ssl::stream_base::client, ec);
            return !ec;
        }

        if (mode == simple_transport_ws) {
            ws_socket.reset (
              new boost::beast::websocket::stream<tcp::socket> (io));
            boost::asio::connect (ws_socket->next_layer (), endpoints, ec);
            if (ec)
                return false;
            ws_socket->binary (true);
            ws_socket->set_option (
              boost::beast::websocket::stream_base::timeout::suggested (
                boost::beast::role_type::client));
            ws_socket->handshake (host + ":" + std::to_string (port), "/", ec);
            return !ec;
        }

        wss_socket.reset (
          new boost::beast::websocket::stream<
            boost::asio::ssl::stream<tcp::socket> > (io, tls_ctx));
        boost::asio::connect (wss_socket->next_layer ().next_layer (), endpoints,
                              ec);
        if (ec)
            return false;
        wss_socket->next_layer ().handshake (
          boost::asio::ssl::stream_base::client, ec);
        if (ec)
            return false;
        wss_socket->binary (true);
        wss_socket->set_option (
          boost::beast::websocket::stream_base::timeout::suggested (
            boost::beast::role_type::client));
        wss_socket->handshake (host + ":" + std::to_string (port), "/", ec);
        return !ec;
    }

    bool send_payload (const std::vector<unsigned char> &payload)
    {
        std::vector<unsigned char> frame (4 + payload.size ());
        store_u32_be (&frame[0], static_cast<uint32_t> (payload.size ()));
        if (!payload.empty ())
            std::memcpy (&frame[4], &payload[0], payload.size ());

        return write_frame_bytes (&frame[0], frame.size ());
    }

    bool send_payload_only (const std::vector<unsigned char> &payload)
    {
        return send_payload (payload);
    }

    bool recv_payload (std::vector<unsigned char> *payload_out)
    {
        if (!payload_out)
            return false;
        payload_out->clear ();

        std::vector<unsigned char> frame;
        if (!read_frame_bytes (&frame))
            return false;
        if (frame.size () < 4)
            return false;
        const uint32_t declared = load_u32_be (&frame[0]);
        if (declared > static_cast<uint32_t> (k_max_chunk_size))
            return false;
        if (frame.size () != static_cast<size_t> (4 + declared))
            return false;
        payload_out->assign (frame.begin () + 4, frame.end ());
        return true;
    }

    void close ()
    {
        boost::system::error_code ec;
        if (ws_socket) {
            ws_socket->close (boost::beast::websocket::close_code::normal, ec);
            ws_socket.reset ();
        }
        if (wss_socket) {
            wss_socket->close (boost::beast::websocket::close_code::normal, ec);
            wss_socket.reset ();
        }
        if (tls_socket) {
            tls_socket->shutdown (ec);
            tls_socket->next_layer ().close (ec);
            tls_socket.reset ();
        }
        if (tcp_socket) {
            tcp_socket->shutdown (tcp::socket::shutdown_both, ec);
            tcp_socket->close (ec);
            tcp_socket.reset ();
        }
    }

    ~simple_transport_client_t () { close (); }

  private:
    bool write_frame_bytes (const unsigned char *data, size_t size)
    {
        boost::system::error_code ec;
        if (mode == simple_transport_tcp && tcp_socket) {
            const size_t n =
              boost::asio::write (*tcp_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == simple_transport_tls && tls_socket) {
            const size_t n =
              boost::asio::write (*tls_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == simple_transport_ws && ws_socket) {
            const size_t n = ws_socket->write (boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == simple_transport_wss && wss_socket) {
            const size_t n =
              wss_socket->write (boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        return false;
    }

    bool read_exact_tcp_like (unsigned char *data, size_t size)
    {
        boost::system::error_code ec;
        if (mode == simple_transport_tcp && tcp_socket) {
            const size_t n =
              boost::asio::read (*tcp_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        if (mode == simple_transport_tls && tls_socket) {
            const size_t n =
              boost::asio::read (*tls_socket, boost::asio::buffer (data, size), ec);
            return !ec && n == size;
        }
        return false;
    }

    bool read_frame_bytes (std::vector<unsigned char> *frame_out)
    {
        if (!frame_out)
            return false;
        frame_out->clear ();

        if (mode == simple_transport_tcp || mode == simple_transport_tls) {
            unsigned char hdr[4];
            if (!read_exact_tcp_like (hdr, sizeof (hdr)))
                return false;
            const uint32_t declared = load_u32_be (hdr);
            if (declared > static_cast<uint32_t> (k_max_chunk_size))
                return false;

            frame_out->resize (4 + declared);
            std::memcpy (&(*frame_out)[0], hdr, sizeof (hdr));
            if (declared > 0
                && !read_exact_tcp_like (&(*frame_out)[4], static_cast<size_t> (declared))) {
                return false;
            }
            return true;
        }

        boost::system::error_code ec;
        boost::beast::flat_buffer recv_buf;
        if (mode == simple_transport_ws && ws_socket) {
            ws_socket->read (recv_buf, ec);
        } else if (mode == simple_transport_wss && wss_socket) {
            wss_socket->read (recv_buf, ec);
        } else {
            return false;
        }
        if (ec)
            return false;

        const size_t n = recv_buf.size ();
        frame_out->resize (n);
        if (n > 0) {
            boost::asio::buffer_copy (boost::asio::buffer (&(*frame_out)[0], n),
                                      recv_buf.cdata ());
        }
        return true;
    }

  private:
    simple_transport_mode_t mode;
    std::string host;
    int port;
    boost::asio::io_context io;
    boost::asio::ssl::context tls_ctx;
    std::unique_ptr<tcp::socket> tcp_socket;
    std::unique_ptr<boost::asio::ssl::stream<tcp::socket> > tls_socket;
    std::unique_ptr<boost::beast::websocket::stream<tcp::socket> > ws_socket;
    std::unique_ptr<boost::beast::websocket::stream<
      boost::asio::ssl::stream<tcp::socket> > > wss_socket;
};

case_metrics_t run_simple_case (const client_options_t &opt, size_t size)
{
    case_metrics_t out;
    simple_transport_client_t client (opt.transport, opt.host, opt.port);
    if (!client.connect ()) {
        out.connect_fail = 1;
        out.pass = false;
        return out;
    }

    out.connect_ok = 1;
    out.connect_fail = 0;

    std::vector<unsigned char> payload (size, static_cast<unsigned char> ('a'));
    std::vector<unsigned char> echoed;
    std::vector<double> rtt_samples;
    rtt_samples.reserve (static_cast<size_t> (std::max (1, opt.lat_count)));

    auto do_roundtrip = [&](bool collect_latency) -> bool {
        const uint64_t t0 = now_ns ();
        if (!client.send_payload (payload)) {
            ++out.send_error;
            return false;
        }
        if (!client.recv_payload (&echoed)) {
            ++out.recv_error;
            return false;
        }
        if (echoed.size () != payload.size ()) {
            ++out.size_mismatch;
            return false;
        }
        if (collect_latency) {
            const uint64_t t1 = now_ns ();
            const double us = static_cast<double> (t1 - t0) / 1000.0;
            rtt_samples.push_back (us);
        }
        return true;
    };

    if (opt.msg_count > 0) {
        for (int i = 0; i < opt.warmup_count; ++i) {
            if (!do_roundtrip (false))
                break;
        }
    } else {
        const uint64_t warmup_deadline =
          now_ns () + static_cast<uint64_t> (std::max (0, opt.warmup)) * 1000000000ULL;
        while (now_ns () < warmup_deadline) {
            if (!do_roundtrip (false))
                break;
        }
    }

    const int latency_iters = std::max (1, opt.lat_count > 0 ? opt.lat_count : 200);
    for (int i = 0; i < latency_iters; ++i) {
        if (!do_roundtrip (true))
            break;
    }

    const uint64_t measure_begin_ns = now_ns ();
    long measured_ops = 0;
    if (opt.msg_count > 0) {
        for (int i = 0; i < opt.msg_count; ++i) {
            if (!do_roundtrip (false))
                break;
            ++measured_ops;
        }
    } else {
        const uint64_t measure_deadline_ns =
          measure_begin_ns
          + static_cast<uint64_t> (std::max (1, opt.duration)) * 1000000000ULL;
        while (now_ns () < measure_deadline_ns) {
            if (!do_roundtrip (false))
                break;
            ++measured_ops;
        }
    }
    const uint64_t measure_end_ns = now_ns ();

    const double elapsed_s =
      std::max (1e-9,
                static_cast<double> (measure_end_ns - measure_begin_ns) / 1e9);
    out.throughput_bps =
      (static_cast<double> (measured_ops) * static_cast<double> (size)) / elapsed_s;
    out.throughput_mib_s = out.throughput_bps / (1024.0 * 1024.0);

    if (!rtt_samples.empty ()) {
        std::sort (rtt_samples.begin (), rtt_samples.end ());
        out.p50_us = percentile_from_sorted (rtt_samples, 0.50);
        out.p95_us = percentile_from_sorted (rtt_samples, 0.95);
        out.p99_us = percentile_from_sorted (rtt_samples, 0.99);
    }

    out.pass = out.connect_ok > 0 && measured_ops > 0 && out.send_error == 0
               && out.recv_error == 0 && out.size_mismatch == 0;
    return out;
}

void print_case_line (size_t size, int run_idx, const case_metrics_t &m)
{
    const char *pass_text = m.pass ? "PASS" : "FAIL";
    std::printf (
      "RESULT size=%zu run=%d throughput_bps=%.2f throughput_mib_s=%.2f "
      "p50_us=%.2f p95_us=%.2f p99_us=%.2f connect_ok=%ld "
      "connect_fail=%ld send_err=%ld recv_err=%ld timeout=%ld "
      "size_mismatch=%ld "
      "pass_fail=%s\n",
      size, run_idx, m.throughput_bps, m.throughput_mib_s, m.p50_us, m.p95_us,
      m.p99_us, m.connect_ok, m.connect_fail, m.send_error, m.recv_error,
      m.timeout_error, m.size_mismatch, pass_text);
}

void print_perf_result_line (const client_options_t &opt,
                             size_t size,
                             const case_metrics_t &m)
{
    if (opt.print_perf_result <= 0 || !m.pass)
        return;
    const double throughput =
      size > 0 ? (m.throughput_bps / static_cast<double> (size)) : 0.0;
    const double bandwidth = m.throughput_bps / 1000000.0;
    const double latency = m.p50_us * 0.5;
    std::printf ("RESULT,current,%s,%s,%zu,throughput,%.6f\n",
                 opt.pattern.c_str (), opt.transport.c_str (), size, throughput);
    std::printf ("RESULT,current,%s,%s,%zu,bandwidth,%.6f\n",
                 opt.pattern.c_str (), opt.transport.c_str (), size, bandwidth);
    std::printf ("RESULT,current,%s,%s,%zu,latency,%.6f\n",
                 opt.pattern.c_str (), opt.transport.c_str (), size, latency);
}

bool send_stop_token_once (const client_options_t &opt)
{
    if (opt.send_stop_token <= 0)
        return true;
    simple_transport_client_t client (opt.transport, opt.host, opt.port);
    if (!client.connect ())
        return false;
    const std::vector<unsigned char> token_payload (opt.stop_token.begin (),
                                                    opt.stop_token.end ());
    return client.send_payload_only (token_payload);
}

int run_simple_client (const client_options_t &opt)
{
    bool all_pass = true;
    for (int run_idx = 1; run_idx <= std::max (1, opt.runs); ++run_idx) {
        for (size_t i = 0; i < opt.sizes.size (); ++i) {
            const size_t size = opt.sizes[i];
            const case_metrics_t m = run_simple_case (opt, size);
            if (opt.print_perf_result <= 1)
                print_case_line (size, run_idx, m);
            print_perf_result_line (opt, size, m);
            if (!m.pass)
                all_pass = false;
            if ((i + 1) < opt.sizes.size ()
                && opt.size_transition_drain_ms > 0) {
                std::this_thread::sleep_for (
                  std::chrono::milliseconds (opt.size_transition_drain_ms));
            }
        }
    }
    if (!send_stop_token_once (opt))
        all_pass = false;
    return all_pass ? 0 : 2;
}

bool parse_options (int argc, char **argv, client_options_t &opt)
{
    const arg_reader_t args (argc, argv);

    opt.transport = lower_copy (args.get_string ("--transport", opt.transport.c_str ()));
    opt.pattern = args.get_string ("--pattern", opt.pattern.c_str ());
    opt.host = args.get_string ("--host", opt.host.c_str ());
    opt.port = args.get_int ("--port", opt.port, 1);
    opt.ccu = args.get_int ("--ccu", opt.ccu, 1);
    opt.runs = args.get_int ("--runs", opt.runs, 1);
    opt.warmup = args.get_int ("--warmup", opt.warmup, 0);
    opt.duration = args.get_int ("--duration", opt.duration, 1);
    opt.drain_ms = args.get_int ("--drain-ms", opt.drain_ms, 0);
    opt.size_transition_drain_ms = args.get_int (
      "--size-transition-drain-ms", opt.size_transition_drain_ms, 0);
    opt.inflight = args.get_int ("--inflight", opt.inflight, 1);
    opt.io_threads = args.get_int ("--io-threads", opt.io_threads, 1);
    opt.latency_sample_rate = args.get_int ("--latency-sample-rate",
                                            opt.latency_sample_rate, 0);
    opt.warmup_count = args.get_int ("--warmup-count", opt.warmup_count, 0);
    opt.lat_count = args.get_int ("--lat-count", opt.lat_count, 0);
    opt.msg_count = args.get_int ("--msg-count", opt.msg_count, 0);
    opt.print_perf_result = args.get_int ("--print-perf-result",
                                          opt.print_perf_result, 0);
    opt.stop_token = args.get_string ("--stop-token", opt.stop_token.c_str ());
    opt.send_stop_token = args.get_int ("--send-stop-token",
                                        opt.send_stop_token, 0);

    const std::string endpoint = args.get_string ("--endpoint", "");
    if (!endpoint.empty ()) {
        std::string endpoint_transport;
        std::string endpoint_host;
        int endpoint_port = 0;
        if (!parse_endpoint (endpoint, &endpoint_transport, &endpoint_host, &endpoint_port)) {
            std::fprintf (stderr, "invalid --endpoint: %s\n", endpoint.c_str ());
            return false;
        }
        if (opt.transport.empty ())
            opt.transport = endpoint_transport;
        else if (opt.transport != endpoint_transport)
            opt.transport = endpoint_transport;
        opt.host = endpoint_host;
        opt.port = endpoint_port;
    }

    if (opt.transport != "tcp" && opt.transport != "tls" && opt.transport != "ws"
        && opt.transport != "wss") {
        std::fprintf (stderr, "invalid --transport: %s\n", opt.transport.c_str ());
        return false;
    }

    const std::string sizes_text = args.get_string ("--sizes", "");
    if (!sizes_text.empty ()) {
        if (!parse_size_list (sizes_text, opt.sizes)) {
            std::fprintf (stderr, "invalid --sizes: %s\n", sizes_text.c_str ());
            return false;
        }
    }

    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("bench_stream_client: no args -> skip\n");
        return 0;
    }

    client_options_t opt;
    if (!parse_options (argc, argv, opt))
        return 2;

    if (opt.msg_count > 0 || opt.transport != "tcp") {
        return run_simple_client (opt);
    }

    bench_client_t app (opt);
    int rc = app.run ();
    if (opt.send_stop_token > 0 && !send_stop_token_once (opt))
        rc = 2;
    return rc;
}
