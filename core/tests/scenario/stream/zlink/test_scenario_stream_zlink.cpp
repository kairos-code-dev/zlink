/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"

#include <boost/asio.hpp>
#include <boost/asio/detail/socket_option.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using boost::asio::ip::tcp;
typedef boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
  io_work_guard_t;

struct Config
{
    std::string scenario;
    std::string transport;
    std::string bind_host;
    int port;
    int ccu;
    int size;
    int inflight;
    int warmup_sec;
    int measure_sec;
    int drain_timeout_sec;
    int connect_concurrency;
    int connect_timeout_sec;
    int connect_retries;
    int connect_retry_delay_ms;
    int backlog;
    int hwm;
    int sndbuf;
    int rcvbuf;
    int no_delay;
    int io_threads;
    int send_batch;
    int latency_sample_rate;
    std::string role;
    std::string scenario_id_override;
    std::string metrics_csv;
    std::string summary_json;

    Config ()
        : scenario ("s0"),
          transport ("tcp"),
          bind_host ("127.0.0.1"),
          port (27110),
          ccu (10000),
          size (1024),
          inflight (30),
          warmup_sec (3),
          measure_sec (10),
          drain_timeout_sec (10),
          connect_concurrency (256),
          connect_timeout_sec (10),
          connect_retries (3),
          connect_retry_delay_ms (100),
          backlog (32768),
          hwm (1000000),
          sndbuf (256 * 1024),
          rcvbuf (256 * 1024),
          no_delay (1),
          io_threads (1),
          send_batch (1),
          latency_sample_rate (1),
          role ("both"),
          scenario_id_override ("")
    {
    }
};

struct ResultRow
{
    std::string scenario_id;
    std::string transport;
    int ccu;
    int inflight;
    int size;
    long connect_success;
    long connect_fail;
    long connect_timeout;
    long sent;
    long recv;
    double incomplete_ratio;
    double throughput;
    double p50;
    double p95;
    double p99;
    std::map<int, long> errors_by_errno;
    std::string pass_fail;
    long drain_timeout_count;
    long gating_violation;

    ResultRow ()
        : ccu (0),
          inflight (0),
          size (0),
          connect_success (0),
          connect_fail (0),
          connect_timeout (0),
          sent (0),
          recv (0),
          incomplete_ratio (0.0),
          throughput (0.0),
          p50 (0.0),
          p95 (0.0),
          p99 (0.0),
          pass_fail ("FAIL"),
          drain_timeout_count (0),
          gating_violation (0)
    {
    }
};

struct ErrorBag
{
    std::map<int, long> by_errno;
    std::mutex lock;
};

uint64_t now_ns ()
{
    return static_cast<uint64_t> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now ().time_since_epoch ())
        .count ());
}

bool has_arg (int argc, char **argv, const char *key)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            return true;
    }
    return false;
}

int arg_int (int argc, char **argv, const char *key, int fallback)
{
    int value = fallback;
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            value = atoi (argv[i + 1]);
    }
    return value;
}

std::string arg_str (int argc,
                     char **argv,
                     const char *key,
                     const char *fallback)
{
    std::string value (fallback);
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            value = std::string (argv[i + 1]);
    }
    return value;
}

void record_error (ErrorBag &bag, int err)
{
    if (err <= 0)
        return;
    std::lock_guard<std::mutex> guard (bag.lock);
    bag.by_errno[err] += 1;
}

bool is_ignorable_socket_error (const boost::system::error_code &ec)
{
    return ec == boost::asio::error::connection_aborted
           || ec == boost::asio::error::connection_refused
           || ec == boost::asio::error::connection_reset
           || ec == boost::asio::error::eof
           || ec == boost::asio::error::operation_aborted;
}

void merge_errors (std::map<int, long> &dst, const ErrorBag &src)
{
    std::lock_guard<std::mutex> guard (
      const_cast<ErrorBag &> (src).lock);
    for (std::map<int, long>::const_iterator it = src.by_errno.begin ();
         it != src.by_errno.end (); ++it) {
        dst[it->first] += it->second;
    }
}

std::string errors_to_string (const std::map<int, long> &errors)
{
    if (errors.empty ())
        return "";

    std::string out;
    for (std::map<int, long>::const_iterator it = errors.begin ();
         it != errors.end (); ++it) {
        if (!out.empty ())
            out += ";";
        char tmp[64];
        snprintf (tmp, sizeof (tmp), "%d:%ld", it->first, it->second);
        out += tmp;
    }
    return out;
}

std::atomic<bool> g_server_stop_requested (false);

void handle_server_stop_signal (int)
{
    g_server_stop_requested.store (true, std::memory_order_release);
}

double percentile (std::vector<double> samples, double p)
{
    if (samples.empty ())
        return 0.0;

    std::sort (samples.begin (), samples.end ());
    const size_t idx = static_cast<size_t> (
      std::max<double> (0.0,
                        std::ceil (samples.size () * p) - 1.0));
    return samples[std::min (idx, samples.size () - 1)];
}

void write_u32_be (unsigned char *dst, uint32_t v)
{
    dst[0] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    dst[1] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    dst[2] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    dst[3] = static_cast<unsigned char> (v & 0xFF);
}

uint32_t read_u32_be (const unsigned char *src)
{
    return (static_cast<uint32_t> (src[0]) << 24)
           | (static_cast<uint32_t> (src[1]) << 16)
           | (static_cast<uint32_t> (src[2]) << 8)
           | static_cast<uint32_t> (src[3]);
}

void write_u64_le (unsigned char *dst, uint64_t v)
{
    memcpy (dst, &v, sizeof (v));
}

uint64_t read_u64_le (const unsigned char *src)
{
    uint64_t v = 0;
    memcpy (&v, src, sizeof (v));
    return v;
}

void fill_common_row (ResultRow &row, const Config &cfg)
{
    row.scenario_id =
      cfg.scenario_id_override.empty () ? cfg.scenario : cfg.scenario_id_override;
    row.transport = cfg.transport;
    row.ccu = cfg.ccu;
    row.inflight = cfg.inflight;
    row.size = cfg.size;
}

class BenchmarkState
{
  public:
    explicit BenchmarkState (const Config &cfg, int shard_count)
        : _cfg (cfg),
          _send_enabled (false),
          _measure_enabled (false)
    {
        const int shards = std::max (1, shard_count);
        _shards.reserve (static_cast<size_t> (shards));
        for (int i = 0; i < shards; ++i) {
            _shards.push_back (
              std::shared_ptr<ShardCounters> (new ShardCounters ()));
        }
    }

    void set_phase (bool send_enabled, bool measure_enabled)
    {
        _send_enabled.store (send_enabled, std::memory_order_release);
        _measure_enabled.store (measure_enabled, std::memory_order_release);
    }

    bool send_enabled () const
    {
        return _send_enabled.load (std::memory_order_acquire);
    }

    int current_phase () const
    {
        return _measure_enabled.load (std::memory_order_acquire) ? 1 : 0;
    }

    void reset_measure_metrics ()
    {
        for (size_t i = 0; i < _shards.size (); ++i) {
            _shards[i]->sent_measure.store (0, std::memory_order_relaxed);
            _shards[i]->recv_measure.store (0, std::memory_order_relaxed);
            _shards[i]->gating_violation.store (0, std::memory_order_relaxed);
            std::lock_guard<std::mutex> guard (_shards[i]->latency_lock);
            _shards[i]->latencies_us.clear ();
        }
    }

