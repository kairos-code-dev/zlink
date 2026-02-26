#ifndef PERF_STREAM_PERF_CLIENT_HPP
#define PERF_STREAM_PERF_CLIENT_HPP

// Async multi-connection benchmark orchestrator (tcp/tls/ws/wss).
// Provides:
//   loopback_bind_plan_t      – source-address sharding plan for loopback
//   read_ipv4_ephemeral_port_capacity() – reads OS ephemeral port range
//   make_loopback_shard_addr()          – generates 127.x.x.x addresses
//   make_loopback_bind_plan()           – computes required shard count
//   bench_client_t             – orchestrator (implements bench_client_iface_t)
//
// bench_client_t manages the full async benchmark lifecycle:
//   1. Spin up io_context + worker threads
//   2. Create CCU client_session_t instances
//   3. Batched connect scheduling
//   4. For each size: resize → warmup → measure → drain → report
//   5. Shutdown and join

#include "perf_stream_client_options.hpp"
#include "perf_stream_client_session.hpp"
#include "perf_stream_common.hpp"

#include <boost/asio.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// --- Loopback port sharding ---
// When CCU exceeds the OS ephemeral port range on loopback, connections are
// distributed across multiple 127.x.x.x source addresses to avoid exhaustion.

struct loopback_bind_plan_t
{
    size_t port_capacity;
    std::vector<boost::asio::ip::address_v4> source_addrs;

    loopback_bind_plan_t () : port_capacity (0), source_addrs () {}
};

// Read usable ephemeral port count from /proc/sys/net/ipv4/ip_local_port_range.
inline size_t read_ipv4_ephemeral_port_capacity ()
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

// Generate a unique loopback address: 127.0.<block>.<1..254>.
inline boost::asio::ip::address_v4 make_loopback_shard_addr (size_t idx)
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

// Compute source-address sharding plan.
// shards = ceil(ccu / usable_ephemeral_ports). Returns empty plan for non-loopback.
inline loopback_bind_plan_t
make_loopback_bind_plan (const boost::asio::ip::tcp::endpoint &endpoint, int ccu)
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

// --- Benchmark orchestrator ---
// Manages worker threads, sessions, phase transitions, and metrics collection.
// Implements bench_client_iface_t so sessions can report events thread-safely.

class bench_client_t : public bench_client_iface_t
{
  public:
    explicit bench_client_t (const client_options_t &opt_)
        : opt (opt_),
          io (),
          work_guard (boost::asio::make_work_guard (io)),
          next_connect_idx (0),
          connect_active (0),
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
          rtt_samples_bits (new std::atomic<uint64_t>[k_rtt_sample_capacity]),
          sample_overwrite_idx (0),
          endpoint (boost::asio::ip::make_address (opt.host),
                    static_cast<unsigned short> (opt.port)),
          loopback_bind_plan ()
    {
        loopback_bind_plan = make_loopback_bind_plan (endpoint, opt.ccu);
    }

