#include "../common/stream_echo_common.hpp"

#include <boost/asio.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using boost::asio::ip::tcp;

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;

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

    server_options_t ()
        : host ("0.0.0.0"),
          port (38001),
          size (1024),
          sndbuf (1024 * 1024),
          rcvbuf (1024 * 1024),
          backlog (32768),
          tcp_nodelay (1),
          io_threads (8)
    {
    }
};

class asio_echo_server_t;

class asio_session_t : public std::enable_shared_from_this<asio_session_t>
{
  public:
    asio_session_t (asio_echo_server_t &owner_, boost::asio::io_context &io_);

    tcp::socket &socket_ref () { return socket; }
    void start ();
    void close ();

  private:
    void start_read_header ();
    void on_read_header (const boost::system::error_code &ec, size_t bytes);
    void start_read_body ();
    void on_read_body (const boost::system::error_code &ec, size_t bytes);
    void start_write_if_needed ();
    void on_write (const boost::system::error_code &ec, size_t bytes);
    void close_internal ();

    bool queue_push (size_t slot);
    bool queue_empty () const;
    size_t queue_front () const;
    size_t queue_pop ();
    unsigned char *frame_ptr (size_t slot);

    asio_echo_server_t &owner;
    tcp::socket socket;
    boost::asio::strand<boost::asio::io_context::executor_type> strand;

    bool closed;
    bool writing;
    bool read_paused;

    unsigned char read_header[4];
    size_t current_read_slot;
    size_t current_read_len;

    const size_t frame_size;
    const size_t slot_count;
    std::vector<unsigned char> slot_storage;
    std::vector<size_t> free_slots;
    std::vector<size_t> queue_slots;
    size_t queue_head;
    size_t queue_tail;
    size_t queue_count;
};

class asio_echo_server_t
{
  public:
    explicit asio_echo_server_t (const server_options_t &opt_)
        : opt (opt_),
          io (),
          work_guard (boost::asio::make_work_guard (io)),
          acceptor (io),
          signals (io, SIGINT, SIGTERM),
          active_connections (0),
          recv_msgs (0),
          parse_error (0),
          protocol_error (0),
          send_error (0),
          stopping (false)
    {
    }

    int run ()
    {
        boost::system::error_code ec;
        const boost::asio::ip::address addr =
          boost::asio::ip::make_address (opt.host, ec);
        if (ec) {
            std::fprintf (stderr, "asio server: invalid host %s\n", opt.host.c_str ());
            return 2;
        }

        const tcp::endpoint ep (addr, static_cast<unsigned short> (opt.port));
        acceptor.open (ep.protocol (), ec);
        if (ec) {
            std::fprintf (stderr, "asio server: acceptor.open failed: %s\n",
                          ec.message ().c_str ());
            return 2;
        }

        acceptor.set_option (tcp::acceptor::reuse_address (true), ec);
        acceptor.bind (ep, ec);
        if (ec) {
            std::fprintf (stderr, "asio server: bind failed: %s\n", ec.message ().c_str ());
            return 2;
        }

        acceptor.listen (opt.backlog, ec);
        if (ec) {
            std::fprintf (stderr, "asio server: listen failed: %s\n", ec.message ().c_str ());
            return 2;
        }

        signals.async_wait ([this] (const boost::system::error_code &, int) {
            stop ();
        });

        start_accept ();

        const int worker_count = std::max (1, opt.io_threads);
        for (int i = 0; i < worker_count; ++i)
            workers.push_back (std::thread ([this] () { io.run (); }));

        for (size_t i = 0; i < workers.size (); ++i) {
            if (workers[i].joinable ())
                workers[i].join ();
        }

        std::printf (
          "%s\n",
          stream_echo::make_metric_line (
            "asio", opt.size, recv_msgs.load (std::memory_order_relaxed),
            parse_error.load (std::memory_order_relaxed),
            protocol_error.load (std::memory_order_relaxed),
            send_error.load (std::memory_order_relaxed),
            active_connections.load (std::memory_order_relaxed))
            .c_str ());

        return 0;
    }