    void on_sent (int phase, size_t shard)
    {
        ShardCounters &s = *counter_shard (shard);
        if (phase == 1)
            s.sent_measure.fetch_add (1, std::memory_order_relaxed);
        s.pending_total.fetch_add (1, std::memory_order_relaxed);
    }

    void on_recv (int phase, uint64_t sent_ns, size_t shard)
    {
        ShardCounters &s = *counter_shard (shard);
        const long before =
          s.pending_total.fetch_sub (1, std::memory_order_relaxed);
        if (before <= 0) {
            s.gating_violation.fetch_add (1, std::memory_order_relaxed);
        }

        if (phase != 1)
            return;

        s.recv_measure.fetch_add (1, std::memory_order_relaxed);
        if (sent_ns == 0)
            return;

        const uint64_t now = now_ns ();
        const uint64_t delta = now >= sent_ns ? now - sent_ns : 0;
        std::lock_guard<std::mutex> guard (s.latency_lock);
        s.latencies_us.push_back (static_cast<double> (delta) / 1000.0);
    }

    void drop_pending (int n, size_t shard)
    {
        if (n <= 0)
            return;
        counter_shard (shard)->pending_total.fetch_sub (n, std::memory_order_relaxed);
    }

    long pending_total () const
    {
        long total = 0;
        for (size_t i = 0; i < _shards.size (); ++i)
            total += _shards[i]->pending_total.load (std::memory_order_relaxed);
        return total;
    }

    long sent_measure () const
    {
        long total = 0;
        for (size_t i = 0; i < _shards.size (); ++i)
            total += _shards[i]->sent_measure.load (std::memory_order_relaxed);
        return total;
    }

    long recv_measure () const
    {
        long total = 0;
        for (size_t i = 0; i < _shards.size (); ++i)
            total += _shards[i]->recv_measure.load (std::memory_order_relaxed);
        return total;
    }

    long gating_violation () const
    {
        long total = 0;
        for (size_t i = 0; i < _shards.size (); ++i)
            total += _shards[i]->gating_violation.load (
              std::memory_order_relaxed);
        return total;
    }

    std::vector<double> latency_snapshot () const
    {
        std::vector<double> out;
        for (size_t i = 0; i < _shards.size (); ++i) {
            std::lock_guard<std::mutex> guard (_shards[i]->latency_lock);
            out.insert (out.end (), _shards[i]->latencies_us.begin (),
                        _shards[i]->latencies_us.end ());
        }
        return out;
    }

  private:
    struct ShardCounters
    {
        std::atomic<long> pending_total;
        std::atomic<long> sent_measure;
        std::atomic<long> recv_measure;
        std::atomic<long> gating_violation;
        std::vector<double> latencies_us;
        mutable std::mutex latency_lock;

        ShardCounters ()
            : pending_total (0),
              sent_measure (0),
              recv_measure (0),
              gating_violation (0)
        {
        }
    };

    ShardCounters *counter_shard (size_t shard)
    {
        const size_t idx = shard % _shards.size ();
        return _shards[idx].get ();
    }

    const Config &_cfg;
    std::atomic<bool> _send_enabled;
    std::atomic<bool> _measure_enabled;
    std::vector<std::shared_ptr<ShardCounters> > _shards;
};

class ServerSession : public std::enable_shared_from_this<ServerSession>
{
  public:
    ServerSession (tcp::socket socket,
                   bool echo_enabled,
                   ErrorBag &errors,
                   size_t packet_size)
        : _socket (std::move (socket)),
          _echo_enabled (echo_enabled),
          _errors (errors),
          _closed (false),
          _sending (false),
          _send_flush_offset (0),
          _packet_size (packet_size),
          _send_limit (1 * 1024 * 1024)
    {
        const size_t read_cap = std::max<size_t> (64 * 1024, _packet_size * 64);
        _read_buffer.resize (read_cap);
        _send_main.reserve (256 * 1024);
        _send_flush.reserve (256 * 1024);
    }

    void start () { do_read (); }

    void shutdown ()
    {
        std::shared_ptr<ServerSession> self = shared_from_this ();
        boost::asio::post (self->_socket.get_executor (), [self] () {
            self->do_close ();
        });
    }

  private:
    void do_read ()
    {
        auto self = shared_from_this ();
        _socket.async_read_some (
          boost::asio::buffer (_read_buffer),
          [self] (const boost::system::error_code &ec, std::size_t n) {
              if (ec) {
                  self->handle_error (ec);
                  return;
              }

              if (n > 0 && self->_echo_enabled)
                  self->queue_send (&self->_read_buffer[0], n);

              self->do_read ();
          });
    }

    void queue_send (const unsigned char *data, std::size_t size)
    {
        if (size == 0 || _closed)
            return;

        const bool send_required = _send_main.empty () || _send_flush.empty ();
        if ((_send_main.size () + size) > _send_limit) {
            record_error (_errors,
                          static_cast<int> (
                            boost::asio::error::no_buffer_space));
            do_close ();
            return;
        }

        _send_main.insert (_send_main.end (), data, data + size);

        if (!send_required)
            return;

        auto self = shared_from_this ();
        boost::asio::dispatch (self->_socket.get_executor (), [self] () {
            self->do_write ();
        });
    }

    void do_write ()
    {
        if (_sending || _closed)
            return;

        if (_send_flush.empty ()) {
            _send_flush.swap (_send_main);
            _send_flush_offset = 0;
        }

        if (_send_flush.empty ())
            return;

        _sending = true;

        auto self = shared_from_this ();
        _socket.async_write_some (
          boost::asio::buffer (_send_flush.data () + _send_flush_offset,
                               _send_flush.size () - _send_flush_offset),
          [self] (const boost::system::error_code &ec, std::size_t size) {
              self->_sending = false;
              if (ec) {
                  self->handle_error (ec);
                  return;
              }

              if (size > 0) {
                  self->_send_flush_offset += size;
                  if (self->_send_flush_offset >= self->_send_flush.size ()) {
                      self->_send_flush.clear ();
                      self->_send_flush_offset = 0;
                  }
              }

              self->do_write ();
          });
    }

    void handle_error (const boost::system::error_code &ec)
    {
        if (is_ignorable_socket_error (ec))
            return;
        record_error (_errors, ec.value ());
        do_close ();
    }

    void do_close ()
    {
        if (_closed)
            return;
        _closed = true;

        boost::system::error_code ignored;
        _socket.shutdown (tcp::socket::shutdown_both, ignored);
        _socket.close (ignored);
    }

    tcp::socket _socket;
    bool _echo_enabled;
    ErrorBag &_errors;
    bool _closed;
    bool _sending;
    size_t _send_flush_offset;
    size_t _packet_size;
    size_t _send_limit;

    std::vector<unsigned char> _read_buffer;
    std::vector<unsigned char> _send_main;
    std::vector<unsigned char> _send_flush;
};