    // Main entry: connect all sessions, run benchmarks per size, then shutdown.
    // Returns 0 on all-pass, 2 on any failure.
    int run ()
    {
        const int worker_count = std::max (1, opt.io_threads);
        for (int i = 0; i < worker_count; ++i)
            workers.push_back (std::thread ([this] () { io.run (); }));

        sessions.reserve (static_cast<size_t> (std::max (1, opt.ccu)));
        for (int i = 0; i < opt.ccu; ++i) {
            sessions.push_back (std::make_shared<client_session_t> (
              *this, io, opt.transport,
              source_bind_endpoint_for (static_cast<size_t> (i))));
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
                if (opt.print_perf_result > 0) {
                    std::printf (
                      "RESULT_CONNECT,current,%s,%s,%zu,connect_ok,%ld,connect_fail,%ld,"
                      "connect_target,%d\n",
                      opt.pattern.c_str (), opt.transport.c_str (), size,
                      m.connect_ok, m.connect_fail, std::max (1, opt.ccu));
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
                std::fflush (stdout);
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

    // --- bench_client_iface_t overrides (called from I/O threads) ---

    // Track connect success/failure and trigger next batched connect.
    void on_connect_result (
      bool success,
      const std::shared_ptr<client_session_t> &session) override
    {
        if (success) {
            connect_success.fetch_add (1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lk (connected_mu);
            connected_sessions.push_back (session);
        } else {
            connect_fail.fetch_add (1, std::memory_order_relaxed);
        }

        connect_completed.fetch_add (1, std::memory_order_relaxed);
        connect_active.fetch_sub (1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lk (connect_mu);
            connect_cv.notify_all ();
        }

        schedule_connects ();
    }

    bool allow_send () const override
    {
        if (mode.load (std::memory_order_acquire) == phase_idle)
            return false;
        return perf_stream_common::perf_stream_now_ns ()
               < phase_end_ns.load (std::memory_order_relaxed);
    }

    size_t current_phase_size () const override
    {
        return phase_size.load (std::memory_order_relaxed);
    }

    bool latency_sampling_enabled () const override
    {
        return opt.latency_sample_rate > 0;
    }

    uint64_t next_seq () override
    {
        return seq_gen.fetch_add (1, std::memory_order_relaxed) + 1;
    }

    void on_send_begin (size_t) override
    {
        outstanding_total.fetch_add (1, std::memory_order_relaxed);
    }

    // Record received bytes; sample RTT if latency sampling is active.
    void on_recv_done (size_t bytes, uint64_t seq, uint64_t sent_ns) override
    {
        outstanding_total.fetch_sub (1, std::memory_order_relaxed);
        if (!collect_metrics.load (std::memory_order_acquire))
            return;

        bytes_recv_measure.fetch_add (
          static_cast<long long> (bytes), std::memory_order_relaxed);

        const int sample_rate = opt.latency_sample_rate;
        if (sample_rate > 0 && seq > 0 && sent_ns > 0
            && (seq % static_cast<uint64_t> (sample_rate)) == 0) {
            const uint64_t now = perf_stream_common::perf_stream_now_ns ();
            if (now > sent_ns) {
                const double us = static_cast<double> (now - sent_ns) / 1000.0;
                add_rtt_sample (us);
            }
        }
    }

    void on_send_error () override
    {
        if (!collect_metrics.load (std::memory_order_acquire))
            return;
        send_error_measure.fetch_add (1, std::memory_order_relaxed);
    }

    void on_recv_error () override
    {
        if (!collect_metrics.load (std::memory_order_acquire))
            return;
        recv_error_measure.fetch_add (1, std::memory_order_relaxed);
    }

    void on_abandon (long count) override
    {
        if (count > 0)
            outstanding_total.fetch_sub (count, std::memory_order_relaxed);
    }

    void on_size_mismatch () override
    {
        if (!collect_metrics.load (std::memory_order_acquire))
            return;
        size_mismatch_measure.fetch_add (1, std::memory_order_relaxed);
    }

  private:
    // --- Connection management ---

    // Launch up to k_connect_batch concurrent connect() calls.
    void schedule_connects ()
    {
        std::lock_guard<std::mutex> lk (connect_sched_mu);
        while (connect_active.load (std::memory_order_relaxed) < k_connect_batch) {
            const size_t idx =
              next_connect_idx.fetch_add (1, std::memory_order_relaxed);
            if (idx >= sessions.size ())
                break;
            connect_active.fetch_add (1, std::memory_order_relaxed);
            sessions[idx]->begin_connect (endpoint);
        }
    }

    // Map session index to a loopback shard address (round-robin).
    boost::asio::ip::tcp::endpoint source_bind_endpoint_for (size_t idx) const
    {
        if (loopback_bind_plan.source_addrs.empty ())
            return boost::asio::ip::tcp::endpoint ();
        const boost::asio::ip::address_v4 &addr =
          loopback_bind_plan
            .source_addrs[idx % loopback_bind_plan.source_addrs.size ()];
        return boost::asio::ip::tcp::endpoint (addr, 0);
    }

    std::vector<std::shared_ptr<client_session_t> > snapshot_connected_sessions ()
    {
        std::vector<std::shared_ptr<client_session_t> > copy;
        {
            std::lock_guard<std::mutex> lk (connected_mu);
            copy.reserve (connected_sessions.size ());
            for (size_t i = 0; i < connected_sessions.size (); ++i) {
                const std::shared_ptr<client_session_t> &session =
                  connected_sessions[i];
                if (session && session->connected ())
                    copy.push_back (session);
            }
        }
        return copy;
    }

    long count_connected_sessions ()
    {
        long count = 0;
        std::lock_guard<std::mutex> lk (connected_mu);
        for (size_t i = 0; i < connected_sessions.size (); ++i) {
            const std::shared_ptr<client_session_t> &session =
              connected_sessions[i];
            if (session && session->connected ())
                ++count;
        }
        return count;
    }

    // --- Phase control ---

    // Dispatch chunk-size update to all connected sessions and wait on latch.
    bool set_phase_size_for_connected (size_t size)
    {
        phase_size.store (size, std::memory_order_release);
        std::vector<std::shared_ptr<client_session_t> > copy =
          snapshot_connected_sessions ();
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

    // Start traffic on all connected sessions (begins the send loop).
    void kick_phase_for_connected ()
    {
        std::vector<std::shared_ptr<client_session_t> > copy =
          snapshot_connected_sessions ();
        for (size_t i = 0; i < copy.size (); ++i)
            copy[i]->start_traffic ();
    }

    void on_timeout (long count)
    {
        if (count <= 0)
            return;
        timeout_error_measure.fetch_add (count, std::memory_order_relaxed);
    }

    // Run a timed window (warmup or measure). Kicks traffic, sleeps for
    // duration, then stops and drains in-flight ops.
    bool run_window (int duration_s, bool measure)
    {
        if (duration_s <= 0)
            return true;

        if (measure)
            reset_measurement_counters ();

        collect_metrics.store (measure, std::memory_order_release);
        const uint64_t end_ns =
          perf_stream_common::perf_stream_now_ns ()
          + static_cast<uint64_t> (duration_s) * 1000ULL * 1000ULL * 1000ULL;
        phase_end_ns.store (end_ns, std::memory_order_release);
        mode.store (measure ? phase_measure : phase_warmup,
                    std::memory_order_release);

        kick_phase_for_connected ();
        std::this_thread::sleep_for (std::chrono::seconds (duration_s));

        mode.store (phase_idle, std::memory_order_release);
        collect_metrics.store (false, std::memory_order_release);

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

    // Brief drain between size transitions to let in-flight ops complete.
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

    // --- RTT sample ring buffer ---
    // Stores latency samples as bit-cast double→uint64_t in atomic[] for
    // lock-free write from I/O threads and safe read from the main thread.

    static uint64_t encode_double_bits (double v)
    {
        uint64_t bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        return bits;
    }

    static double decode_double_bits (uint64_t bits)
    {
        double v = 0.0;
        std::memcpy (&v, &bits, sizeof (v));
        return v;
    }

    void add_rtt_sample (double us)
    {
        if (!rtt_samples_bits)
            return;

        const size_t idx =
          sample_overwrite_idx.fetch_add (1, std::memory_order_relaxed)
          % k_rtt_sample_capacity;
        rtt_samples_bits[idx].store (encode_double_bits (us),
                                     std::memory_order_release);
    }

    // --- Per-size benchmark execution ---
    // resize → warmup → measure → collect metrics
    case_metrics_t run_case (size_t size)
    {
        const long connect_target = static_cast<long> (std::max (1, opt.ccu));
        const long required_connect = std::max<long> (1, connect_target);

        const bool resize_ok = set_phase_size_for_connected (size);
        if (!resize_ok) {
            case_metrics_t failed;
            failed.connect_ok = count_connected_sessions ();
            failed.connect_fail =
              std::max<long> (0, connect_target - failed.connect_ok);
            failed.timeout_error = 1;
            failed.pass = false;
            return failed;
        }

        if (opt.warmup > 0)
            (void)run_window (opt.warmup, false);

        const bool window_ok = run_window (std::max (1, opt.duration), true);

        case_metrics_t out;
        out.connect_ok = count_connected_sessions ();
        out.connect_fail = std::max<long> (0, connect_target - out.connect_ok);

        const long long bytes_recv =
          bytes_recv_measure.load (std::memory_order_relaxed);
        const double duration_s = static_cast<double> (std::max (1, opt.duration));
        out.throughput_bps = duration_s > 0.0
                               ? static_cast<double> (bytes_recv) / duration_s
                               : 0.0;
        out.throughput_mib_s = out.throughput_bps / (1024.0 * 1024.0);

        const size_t sample_count = std::min<size_t> (
          sample_overwrite_idx.load (std::memory_order_relaxed),
          k_rtt_sample_capacity);
        if (sample_count > 0 && latency_sampling_enabled ()) {
            std::vector<double> snapshot;
            snapshot.reserve (sample_count);
            for (size_t i = 0; i < sample_count; ++i) {
                snapshot.push_back (decode_double_bits (
                  rtt_samples_bits[i].load (std::memory_order_acquire)));
            }
            std::sort (snapshot.begin (), snapshot.end ());
            out.p50_us = perf_stream_common::percentile_from_sorted (snapshot, 0.50);
            out.p95_us = perf_stream_common::percentile_from_sorted (snapshot, 0.95);
            out.p99_us = perf_stream_common::percentile_from_sorted (snapshot, 0.99);
        }

        out.send_error = send_error_measure.load (std::memory_order_relaxed);
        out.recv_error = recv_error_measure.load (std::memory_order_relaxed);
        out.timeout_error = timeout_error_measure.load (std::memory_order_relaxed);
        out.size_mismatch = size_mismatch_measure.load (std::memory_order_relaxed);

        out.pass = window_ok && out.connect_ok >= required_connect
                   && out.send_error == 0 && out.recv_error == 0
                   && out.timeout_error == 0 && out.size_mismatch == 0
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

    // --- Member state ---

    client_options_t opt;
    boost::asio::io_context io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard;                      // keeps io_context alive until shutdown

    std::vector<std::thread> workers;  // I/O worker threads
    std::vector<std::shared_ptr<client_session_t> > sessions;           // all sessions
    std::vector<std::shared_ptr<client_session_t> > connected_sessions; // successfully connected

    std::mutex connected_mu;           // guards connected_sessions

    // --- Connect tracking (atomics for cross-thread access) ---
    std::atomic<size_t> next_connect_idx;
    std::atomic<long> connect_active;
    std::atomic<long> connect_success;
    std::atomic<long> connect_fail;
    std::atomic<long> connect_completed;

    std::mutex connect_sched_mu;       // serializes schedule_connects()
    std::mutex connect_mu;             // guards connect_cv wait
    std::condition_variable connect_cv;

    // --- Phase state (atomics read by I/O threads) ---
    std::atomic<int> mode;             // phase_mode_t
    std::atomic<uint64_t> phase_end_ns;
    std::atomic<size_t> phase_size;
    std::atomic<long> outstanding_total; // global in-flight count

    std::atomic<uint64_t> seq_gen;     // monotonic sequence for latency embedding

    // --- Measurement counters (reset per-case) ---
    std::atomic<long long> bytes_recv_measure;
    std::atomic<long> send_error_measure;
    std::atomic<long> recv_error_measure;
    std::atomic<long> timeout_error_measure;
    std::atomic<long> size_mismatch_measure;

    std::atomic<bool> collect_metrics; // true only during measure window

    // --- RTT ring buffer ---
    std::unique_ptr<std::atomic<uint64_t>[]> rtt_samples_bits; // bit-cast doubles
    std::atomic<size_t> sample_overwrite_idx;  // write cursor (wraps at capacity)
    boost::asio::ip::tcp::endpoint endpoint;
    loopback_bind_plan_t loopback_bind_plan;
};

#endif