    void stop ()
    {
        if (stopping.exchange (true, std::memory_order_acq_rel))
            return;

        boost::system::error_code ec;
        acceptor.cancel (ec);
        acceptor.close (ec);
        work_guard.reset ();
        io.stop ();
    }

    void on_session_open ()
    {
        active_connections.fetch_add (1, std::memory_order_relaxed);
    }

    void on_session_close ()
    {
        if (active_connections.load (std::memory_order_relaxed) > 0)
            active_connections.fetch_sub (1, std::memory_order_relaxed);
    }

    void on_recv_frame (size_t payload_size)
    {
        (void)payload_size;
    }

    void on_parse_error ()
    {
    }

    void on_protocol_error ()
    {
    }

    void on_send_error ()
    {
    }

    void apply_socket_tuning (tcp::socket &socket)
    {
        boost::system::error_code ec;
        socket.set_option (
          boost::asio::ip::tcp::no_delay (opt.tcp_nodelay != 0), ec);
        boost::asio::socket_base::send_buffer_size snd (opt.sndbuf);
        socket.set_option (snd, ec);
        boost::asio::socket_base::receive_buffer_size rcv (opt.rcvbuf);
        socket.set_option (rcv, ec);
    }

    size_t payload_size () const { return opt.size; }

    size_t frame_size () const { return 4 + opt.size; }

    size_t slot_count () const
    {
        const size_t per_slot = frame_size ();
        const size_t session_budget = 64 * 1024;
        size_t slots = session_budget / per_slot;
        if (slots < 1)
            slots = 1;
        if (slots > 16)
            slots = 16;
        return slots;
    }

  private:
    void start_accept ()
    {
        if (stopping.load (std::memory_order_acquire))
            return;

        std::shared_ptr<asio_session_t> session =
          std::make_shared<asio_session_t> (*this, io);
        acceptor.async_accept (
          session->socket_ref (),
          [this, session] (const boost::system::error_code &ec) {
              if (!ec) {
                  apply_socket_tuning (session->socket_ref ());
                  session->start ();
              }
              if (!stopping.load (std::memory_order_acquire))
                  start_accept ();
          });
    }

    server_options_t opt;
    boost::asio::io_context io;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      work_guard;
    tcp::acceptor acceptor;
    boost::asio::signal_set signals;
    std::vector<std::thread> workers;

    std::atomic<long> active_connections;
    std::atomic<long> recv_msgs;
    std::atomic<long> parse_error;
    std::atomic<long> protocol_error;
    std::atomic<long> send_error;
    std::atomic<bool> stopping;
};

asio_session_t::asio_session_t (asio_echo_server_t &owner_, boost::asio::io_context &io_)
    : owner (owner_),
      socket (io_),
      strand (boost::asio::make_strand (io_)),
      closed (false),
      writing (false),
      read_paused (false),
      current_read_slot (0),
      current_read_len (0),
      frame_size (owner_.frame_size ()),
      slot_count (owner_.slot_count ()),
      slot_storage (frame_size * slot_count, 0),
      free_slots (),
      queue_slots (slot_count, 0),
      queue_head (0),
      queue_tail (0),
      queue_count (0)
{
    std::memset (read_header, 0, sizeof (read_header));
    free_slots.reserve (slot_count);
    for (size_t i = 0; i < slot_count; ++i) {
        stream_echo::store_u32_be (
          frame_ptr (i), static_cast<uint32_t> (owner.payload_size ()));
        free_slots.push_back (slot_count - 1 - i);
    }
}

unsigned char *asio_session_t::frame_ptr (size_t slot)
{
    return &slot_storage[slot * frame_size];
}

bool asio_session_t::queue_push (size_t slot)
{
    if (queue_count >= slot_count)
        return false;
    queue_slots[queue_tail] = slot;
    queue_tail = (queue_tail + 1) % slot_count;
    ++queue_count;
    return true;
}

bool asio_session_t::queue_empty () const
{
    return queue_count == 0;
}

size_t asio_session_t::queue_front () const
{
    return queue_slots[queue_head];
}