class EchoServer
{
  public:
    EchoServer (boost::asio::io_context &accept_io,
                std::vector<std::shared_ptr<boost::asio::io_context> > &session_ios,
                const Config &cfg,
                bool echo_enabled,
                ErrorBag &errors)
        : _accept_io (accept_io),
          _session_ios (session_ios),
          _cfg (cfg),
          _acceptor (accept_io),
          _echo_enabled (echo_enabled),
          _errors (errors),
          _running (false),
          _next_session_shard (0)
    {
    }

    bool start ()
    {
        if (_session_ios.empty ()) {
            record_error (
              _errors,
              static_cast<int> (boost::asio::error::invalid_argument));
            return false;
        }

        boost::system::error_code ec;
        const boost::asio::ip::address addr =
          boost::asio::ip::make_address (_cfg.bind_host, ec);
        if (ec) {
            record_error (_errors, ec.value ());
            return false;
        }

        const tcp::endpoint endpoint (addr, static_cast<unsigned short> (_cfg.port));

        _acceptor.open (endpoint.protocol (), ec);
        if (ec) {
            record_error (_errors, ec.value ());
            return false;
        }

        _acceptor.set_option (tcp::acceptor::reuse_address (true), ec);
        if (ec) {
            record_error (_errors, ec.value ());
            return false;
        }

#ifdef SO_REUSEPORT
        typedef boost::asio::detail::socket_option::boolean<SOL_SOCKET,
                                                            SO_REUSEPORT>
          reuse_port_t;
        _acceptor.set_option (reuse_port_t (true), ec);
        if (ec) {
            record_error (_errors, ec.value ());
            return false;
        }
#endif

        _acceptor.bind (endpoint, ec);
        if (ec) {
            record_error (_errors, ec.value ());
            return false;
        }

        _acceptor.listen (std::max (1, _cfg.backlog), ec);
        if (ec) {
            record_error (_errors, ec.value ());
            return false;
        }

        _running.store (true, std::memory_order_release);
        do_accept ();
        return true;
    }

    void stop ()
    {
        _running.store (false, std::memory_order_release);
        boost::system::error_code ignored;
        _acceptor.close (ignored);

        std::lock_guard<std::mutex> guard (_sessions_lock);
        for (size_t i = 0; i < _sessions.size (); ++i)
            _sessions[i]->shutdown ();
        _sessions.clear ();
    }

  private:
    boost::asio::io_context &next_session_io ()
    {
        const size_t shards = _session_ios.size ();
        const size_t idx =
          _next_session_shard.fetch_add (1, std::memory_order_relaxed);
        return *_session_ios[idx % shards];
    }

    void do_accept ()
    {
        boost::asio::io_context &session_io = next_session_io ();
        auto socket = std::make_shared<tcp::socket> (session_io);
        _acceptor.async_accept (
          *socket, [this, socket] (const boost::system::error_code &ec) {
              if (!_running.load (std::memory_order_acquire))
                  return;

              if (ec) {
                  if (ec != boost::asio::error::operation_aborted)
                      record_error (_errors, ec.value ());
              } else {
                  boost::system::error_code sec;
                  if (_cfg.no_delay != 0)
                      socket->set_option (tcp::no_delay (true), sec);
                  if (_cfg.sndbuf > 0) {
                      socket->set_option (
                        boost::asio::socket_base::send_buffer_size (
                          _cfg.sndbuf),
                        sec);
                  }
                  if (_cfg.rcvbuf > 0) {
                      socket->set_option (
                        boost::asio::socket_base::receive_buffer_size (
                          _cfg.rcvbuf),
                        sec);
                  }

                  std::shared_ptr<ServerSession> session (
                    new ServerSession (std::move (*socket), _echo_enabled,
                                       _errors,
                                       static_cast<size_t> (
                                         4 + std::max (9, _cfg.size))));

                  {
                      std::lock_guard<std::mutex> guard (_sessions_lock);
                      _sessions.push_back (session);
                  }
                  session->start ();
              }

              if (_running.load (std::memory_order_acquire))
                  do_accept ();
          });
    }

    boost::asio::io_context &_accept_io;
    std::vector<std::shared_ptr<boost::asio::io_context> > &_session_ios;
    const Config &_cfg;
    tcp::acceptor _acceptor;
    bool _echo_enabled;
    ErrorBag &_errors;
    std::atomic<bool> _running;
    std::atomic<size_t> _next_session_shard;

    std::mutex _sessions_lock;
    std::vector<std::shared_ptr<ServerSession> > _sessions;
};

class ClientSession : public std::enable_shared_from_this<ClientSession>
{
  public:
    typedef std::function<void(bool)> connect_cb_t;

    ClientSession (boost::asio::io_context &io,
                   BenchmarkState &state,
                   const Config &cfg,
                   ErrorBag &errors,
                   size_t shard_id)
        : _socket (io),
          _state (state),
          _cfg (cfg),
          _errors (errors),
          _shard_id (shard_id),
          _connected (false),
          _closed (false),
          _pending (0),
          _sending (false),
          _send_flush_offset (0),
          _body_size (static_cast<size_t> (std::max (9, cfg.size))),
          _packet_size (4 + _body_size),
          _latency_enabled (cfg.latency_sample_rate > 0),
          _sample_rate (std::max (1, cfg.latency_sample_rate)),
          _sample_seq (0)
    {
        _packet_template.resize (_packet_size, 0x33);
        write_u32_be (&_packet_template[0], static_cast<uint32_t> (_body_size));

        _read_buffer.resize (std::max<size_t> (64 * 1024, _packet_size * 64));
        const size_t reserve_size = std::max<size_t> (
          256 * 1024,
          _packet_size
            * static_cast<size_t> (std::max (64, _cfg.inflight * 4)));
        _send_main.reserve (reserve_size);
        _send_flush.reserve (reserve_size);
    }

    void start_connect (const tcp::endpoint &endpoint, const connect_cb_t &cb)
    {
        _connect_cb = cb;

        auto self = shared_from_this ();
        _socket.async_connect (endpoint, [self] (const boost::system::error_code &ec) {
            self->handle_connect (ec);
        });
    }

    void kick_send ()
    {
        std::shared_ptr<ClientSession> self = shared_from_this ();
        boost::asio::post (self->_socket.get_executor (), [self] () {
            self->fill_send_window ();
        });
    }

    void shutdown ()
    {
        std::shared_ptr<ClientSession> self = shared_from_this ();
        boost::asio::post (self->_socket.get_executor (), [self] () {
            self->do_close ();
        });
    }

  private:
    void handle_connect (const boost::system::error_code &ec)
    {
        if (ec) {
            record_error (_errors, ec.value ());
            if (_connect_cb)
                _connect_cb (false);
            return;
        }

        boost::system::error_code sec;
        if (_cfg.no_delay != 0)
            _socket.set_option (tcp::no_delay (true), sec);
        if (_cfg.sndbuf > 0) {
            _socket.set_option (
              boost::asio::socket_base::send_buffer_size (_cfg.sndbuf), sec);
        }
        if (_cfg.rcvbuf > 0) {
            _socket.set_option (
              boost::asio::socket_base::receive_buffer_size (_cfg.rcvbuf), sec);
        }

        _connected = true;
        if (_connect_cb)
            _connect_cb (true);

        do_read ();
        fill_send_window ();
    }

    void do_read ()
    {
        if (!_connected)
            return;

        auto self = shared_from_this ();
        _socket.async_read_some (
          boost::asio::buffer (_read_buffer), [self] (const boost::system::error_code &ec,
                                                      std::size_t n) {
              if (ec) {
                  self->handle_error (ec);
                  return;
              }

              if (n > 0) {
                  self->consume_received (&self->_read_buffer[0], n);

                  if (self->_state.send_enabled ())
                      self->fill_send_window ();
              }

              self->do_read ();
          });
    }

    void consume_received (const unsigned char *data_, size_t size_)
    {
        if (size_ == 0)
            return;

        const unsigned char *cursor = data_;
        size_t remaining = size_;

        if (!_recv_partial.empty ()) {
            const size_t need = _packet_size - _recv_partial.size ();
            const size_t take = std::min (need, remaining);
            _recv_partial.insert (_recv_partial.end (), cursor, cursor + take);
            cursor += take;
            remaining -= take;

            if (_recv_partial.size () == _packet_size) {
                on_packet_received (&_recv_partial[0]);
                _recv_partial.clear ();
            }
        }

        while (remaining >= _packet_size) {
            on_packet_received (cursor);
            cursor += _packet_size;
            remaining -= _packet_size;
        }

        if (remaining > 0)
            _recv_partial.insert (_recv_partial.end (), cursor, cursor + remaining);
    }

    void on_packet_received (const unsigned char *packet_)
    {
        int phase = _state.current_phase ();
        uint64_t sent_ns = 0;
        if (_packet_size >= (4 + 9)) {
            phase = packet_[4];
            sent_ns = read_u64_le (packet_ + 5);
        }
        if (!_latency_enabled)
            sent_ns = 0;

        if (_pending > 0)
            _pending -= 1;

        _state.on_recv (phase, sent_ns, _shard_id);
    }

    bool queue_packet ()
    {
        const int phase = _state.current_phase ();
        uint64_t sent_ns = 0;
        if (_latency_enabled && phase == 1) {
            _sample_seq += 1;
            if (_sample_rate <= 1 || (_sample_seq % static_cast<unsigned> (_sample_rate)) == 0)
                sent_ns = now_ns ();
        }
        const size_t offset = _send_main.size ();
        _send_main.resize (offset + _packet_size);

        memcpy (&_send_main[offset], &_packet_template[0], _packet_size);
        _send_main[offset + 4] = static_cast<unsigned char> (phase);
        write_u64_le (&_send_main[offset + 5], sent_ns);

        _state.on_sent (phase, _shard_id);
        _pending += 1;
        return true;
    }

    void fill_send_window ()
    {
        if (!_connected || !_state.send_enabled ())
            return;

        while (_pending < _cfg.inflight && _state.send_enabled ()) {
            if (!queue_packet ())
                break;
        }

        if (!_sending)
            do_write ();
    }

    void do_write ()
    {
        if (_sending || !_connected || _closed)
            return;

        if (_send_flush.empty ()) {
            _send_flush.swap (_send_main);
            _send_flush_offset = 0;
        }

        if (_send_flush.empty ())
            return;

        _sending = true;

        auto self = shared_from_this ();
        _socket.async_write_some (
          boost::asio::buffer (_send_flush.data () + _send_flush_offset,
                               _send_flush.size () - _send_flush_offset),
          [self] (const boost::system::error_code &ec, std::size_t size) {
              self->_sending = false;
              if (ec) {
                  self->handle_error (ec);
                  return;
              }

              if (size > 0) {
                  self->_send_flush_offset += size;
                  if (self->_send_flush_offset >= self->_send_flush.size ()) {
                      self->_send_flush.clear ();
                      self->_send_flush_offset = 0;
                  }
              }

              self->do_write ();
          });
    }

    void handle_error (const boost::system::error_code &ec)
    {
        if (is_ignorable_socket_error (ec))
            return;
        record_error (_errors, ec.value ());
        do_close ();
    }

    void do_close ()
    {
        if (_closed)
            return;

        _closed = true;
        _connected = false;

        _state.drop_pending (_pending, _shard_id);
        _pending = 0;
        _send_main.clear ();
        _send_flush.clear ();
        _send_flush_offset = 0;
        _recv_partial.clear ();

        boost::system::error_code ignored;
        _socket.shutdown (tcp::socket::shutdown_both, ignored);
        _socket.close (ignored);
    }

    tcp::socket _socket;
    BenchmarkState &_state;
    const Config &_cfg;
    ErrorBag &_errors;
    size_t _shard_id;

    bool _connected;
    bool _closed;
    int _pending;
    bool _sending;
    size_t _send_flush_offset;

    connect_cb_t _connect_cb;

    size_t _body_size;
    size_t _packet_size;
    bool _latency_enabled;
    int _sample_rate;
    unsigned _sample_seq;

    std::vector<unsigned char> _packet_template;
    std::vector<unsigned char> _read_buffer;
    std::vector<unsigned char> _recv_partial;
    std::vector<unsigned char> _send_main;
    std::vector<unsigned char> _send_flush;
};

bool run_s0 (const Config &cfg, ResultRow &row)
{
    fill_common_row (row, cfg);

    ErrorBag errors;
    boost::asio::io_context io;

    bool ok = false;

    try {
        const boost::asio::ip::address addr =
          boost::asio::ip::make_address (cfg.bind_host);
        tcp::acceptor acceptor (io, tcp::endpoint (addr,
                                                   static_cast<unsigned short> (
                                                     cfg.port)));

        std::thread server ([&]() {
            try {
                tcp::socket socket (io);
                acceptor.accept (socket);

                std::array<unsigned char, 4> hdr;
                boost::asio::read (socket, boost::asio::buffer (hdr));
                const uint32_t len = read_u32_be (&hdr[0]);

                std::vector<unsigned char> body (len);
                boost::asio::read (socket, boost::asio::buffer (body));

                boost::asio::write (socket, boost::asio::buffer (hdr));
                boost::asio::write (socket, boost::asio::buffer (body));
            }
            catch (...) {
            }
        });

        tcp::socket client (io);
        client.connect (tcp::endpoint (addr,
                                       static_cast<unsigned short> (cfg.port)));

        const size_t body_size = static_cast<size_t> (std::max (9, cfg.size));
        std::vector<unsigned char> msg (4 + body_size, 0x44);
        write_u32_be (&msg[0], static_cast<uint32_t> (body_size));
        msg[4] = 1;
        write_u64_le (&msg[5], now_ns ());

        boost::asio::write (client, boost::asio::buffer (msg));

        std::array<unsigned char, 4> rh;
        boost::asio::read (client, boost::asio::buffer (rh));
        const uint32_t rlen = read_u32_be (&rh[0]);
        std::vector<unsigned char> rb (rlen);
        boost::asio::read (client, boost::asio::buffer (rb));

        if (rlen == body_size && rb[0] == 1)
            ok = true;

        boost::system::error_code ignored;
        client.shutdown (tcp::socket::shutdown_both, ignored);
        client.close (ignored);

        if (server.joinable ())
            server.join ();
    }
    catch (const std::exception &) {
        ok = false;
    }

    row.connect_success = ok ? 1 : 0;
    row.connect_fail = ok ? 0 : 1;
    row.sent = ok ? 1 : 0;
    row.recv = ok ? 1 : 0;
    row.pass_fail = ok ? "PASS" : "FAIL";

    merge_errors (row.errors_by_errno, errors);
    return ok;
}