size_t asio_session_t::queue_pop ()
{
    const size_t slot = queue_slots[queue_head];
    queue_head = (queue_head + 1) % slot_count;
    --queue_count;
    return slot;
}

void asio_session_t::start ()
{
    owner.on_session_open ();
    start_read_header ();
}

void asio_session_t::start_read_header ()
{
    if (closed)
        return;

    if (free_slots.empty ()) {
        read_paused = true;
        return;
    }

    read_paused = false;
    const std::shared_ptr<asio_session_t> self = shared_from_this ();
    boost::asio::async_read (
      socket, boost::asio::buffer (read_header, sizeof (read_header)),
      boost::asio::bind_executor (
        strand,
        [self] (const boost::system::error_code &ec, size_t bytes) {
            self->on_read_header (ec, bytes);
        }));
}

void asio_session_t::on_read_header (const boost::system::error_code &ec, size_t bytes)
{
    if (closed)
        return;

    if (ec || bytes != sizeof (read_header)) {
        close_internal ();
        return;
    }

    const size_t len =
      static_cast<size_t> (stream_echo::load_u32_be (read_header));
    if (len < k_min_payload_size || len > k_max_payload_size) {
        owner.on_parse_error ();
        close_internal ();
        return;
    }
    if (len != owner.payload_size ()) {
        owner.on_protocol_error ();
        close_internal ();
        return;
    }

    current_read_len = len;
    current_read_slot = free_slots.back ();
    free_slots.pop_back ();
    start_read_body ();
}

void asio_session_t::start_read_body ()
{
    if (closed)
        return;

    unsigned char *frame = frame_ptr (current_read_slot);
    const std::shared_ptr<asio_session_t> self = shared_from_this ();
    boost::asio::async_read (
      socket, boost::asio::buffer (frame + 4, current_read_len),
      boost::asio::bind_executor (
        strand,
        [self] (const boost::system::error_code &ec, size_t bytes) {
            self->on_read_body (ec, bytes);
        }));
}

void asio_session_t::on_read_body (const boost::system::error_code &ec, size_t bytes)
{
    if (closed)
        return;

    if (ec || bytes != current_read_len) {
        close_internal ();
        return;
    }

    owner.on_recv_frame (current_read_len);

    if (!queue_push (current_read_slot)) {
        owner.on_send_error ();
        close_internal ();
        return;
    }

    start_write_if_needed ();
    start_read_header ();
}

void asio_session_t::start_write_if_needed ()
{
    if (closed || writing || queue_empty ())
        return;

    const size_t slot = queue_front ();
    writing = true;
    unsigned char *frame = frame_ptr (slot);
    const std::shared_ptr<asio_session_t> self = shared_from_this ();
    boost::asio::async_write (
      socket, boost::asio::buffer (frame, frame_size),
      boost::asio::bind_executor (
        strand,
        [self] (const boost::system::error_code &ec, size_t bytes) {
            self->on_write (ec, bytes);
        }));
}

void asio_session_t::on_write (const boost::system::error_code &ec, size_t bytes)
{
    writing = false;

    if (closed)
        return;

    if (ec || bytes != frame_size || queue_empty ()) {
        owner.on_send_error ();
        close_internal ();
        return;
    }

    const size_t released = queue_pop ();
    free_slots.push_back (released);

    start_write_if_needed ();
    if (read_paused)
        start_read_header ();
}

void asio_session_t::close ()
{
    const std::shared_ptr<asio_session_t> self = shared_from_this ();
    boost::asio::post (strand, [self] () { self->close_internal (); });
}

void asio_session_t::close_internal ()
{
    if (closed)
        return;
    closed = true;

    boost::system::error_code ec;
    socket.cancel (ec);
    socket.shutdown (tcp::socket::shutdown_both, ec);
    socket.close (ec);

    owner.on_session_close ();
}

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
        std::fprintf (stderr, "asio server: size too large %zu\n", opt.size);
        return false;
    }

    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("test_scenario_stream_asio: no args -> skip\n");
        return 0;
    }

    server_options_t opt;
    if (!parse_options (argc, argv, opt))
        return 2;

    asio_echo_server_t server (opt);
    return server.run ();
}