bool run_s1_or_s2 (const Config &cfg, ResultRow &row, bool with_send)
{
    fill_common_row (row, cfg);

    ErrorBag errors;
    const int server_io_shards = std::max (1, cfg.io_threads);
    const int client_io_shards =
      std::max (1, std::min (server_io_shards, server_io_shards / 4));
    BenchmarkState state (cfg, client_io_shards);

    boost::asio::io_context server_accept_io;
    std::vector<std::shared_ptr<boost::asio::io_context> > server_session_ios;
    std::vector<std::shared_ptr<io_work_guard_t> >
      server_session_work;
    server_session_ios.reserve (static_cast<size_t> (server_io_shards));
    server_session_work.reserve (static_cast<size_t> (server_io_shards));
    for (int i = 0; i < server_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io (
          new boost::asio::io_context ());
        server_session_work.push_back (
          std::shared_ptr<io_work_guard_t> (
            new io_work_guard_t (io->get_executor ())));
        server_session_ios.push_back (io);
    }

    std::vector<std::shared_ptr<boost::asio::io_context> > client_ios;
    std::vector<std::shared_ptr<io_work_guard_t> > client_work;
    client_ios.reserve (static_cast<size_t> (client_io_shards));
    client_work.reserve (static_cast<size_t> (client_io_shards));
    for (int i = 0; i < client_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io (
          new boost::asio::io_context ());
        client_work.push_back (
          std::shared_ptr<io_work_guard_t> (
            new io_work_guard_t (io->get_executor ())));
        client_ios.push_back (io);
    }

    EchoServer server (
      server_accept_io, server_session_ios, cfg, with_send, errors);
    if (!server.start ()) {
        row.pass_fail = "FAIL";
        merge_errors (row.errors_by_errno, errors);
        return false;
    }

    std::thread server_accept_worker (
      [&]() { server_accept_io.run (); });
    std::vector<std::thread> server_session_workers;
    server_session_workers.reserve (static_cast<size_t> (server_io_shards));
    for (int i = 0; i < server_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io =
          server_session_ios[static_cast<size_t> (i)];
        server_session_workers.push_back (
          std::thread ([io] () { io->run (); }));
    }

    std::vector<std::shared_ptr<ClientSession> > sessions (cfg.ccu);

    std::atomic<int> started (0);
    std::atomic<int> completed (0);
    std::atomic<long> connected (0);
    std::atomic<long> failed (0);

    std::mutex wait_lock;
    std::condition_variable wait_cv;

    const boost::asio::ip::address addr =
      boost::asio::ip::make_address (cfg.bind_host);
    const tcp::endpoint endpoint (addr,
                                  static_cast<unsigned short> (cfg.port));

    std::function<void()> launch_one;
    launch_one = [&]() {
        const int idx = started.fetch_add (1, std::memory_order_relaxed);
        if (idx >= cfg.ccu)
            return;

        boost::asio::io_context &session_io =
          *client_ios[static_cast<size_t> (idx % client_io_shards)];
        const size_t shard_id = static_cast<size_t> (idx % client_io_shards);
        std::shared_ptr<ClientSession> session (
          new ClientSession (session_io, state, cfg, errors, shard_id));
        sessions[idx] = session;

        session->start_connect (endpoint, [&, session] (bool ok) {
            if (ok)
                connected.fetch_add (1, std::memory_order_relaxed);
            else
                failed.fetch_add (1, std::memory_order_relaxed);

            const int done =
              completed.fetch_add (1, std::memory_order_relaxed) + 1;

            if (started.load (std::memory_order_relaxed) < cfg.ccu) {
                const int post_idx = started.load (std::memory_order_relaxed);
                boost::asio::io_context &post_io =
                  *client_ios[static_cast<size_t> (post_idx % client_io_shards)];
                boost::asio::post (post_io, launch_one);
            }

            if (done >= cfg.ccu)
                wait_cv.notify_one ();
        });
    };

    const int concurrency =
      std::max (1, std::min (cfg.connect_concurrency, cfg.ccu));
    for (int i = 0; i < concurrency; ++i)
        boost::asio::post (
                           *client_ios[static_cast<size_t> (i % client_io_shards)],
                           launch_one);

    std::vector<std::thread> client_workers;
    client_workers.reserve (static_cast<size_t> (client_io_shards));
    for (int i = 0; i < client_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io = client_ios[static_cast<size_t> (i)];
        client_workers.push_back (std::thread ([io] () { io->run (); }));
    }

    const auto connect_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (5, cfg.connect_timeout_sec));

    {
        std::unique_lock<std::mutex> lock (wait_lock);
        wait_cv.wait_until (lock, connect_deadline, [&]() {
            return completed.load (std::memory_order_relaxed) >= cfg.ccu;
        });
    }

    row.connect_success = connected.load (std::memory_order_relaxed);
    row.connect_fail = failed.load (std::memory_order_relaxed);
    row.connect_timeout = cfg.ccu - completed.load (std::memory_order_relaxed);

    bool ok = row.connect_success == cfg.ccu && row.connect_fail == 0
              && row.connect_timeout == 0;

    if (with_send && ok) {
        const bool markerless_phase = cfg.latency_sample_rate == 0;
        auto wait_for_drain = [&](long &timeout_flag) {
            const auto deadline =
              std::chrono::steady_clock::now ()
              + std::chrono::seconds (std::max (1, cfg.drain_timeout_sec));
            while (state.pending_total () > 0
                   && std::chrono::steady_clock::now () < deadline) {
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
            if (state.pending_total () > 0)
                timeout_flag = 1;
        };

        state.set_phase (true, false);
        for (size_t i = 0; i < sessions.size (); ++i)
            if (sessions[i])
                sessions[i]->kick_send ();

        if (cfg.warmup_sec > 0)
            std::this_thread::sleep_for (
              std::chrono::seconds (cfg.warmup_sec));

        if (markerless_phase) {
            long warmup_drain_timeout = 0;
            state.set_phase (false, false);
            wait_for_drain (warmup_drain_timeout);
            if (warmup_drain_timeout > 0)
                row.drain_timeout_count = 1;
        }

        state.reset_measure_metrics ();
        state.set_phase (true, true);
        for (size_t i = 0; i < sessions.size (); ++i)
            if (sessions[i])
                sessions[i]->kick_send ();

        std::this_thread::sleep_for (
          std::chrono::seconds (std::max (1, cfg.measure_sec)));

        state.set_phase (false, markerless_phase);
        wait_for_drain (row.drain_timeout_count);
        if (markerless_phase)
            state.set_phase (false, false);

        row.sent = state.sent_measure ();
        row.recv = state.recv_measure ();
        row.incomplete_ratio =
          row.sent > 0 ? static_cast<double> (row.sent - row.recv) / row.sent
                       : 0.0;
        row.throughput = cfg.measure_sec > 0
                           ? static_cast<double> (row.recv) / cfg.measure_sec
                           : 0.0;
        row.gating_violation = state.gating_violation ();

        const std::vector<double> lats = state.latency_snapshot ();
        row.p50 = percentile (lats, 0.50);
        row.p95 = percentile (lats, 0.95);
        row.p99 = percentile (lats, 0.99);

        ok = ok && row.recv > 0 && row.incomplete_ratio <= 0.01
             && row.drain_timeout_count == 0 && row.gating_violation == 0;
    }

    for (size_t i = 0; i < sessions.size (); ++i) {
        if (sessions[i])
            sessions[i]->shutdown ();
    }

    server.stop ();
    client_work.clear ();
    server_session_work.clear ();
    for (size_t i = 0; i < client_ios.size (); ++i)
        client_ios[i]->stop ();
    for (size_t i = 0; i < server_session_ios.size (); ++i)
        server_session_ios[i]->stop ();
    server_accept_io.stop ();

    for (size_t i = 0; i < client_workers.size (); ++i) {
        if (client_workers[i].joinable ())
            client_workers[i].join ();
    }

    for (size_t i = 0; i < server_session_workers.size (); ++i) {
        if (server_session_workers[i].joinable ())
            server_session_workers[i].join ();
    }
    if (server_accept_worker.joinable ())
        server_accept_worker.join ();

    row.pass_fail = ok ? "PASS" : "FAIL";
    merge_errors (row.errors_by_errno, errors);
    return ok;
}

bool run_client_only (const Config &cfg, ResultRow &row, bool with_send)
{
    fill_common_row (row, cfg);

    ErrorBag errors;
    const int client_io_shards = std::max (1, cfg.io_threads);
    BenchmarkState state (cfg, client_io_shards);

    std::vector<std::shared_ptr<boost::asio::io_context> > client_ios;
    std::vector<std::shared_ptr<io_work_guard_t> > client_work;
    client_ios.reserve (static_cast<size_t> (client_io_shards));
    client_work.reserve (static_cast<size_t> (client_io_shards));
    for (int i = 0; i < client_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io (
          new boost::asio::io_context ());
        client_work.push_back (
          std::shared_ptr<io_work_guard_t> (
            new io_work_guard_t (io->get_executor ())));
        client_ios.push_back (io);
    }

    std::vector<std::shared_ptr<ClientSession> > sessions (cfg.ccu);

    std::atomic<int> started (0);
    std::atomic<int> completed (0);
    std::atomic<long> connected (0);
    std::atomic<long> failed (0);

    std::mutex wait_lock;
    std::condition_variable wait_cv;

    const boost::asio::ip::address addr =
      boost::asio::ip::make_address (cfg.bind_host);
    const tcp::endpoint endpoint (addr,
                                  static_cast<unsigned short> (cfg.port));

    std::function<void()> launch_one;
    launch_one = [&]() {
        const int idx = started.fetch_add (1, std::memory_order_relaxed);
        if (idx >= cfg.ccu)
            return;

        boost::asio::io_context &session_io =
          *client_ios[static_cast<size_t> (idx % client_io_shards)];
        const size_t shard_id = static_cast<size_t> (idx % client_io_shards);
        std::shared_ptr<ClientSession> session (
          new ClientSession (session_io, state, cfg, errors, shard_id));
        sessions[idx] = session;

        session->start_connect (endpoint, [&, session] (bool ok) {
            if (ok)
                connected.fetch_add (1, std::memory_order_relaxed);
            else
                failed.fetch_add (1, std::memory_order_relaxed);

            const int done =
              completed.fetch_add (1, std::memory_order_relaxed) + 1;

            if (started.load (std::memory_order_relaxed) < cfg.ccu) {
                const int post_idx = started.load (std::memory_order_relaxed);
                boost::asio::io_context &post_io =
                  *client_ios[static_cast<size_t> (post_idx % client_io_shards)];
                boost::asio::post (post_io, launch_one);
            }

            if (done >= cfg.ccu)
                wait_cv.notify_one ();
        });
    };

    const int concurrency =
      std::max (1, std::min (cfg.connect_concurrency, cfg.ccu));
    for (int i = 0; i < concurrency; ++i)
        boost::asio::post (
          *client_ios[static_cast<size_t> (i % client_io_shards)], launch_one);

    std::vector<std::thread> client_workers;
    client_workers.reserve (static_cast<size_t> (client_io_shards));
    for (int i = 0; i < client_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io =
          client_ios[static_cast<size_t> (i)];
        client_workers.push_back (std::thread ([io] () { io->run (); }));
    }

    const auto connect_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (5, cfg.connect_timeout_sec));

    {
        std::unique_lock<std::mutex> lock (wait_lock);
        wait_cv.wait_until (lock, connect_deadline, [&]() {
            return completed.load (std::memory_order_relaxed) >= cfg.ccu;
        });
    }

    row.connect_success = connected.load (std::memory_order_relaxed);
    row.connect_fail = failed.load (std::memory_order_relaxed);
    row.connect_timeout = cfg.ccu - completed.load (std::memory_order_relaxed);

    bool ok = row.connect_success == cfg.ccu && row.connect_fail == 0
              && row.connect_timeout == 0;

    if (with_send && ok) {
        const bool markerless_phase = cfg.latency_sample_rate == 0;
        auto wait_for_drain = [&](long &timeout_flag) {
            const auto deadline =
              std::chrono::steady_clock::now ()
              + std::chrono::seconds (std::max (1, cfg.drain_timeout_sec));
            while (state.pending_total () > 0
                   && std::chrono::steady_clock::now () < deadline) {
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
            if (state.pending_total () > 0)
                timeout_flag = 1;
        };

        state.set_phase (true, false);
        for (size_t i = 0; i < sessions.size (); ++i)
            if (sessions[i])
                sessions[i]->kick_send ();

        if (cfg.warmup_sec > 0)
            std::this_thread::sleep_for (
              std::chrono::seconds (cfg.warmup_sec));

        if (markerless_phase) {
            long warmup_drain_timeout = 0;
            state.set_phase (false, false);
            wait_for_drain (warmup_drain_timeout);
            if (warmup_drain_timeout > 0)
                row.drain_timeout_count = 1;
        }

        state.reset_measure_metrics ();
        state.set_phase (true, true);
        for (size_t i = 0; i < sessions.size (); ++i)
            if (sessions[i])
                sessions[i]->kick_send ();

        std::this_thread::sleep_for (
          std::chrono::seconds (std::max (1, cfg.measure_sec)));

        state.set_phase (false, markerless_phase);
        wait_for_drain (row.drain_timeout_count);
        if (markerless_phase)
            state.set_phase (false, false);

        row.sent = state.sent_measure ();
        row.recv = state.recv_measure ();
        row.incomplete_ratio =
          row.sent > 0 ? static_cast<double> (row.sent - row.recv) / row.sent
                       : 0.0;
        row.throughput = cfg.measure_sec > 0
                           ? static_cast<double> (row.recv) / cfg.measure_sec
                           : 0.0;
        row.gating_violation = state.gating_violation ();

        const std::vector<double> lats = state.latency_snapshot ();
        row.p50 = percentile (lats, 0.50);
        row.p95 = percentile (lats, 0.95);
        row.p99 = percentile (lats, 0.99);

        ok = ok && row.recv > 0 && row.incomplete_ratio <= 0.01
             && row.drain_timeout_count == 0 && row.gating_violation == 0;
    }

    for (size_t i = 0; i < sessions.size (); ++i) {
        if (sessions[i])
            sessions[i]->shutdown ();
    }

    client_work.clear ();
    for (size_t i = 0; i < client_ios.size (); ++i)
        client_ios[i]->stop ();

    for (size_t i = 0; i < client_workers.size (); ++i) {
        if (client_workers[i].joinable ())
            client_workers[i].join ();
    }

    row.pass_fail = ok ? "PASS" : "FAIL";
    merge_errors (row.errors_by_errno, errors);
    return ok;
}

bool run_server_only (const Config &cfg, ResultRow &row, bool with_send)
{
    fill_common_row (row, cfg);

    ErrorBag errors;
    const int server_io_shards = std::max (1, cfg.io_threads);

    boost::asio::io_context server_accept_io;
    std::vector<std::shared_ptr<boost::asio::io_context> > server_session_ios;
    std::vector<std::shared_ptr<io_work_guard_t> > server_session_work;
    server_session_ios.reserve (static_cast<size_t> (server_io_shards));
    server_session_work.reserve (static_cast<size_t> (server_io_shards));
    for (int i = 0; i < server_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io (
          new boost::asio::io_context ());
        server_session_work.push_back (
          std::shared_ptr<io_work_guard_t> (
            new io_work_guard_t (io->get_executor ())));
        server_session_ios.push_back (io);
    }

    EchoServer server (
      server_accept_io, server_session_ios, cfg, with_send, errors);
    if (!server.start ()) {
        row.pass_fail = "FAIL";
        merge_errors (row.errors_by_errno, errors);
        return false;
    }

    std::thread server_accept_worker (
      [&]() { server_accept_io.run (); });
    std::vector<std::thread> server_session_workers;
    server_session_workers.reserve (static_cast<size_t> (server_io_shards));
    for (int i = 0; i < server_io_shards; ++i) {
        std::shared_ptr<boost::asio::io_context> io =
          server_session_ios[static_cast<size_t> (i)];
        server_session_workers.push_back (
          std::thread ([io] () { io->run (); }));
    }

    g_server_stop_requested.store (false, std::memory_order_release);
    std::signal (SIGINT, handle_server_stop_signal);
    std::signal (SIGTERM, handle_server_stop_signal);

    while (!g_server_stop_requested.load (std::memory_order_acquire))
        std::this_thread::sleep_for (std::chrono::milliseconds (100));

    server.stop ();
    server_session_work.clear ();
    for (size_t i = 0; i < server_session_ios.size (); ++i)
        server_session_ios[i]->stop ();
    server_accept_io.stop ();

    for (size_t i = 0; i < server_session_workers.size (); ++i) {
        if (server_session_workers[i].joinable ())
            server_session_workers[i].join ();
    }
    if (server_accept_worker.joinable ())
        server_accept_worker.join ();

    const bool ok = errors.by_errno.empty ();
    row.pass_fail = ok ? "PASS" : "FAIL";
    merge_errors (row.errors_by_errno, errors);
    return ok;
}

void print_row (const ResultRow &row)
{
    printf (
      "RESULT scenario=%s transport=%s ccu=%d size=%d inflight=%d connect_success=%ld connect_fail=%ld connect_timeout=%ld sent=%ld recv=%ld incomplete_ratio=%.6f throughput=%.2f p50_us=%.2f p95_us=%.2f p99_us=%.2f drain_timeout=%ld gating_violation=%ld pass_fail=%s\n",
      row.scenario_id.c_str (), row.transport.c_str (), row.ccu, row.size,
      row.inflight, row.connect_success, row.connect_fail, row.connect_timeout,
      row.sent, row.recv, row.incomplete_ratio, row.throughput, row.p50,
      row.p95, row.p99, row.drain_timeout_count, row.gating_violation,
      row.pass_fail.c_str ());

    printf ("METRIC scenario_id=%s transport=%s ccu=%d inflight=%d size=%d "
            "connect_success=%ld connect_fail=%ld connect_timeout=%ld sent=%ld "
            "recv=%ld incomplete_ratio=%.6f throughput=%.2f p50=%.2f p95=%.2f "
            "p99=%.2f errors_by_errno=%s pass_fail=%s\n",
            row.scenario_id.c_str (), row.transport.c_str (), row.ccu,
            row.inflight, row.size, row.connect_success, row.connect_fail,
            row.connect_timeout, row.sent, row.recv, row.incomplete_ratio,
            row.throughput, row.p50, row.p95, row.p99,
            errors_to_string (row.errors_by_errno).c_str (),
            row.pass_fail.c_str ());
}

void append_csv (const std::string &path, const ResultRow &row)
{
    if (path.empty ())
        return;

    FILE *f = fopen (path.c_str (), "rb");
    const bool exists = f != NULL;
    if (f)
        fclose (f);

    f = fopen (path.c_str (), exists ? "ab" : "wb");
    if (!f)
        return;

    if (!exists) {
        fprintf (
          f,
          "scenario_id,transport,ccu,inflight,size,connect_success,connect_fail,connect_timeout,sent,recv,incomplete_ratio,throughput,p50,p95,p99,errors_by_errno,pass_fail\n");
    }

    fprintf (
      f,
      "%s,%s,%d,%d,%d,%ld,%ld,%ld,%ld,%ld,%.6f,%.2f,%.2f,%.2f,%.2f,\"%s\",%s\n",
      row.scenario_id.c_str (), row.transport.c_str (), row.ccu, row.inflight,
      row.size, row.connect_success, row.connect_fail, row.connect_timeout,
      row.sent, row.recv, row.incomplete_ratio, row.throughput, row.p50,
      row.p95, row.p99, errors_to_string (row.errors_by_errno).c_str (),
      row.pass_fail.c_str ());

    fclose (f);
}

void write_summary_json (const std::string &path, const ResultRow &row)
{
    if (path.empty ())
        return;

    FILE *f = fopen (path.c_str (), "wb");
    if (!f)
        return;

    fprintf (f,
             "{\n"
             "  \"scenario_id\": \"%s\",\n"
             "  \"transport\": \"%s\",\n"
             "  \"ccu\": %d,\n"
             "  \"inflight\": %d,\n"
             "  \"size\": %d,\n"
             "  \"connect_success\": %ld,\n"
             "  \"connect_fail\": %ld,\n"
             "  \"connect_timeout\": %ld,\n"
             "  \"sent\": %ld,\n"
             "  \"recv\": %ld,\n"
             "  \"incomplete_ratio\": %.6f,\n"
             "  \"throughput\": %.2f,\n"
             "  \"p50\": %.2f,\n"
             "  \"p95\": %.2f,\n"
             "  \"p99\": %.2f,\n"
             "  \"errors_by_errno\": \"%s\",\n"
             "  \"pass_fail\": \"%s\"\n"
             "}\n",
             row.scenario_id.c_str (), row.transport.c_str (), row.ccu,
             row.inflight, row.size, row.connect_success, row.connect_fail,
             row.connect_timeout, row.sent, row.recv, row.incomplete_ratio,
             row.throughput, row.p50, row.p95, row.p99,
             errors_to_string (row.errors_by_errno).c_str (),
             row.pass_fail.c_str ());

    fclose (f);
}

void print_usage (const char *prog)
{
    printf ("Usage: %s --scenario s0|s1|s2 [options]\n", prog);
    printf ("Options:\n");
    printf ("  --transport tcp                 (cs fastpath supports tcp only)\n");
    printf ("  --port N                        (default 27110)\n");
    printf ("  --ccu N                         (default 10000)\n");
    printf ("  --size N                        (default 1024)\n");
    printf ("  --inflight N                    (per-connection, default 30)\n");
    printf ("  --warmup N                      (default 3 sec)\n");
    printf ("  --measure N                     (default 10 sec)\n");
    printf ("  --drain-timeout N               (default 10 sec)\n");
    printf ("  --connect-concurrency N         (default 256)\n");
    printf ("  --connect-timeout N             (default 10 sec)\n");
    printf ("  --connect-retries N             (accepted, default 3)\n");
    printf ("  --connect-retry-delay-ms N      (accepted, default 100)\n");
    printf ("  --backlog N                     (default 32768)\n");
    printf ("  --hwm N                         (accepted, default 1000000)\n");
    printf ("  --sndbuf N                      (default 262144)\n");
    printf ("  --rcvbuf N                      (default 262144)\n");
    printf ("  --no-delay 0|1                  (default 1)\n");
    printf ("  --io-threads N                  (default 1)\n");
    printf ("  --send-batch N                  (default 1)\n");
    printf ("  --latency-sample-rate N         (default 1, 0=disable latency)\n");
    printf ("  --role both|server|client       (default both)\n");
    printf ("  --scenario-id ID                override scenario_id output\n");
    printf ("  --metrics-csv PATH              append row to csv\n");
    printf ("  --summary-json PATH             write row json\n");
}

} // namespace

int main (int argc, char **argv)
{
    setup_test_environment (0);

    Config cfg;
    cfg.scenario = arg_str (argc, argv, "--scenario", cfg.scenario.c_str ());
    cfg.transport =
      arg_str (argc, argv, "--transport", cfg.transport.c_str ());
    cfg.bind_host =
      arg_str (argc, argv, "--bind-host", cfg.bind_host.c_str ());
    cfg.port = arg_int (argc, argv, "--port", cfg.port);
    cfg.ccu = arg_int (argc, argv, "--ccu", cfg.ccu);
    cfg.size = arg_int (argc, argv, "--size", cfg.size);
    cfg.inflight = arg_int (argc, argv, "--inflight", cfg.inflight);
    cfg.warmup_sec = arg_int (argc, argv, "--warmup", cfg.warmup_sec);
    cfg.measure_sec = arg_int (argc, argv, "--measure", cfg.measure_sec);
    cfg.drain_timeout_sec =
      arg_int (argc, argv, "--drain-timeout", cfg.drain_timeout_sec);
    cfg.connect_concurrency =
      arg_int (argc, argv, "--connect-concurrency", cfg.connect_concurrency);
    cfg.connect_timeout_sec =
      arg_int (argc, argv, "--connect-timeout", cfg.connect_timeout_sec);
    cfg.connect_retries =
      arg_int (argc, argv, "--connect-retries", cfg.connect_retries);
    cfg.connect_retry_delay_ms = arg_int (argc, argv, "--connect-retry-delay-ms",
                                          cfg.connect_retry_delay_ms);
    cfg.backlog = arg_int (argc, argv, "--backlog", cfg.backlog);
    cfg.hwm = arg_int (argc, argv, "--hwm", cfg.hwm);
    cfg.sndbuf = arg_int (argc, argv, "--sndbuf", cfg.sndbuf);
    cfg.rcvbuf = arg_int (argc, argv, "--rcvbuf", cfg.rcvbuf);
    cfg.no_delay = arg_int (argc, argv, "--no-delay", cfg.no_delay);
    cfg.io_threads = arg_int (argc, argv, "--io-threads", cfg.io_threads);
    cfg.send_batch = arg_int (argc, argv, "--send-batch", cfg.send_batch);
    cfg.latency_sample_rate = arg_int (argc, argv, "--latency-sample-rate",
                                       cfg.latency_sample_rate);
    cfg.role = arg_str (argc, argv, "--role", cfg.role.c_str ());
    cfg.scenario_id_override = arg_str (argc, argv, "--scenario-id", "");
    cfg.metrics_csv = arg_str (argc, argv, "--metrics-csv", "");
    cfg.summary_json = arg_str (argc, argv, "--summary-json", "");

    if (has_arg (argc, argv, "--help")
        || (cfg.scenario != "s0" && cfg.scenario != "s1" && cfg.scenario != "s2")) {
        print_usage (argv[0]);
        return cfg.scenario == "s0" || cfg.scenario == "s1" || cfg.scenario == "s2"
                 ? 0
                 : 2;
    }

    cfg.ccu = std::max (1, cfg.ccu);
    cfg.size = std::max (16, cfg.size);
    cfg.inflight = std::max (1, cfg.inflight);
    cfg.connect_concurrency = std::max (1, cfg.connect_concurrency);
    cfg.connect_timeout_sec = std::max (1, cfg.connect_timeout_sec);
    cfg.connect_retries = std::max (1, cfg.connect_retries);
    cfg.connect_retry_delay_ms = std::max (0, cfg.connect_retry_delay_ms);
    cfg.hwm = std::max (1, cfg.hwm);
    cfg.send_batch = std::max (1, cfg.send_batch);
    cfg.latency_sample_rate = std::max (0, cfg.latency_sample_rate);
    cfg.no_delay = cfg.no_delay != 0 ? 1 : 0;

    const bool role_ok =
      cfg.role == "both" || cfg.role == "server" || cfg.role == "client";
    if (!role_ok) {
        print_usage (argv[0]);
        return 2;
    }

    ResultRow row;
    fill_common_row (row, cfg);

    if (cfg.transport != "tcp") {
        row.pass_fail = "SKIP";
        print_row (row);
        append_csv (cfg.metrics_csv, row);
        write_summary_json (cfg.summary_json, row);
        fprintf (stderr,
                 "[stream-zlink-csfast] transport '%s' skipped (tcp only)\n",
                 cfg.transport.c_str ());
        return 0;
    }

    bool ok = false;
    if (cfg.scenario == "s0") {
        if (cfg.role != "both") {
            row.pass_fail = "FAIL";
            print_row (row);
            append_csv (cfg.metrics_csv, row);
            write_summary_json (cfg.summary_json, row);
            return 2;
        }
        ok = run_s0 (cfg, row);
    } else if (cfg.scenario == "s1") {
        if (cfg.role == "server")
            ok = run_server_only (cfg, row, false);
        else if (cfg.role == "client")
            ok = run_client_only (cfg, row, false);
        else
            ok = run_s1_or_s2 (cfg, row, false);
    } else {
        if (cfg.role == "server")
            ok = run_server_only (cfg, row, true);
        else if (cfg.role == "client")
            ok = run_client_only (cfg, row, true);
        else
            ok = run_s1_or_s2 (cfg, row, true);
    }

    print_row (row);
    append_csv (cfg.metrics_csv, row);
    write_summary_json (cfg.summary_json, row);

    return ok ? 0 : 2;
}
