/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/streams/stream_host_service.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#endif

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>
#include <thread>
#include <utility>
#include <unordered_set>
#include <vector>

namespace zlink::framework::runtime
{
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
namespace ssl = asio::ssl;
#endif
using detail::stream_header_flags_t;
using detail::stream_header_t;
using detail::stream_message_kind_t;

namespace
{

struct parsed_tcp_endpoint_t
{
    std::string host;
    std::string port;
};

parsed_tcp_endpoint_t parse_tcp_endpoint (const std::string &endpoint)
{
    constexpr std::string_view prefix = "tcp://";
    if (endpoint.rfind (prefix, 0) != 0) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM host currently supports tcp endpoints only");
    }
    const auto host_start = prefix.size ();
    const auto separator = endpoint.rfind (':');
    if (separator == std::string::npos || separator <= host_start
        || separator + 1 >= endpoint.size ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM tcp endpoint must be tcp://host:port");
    }
    return {endpoint.substr (host_start, separator - host_start), endpoint.substr (separator + 1)};
}

parsed_tcp_endpoint_t parse_tls_endpoint (const std::string &endpoint)
{
    constexpr std::string_view prefix = "tls://";
    if (endpoint.rfind (prefix, 0) != 0) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM TLS host requires tls://host:port endpoint");
    }
    const auto host_start = prefix.size ();
    const auto separator = endpoint.rfind (':');
    if (separator == std::string::npos || separator <= host_start
        || separator + 1 >= endpoint.size ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM tls endpoint must be tls://host:port");
    }
    return {endpoint.substr (host_start, separator - host_start), endpoint.substr (separator + 1)};
}

bool stream_uses_tls (const stream_snapshot_t &stream)
{
    return !stream.tls_certificate_file.empty () || !stream.tls_private_key_file.empty ()
           || stream.bind_endpoint.rfind ("tls://", 0) == 0;
}

parsed_tcp_endpoint_t parse_stream_endpoint (const stream_snapshot_t &stream)
{
    return stream_uses_tls (stream) ? parse_tls_endpoint (stream.bind_endpoint)
                                    : parse_tcp_endpoint (stream.bind_endpoint);
}

bool stream_trace_enabled ()
{
    const char *value = std::getenv ("ZLINK_CPP_STREAM_TRACE");
    return value != nullptr && std::string_view (value) != "0" && std::string_view (value) != "";
}

const char *stream_kind_name (stream_message_kind_t kind)
{
    switch (kind) {
    case stream_message_kind_t::send:
        return "send";
    case stream_message_kind_t::request:
        return "request";
    case stream_message_kind_t::response:
        return "response";
    case stream_message_kind_t::error:
        return "error";
    case stream_message_kind_t::control:
        return "control";
    }
    return "unknown";
}

void trace_stream_host (std::string_view stage,
                        const stream_snapshot_t &stream,
                        std::optional<stream_header_t> header = std::nullopt,
                        std::string_view detail = {})
{
    if (!stream_trace_enabled ()) {
        return;
    }
    std::cerr << "zlink-cpp-stream-trace side=server stage=" << stage
              << " stream=" << stream.name << " endpoint=" << stream.bind_endpoint;
    if (header) {
        std::cerr << " seq="
                  << (header->request_seq () ? std::to_string (*header->request_seq ()) : "-")
                  << " name=" << header->packet_name ()
                  << " kind=" << stream_kind_name (header->kind ());
    }
    if (!detail.empty ()) {
        std::cerr << " " << detail;
    }
    std::cerr << std::endl;
}

template <typename TStream> std::vector<std::uint8_t> read_exact (TStream &socket, std::size_t size)
{
    std::vector<std::uint8_t> bytes (size);
    std::size_t offset = 0;
    while (offset < bytes.size ()) {
        boost::system::error_code error;
        const auto read =
          socket.read_some (asio::buffer (bytes.data () + offset, bytes.size () - offset), error);
        if (error) {
            throw boost::system::system_error (error);
        }
        offset += read;
    }
    return bytes;
}

std::vector<std::uint8_t> message_bytes (const zlink::message_t &message)
{
    return message.to_bytes ();
}

zlink::message_t message_from_bytes (const std::vector<std::uint8_t> &bytes)
{
    return zlink::message_t::from (bytes);
}

} // namespace

class stream_host_service_t::listener_t
{
  public:
    listener_t (detail::stream_runtime_t runtime,
                stream_snapshot_t stream,
                detail::stream_session_factory_t session_factory,
                service_provider_t &services,
                std::atomic_bool &stop) :
        _runtime (std::move (runtime)),
        _stream (std::move (stream)),
        _session_factory (std::move (session_factory)),
        _services (&services),
        _stop (&stop),
        _acceptor (_io)
    {
    }

    void run ()
    {
        if (!stream_uses_tls (_stream)) {
            run_tcp_native_accept ();
            return;
        }
        if (stream_uses_tls (_stream)) {
            configure_tls_context ();
        }
        auto endpoint = parse_stream_endpoint (_stream);
        tcp::resolver resolver (_io);
        const auto endpoints = resolver.resolve (endpoint.host, endpoint.port);
        _acceptor.open (endpoints.begin ()->endpoint ().protocol ());
        _acceptor.set_option (tcp::acceptor::reuse_address (true));
        _acceptor.bind (endpoints.begin ()->endpoint ());
        _acceptor.listen ();
        start_boost_accept ();
        mark_started ();
        _io.run ();
    }

    void wait_started ()
    {
        std::unique_lock<std::mutex> lock (_ready_mutex);
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
        if (!_ready_cv.wait_until (lock, deadline, [&] { return _started || _start_failed; })) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "STREAM listener did not become ready: " + _stream.name);
        }
        if (_start_failed) {
            throw framework_exception_t (
              framework_error_kind_t::request_failed,
              _start_error.empty () ? "STREAM listener failed to start: " + _stream.name
                                    : _start_error);
        }
    }

    void request_stop () noexcept
    {
        wake_native_accept ();
        if (!stream_uses_tls (_stream)) {
            return;
        }
        asio::post (_io, [this] {
            boost::system::error_code ignored;
            _acceptor.close (ignored);
        });
    }

    void wake_native_accept () noexcept
    {
        int fd = -1;
        {
            const std::lock_guard<std::mutex> lock (_native_accept_mutex);
            fd = _native_accept_fd;
        }
        if (fd < 0) {
            return;
        }
        const auto endpoint = parse_tcp_endpoint (_stream.bind_endpoint);
        const int wake_fd = ::socket (AF_INET, SOCK_STREAM, 0);
        if (wake_fd < 0) {
            return;
        }
        set_nonblocking (wake_fd);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons (static_cast<std::uint16_t> (std::stoi (endpoint.port)));
        if (::inet_pton (AF_INET, endpoint.host.c_str (), &address.sin_addr) != 1) {
            address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        }
        (void) ::connect (wake_fd, reinterpret_cast<sockaddr *> (&address), sizeof (address));
        pollfd wake_poll{};
        wake_poll.fd = wake_fd;
        wake_poll.events = POLLOUT;
        (void) ::poll (&wake_poll, 1, 50);
        ::close (wake_fd);
    }

    void stop_connections () noexcept
    {
        trace_stream_host ("stop-connections-begin", _stream);
        boost::system::error_code ignored;
        wake_native_accept ();
        {
            const std::lock_guard<std::mutex> lock (_sockets_mutex);
            for (auto *socket : _sockets) {
                const std::lock_guard<std::mutex> io_lock (_io_mutex);
                socket->shutdown (tcp::socket::shutdown_both, ignored);
                socket->close (ignored);
            }
            for (auto it = _native_connections.begin (); it != _native_connections.end ();) {
                auto connection = it->lock ();
                if (!connection) {
                    it = _native_connections.erase (it);
                    continue;
                }
                close_connection (*connection);
                ++it;
            }
        }
        {
            const std::lock_guard<std::mutex> lock (_workers_mutex);
            trace_stream_host ("stop-connections-join-workers", _stream);
            for (auto &worker : _workers) {
                if (worker.joinable ()) {
                    worker.join ();
                }
            }
            _workers.clear ();
        }
        trace_stream_host ("stop-connections-end", _stream);
    }

  private:
    struct tcp_connection_t
    {
        asio::io_context io;
        tcp::socket socket;

        tcp_connection_t () : socket (io) {}
    };

    struct native_tcp_connection_t
    {
        explicit native_tcp_connection_t (int accepted) : fd (accepted) {}

        std::mutex mutex;
        int fd;
    };

    struct frame_t
    {
        stream_header_t header;
        zlink::message_t payload;
    };

    static void set_nonblocking (int fd)
    {
        const int flags = ::fcntl (fd, F_GETFL, 0);
        if (flags >= 0) {
            (void) ::fcntl (fd, F_SETFL, flags | O_NONBLOCK);
        }
    }

    void run_tcp_native_accept ()
    {
        const auto endpoint = parse_tcp_endpoint (_stream.bind_endpoint);
        const int server_fd = ::socket (AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            mark_start_failed ("STREAM TCP socket create failed: " + _stream.name);
            return;
        }
        {
            const std::lock_guard<std::mutex> lock (_native_accept_mutex);
            _native_accept_fd = server_fd;
        }
        int reuse = 1;
        (void) ::setsockopt (server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));
        set_nonblocking (server_fd);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons (static_cast<std::uint16_t> (std::stoi (endpoint.port)));
        if (::inet_pton (AF_INET, endpoint.host.c_str (), &address.sin_addr) != 1) {
            address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        }
        if (::bind (server_fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0
            || ::listen (server_fd, SOMAXCONN) != 0) {
            const auto message =
              std::string ("STREAM TCP listener failed: ") + std::strerror (errno);
            const std::lock_guard<std::mutex> lock (_native_accept_mutex);
            ::close (server_fd);
            _native_accept_fd = -1;
            mark_start_failed (message);
            return;
        }
        mark_started ();
        while (!_stop->load (std::memory_order_acquire)) {
            pollfd accept_poll{};
            accept_poll.fd = server_fd;
            accept_poll.events = POLLIN;
            const int ready = ::poll (&accept_poll, 1, 100);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (_stop->load (std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (ready == 0) {
                continue;
            }
            if ((accept_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                if (_stop->load (std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            sockaddr_in peer{};
            socklen_t peer_len = sizeof (peer);
            const int accepted =
              ::accept (server_fd, reinterpret_cast<sockaddr *> (&peer), &peer_len);
            if (accepted < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                if (_stop->load (std::memory_order_acquire)) {
                    break;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (_stop->load (std::memory_order_acquire)) {
                ::shutdown (accepted, SHUT_RDWR);
                ::close (accepted);
                break;
            }
            trace_stream_host ("accept", _stream);
            auto connection = std::make_shared<native_tcp_connection_t> (accepted);
            {
                const std::lock_guard<std::mutex> lock (_sockets_mutex);
                _native_connections.push_back (connection);
            }
            const std::lock_guard<std::mutex> lock (_workers_mutex);
            _workers.emplace_back ([this, connection] { handle_native_connection (connection); });
        }
        {
            const std::lock_guard<std::mutex> lock (_native_accept_mutex);
            if (_native_accept_fd >= 0) {
                ::close (_native_accept_fd);
                _native_accept_fd = -1;
            }
        }
    }

    void mark_started ()
    {
        {
            const std::lock_guard<std::mutex> lock (_ready_mutex);
            _started = true;
        }
        _ready_cv.notify_all ();
    }

    void mark_start_failed (std::string message)
    {
        {
            const std::lock_guard<std::mutex> lock (_ready_mutex);
            _start_failed = true;
            _start_error = std::move (message);
        }
        _ready_cv.notify_all ();
    }

    void configure_tls_context ()
    {
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
        if (_stream.tls_certificate_file.empty () || _stream.tls_private_key_file.empty ()) {
            throw framework_exception_t (
              framework_error_kind_t::request_protocol_error,
              "STREAM TLS endpoint requires certificate and private key");
        }
        _tls_context.emplace (ssl::context::tls_server);
        _tls_context->use_certificate_chain_file (_stream.tls_certificate_file);
        _tls_context->use_private_key_file (_stream.tls_private_key_file, ssl::context::pem);
#else
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM TLS support requires OpenSSL");
#endif
    }

    void start_boost_accept ()
    {
        if (_stop->load (std::memory_order_acquire)) {
            return;
        }
        auto connection = std::make_shared<tcp_connection_t> ();
        _acceptor.async_accept (
          connection->socket, [this, connection] (const boost::system::error_code &error) {
              handle_boost_accept (connection, error);
          });
    }

    void handle_boost_accept (const std::shared_ptr<tcp_connection_t> &connection,
                              const boost::system::error_code &error)
    {
        if (error) {
            if (!_stop->load (std::memory_order_acquire)) {
                start_boost_accept ();
            }
            return;
        }
        if (_stop->load (std::memory_order_acquire)) {
            close_connection (connection->socket);
            return;
        }
        trace_stream_host ("accept", _stream);
        {
            const std::lock_guard<std::mutex> lock (_sockets_mutex);
            _sockets.insert (&connection->socket);
        }
        if (stream_uses_tls (_stream)) {
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
            auto tls_connection =
              std::make_shared<ssl::stream<tcp::socket>> (std::move (connection->socket),
                                                          *_tls_context);
            {
                const std::lock_guard<std::mutex> lock (_sockets_mutex);
                _sockets.erase (&connection->socket);
                _sockets.insert (&tls_connection->next_layer ());
            }
            {
                const std::lock_guard<std::mutex> lock (_workers_mutex);
                _workers.emplace_back ([this, connection, tls_connection] {
                    handle_tls_connection (connection, tls_connection);
                });
            }
#else
            close_connection (connection->socket);
#endif
        } else {
            const std::lock_guard<std::mutex> lock (_workers_mutex);
            _workers.emplace_back ([this, connection] { handle_connection (connection); });
        }
        start_boost_accept ();
    }

    void close_tcp_socket (tcp::socket &socket) noexcept
    {
        boost::system::error_code ignored;
        const std::lock_guard<std::mutex> lock (_io_mutex);
        socket.shutdown (tcp::socket::shutdown_both, ignored);
        socket.close (ignored);
    }

    void close_connection (tcp::socket &socket) noexcept { close_tcp_socket (socket); }

    void close_connection (native_tcp_connection_t &connection) noexcept
    {
        int fd = -1;
        {
            const std::lock_guard<std::mutex> lock (connection.mutex);
            fd = connection.fd;
            connection.fd = -1;
        }
        if (fd < 0)
            return;
        ::shutdown (fd, SHUT_RDWR);
        ::close (fd);
    }

#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
    void close_connection (ssl::stream<tcp::socket> &stream) noexcept
    {
        close_tcp_socket (stream.next_layer ());
    }
#endif

    std::vector<std::uint8_t> read_exact_native (native_tcp_connection_t &connection,
                                                 std::size_t size)
    {
        std::vector<std::uint8_t> bytes (size);
        std::size_t offset = 0;
        while (offset < bytes.size ()) {
            int fd = -1;
            {
                const std::lock_guard<std::mutex> lock (connection.mutex);
                fd = connection.fd;
            }
            if (fd < 0) {
                throw framework_exception_t (framework_error_kind_t::disconnected,
                                             "stream native socket closed");
            }
            const auto received = ::recv (fd, bytes.data () + offset, bytes.size () - offset, 0);
            const int received_errno = errno;
            if (received < 0) {
                if (received_errno == EINTR) {
                    continue;
                }
                throw framework_exception_t (framework_error_kind_t::disconnected,
                                             std::strerror (received_errno));
            }
            if (received == 0) {
                throw framework_exception_t (framework_error_kind_t::disconnected,
                                             "STREAM TCP peer disconnected");
            }
            offset += static_cast<std::size_t> (received);
        }
        return bytes;
    }

    template <typename TStream> frame_t read_frame (TStream &socket)
    {
        auto prefix = read_exact (socket, 6);
        const auto header_size = static_cast<std::size_t> ((prefix[0] << 8) | prefix[1]);
        const auto payload_size = (static_cast<std::size_t> (prefix[2]) << 24)
                                  | (static_cast<std::size_t> (prefix[3]) << 16)
                                  | (static_cast<std::size_t> (prefix[4]) << 8)
                                  | static_cast<std::size_t> (prefix[5]);
        auto header_bytes = read_exact (socket, header_size);
        auto payload_bytes = read_exact (socket, payload_size);
        auto header = _runtime.decode_header (header_bytes);
        if (!header) {
            throw framework_exception_t (header.error_kind (), header.error ()
                                                                 ? header.error ()->what ()
                                                                 : "STREAM header decode failed");
        }
        auto decoded_header = header.value ();
        trace_stream_host ("read-frame", _stream, decoded_header,
                           "payload_bytes=" + std::to_string (payload_bytes.size ()));
        return {decoded_header, message_from_bytes (payload_bytes)};
    }

    template <typename TStream>
    void
    write_frame (TStream &socket, const stream_header_t &header, const zlink::message_t &payload)
    {
        auto encoded_header = _runtime.encode_header (header);
        if (!encoded_header) {
            throw framework_exception_t (encoded_header.error_kind (),
                                         encoded_header.error () ? encoded_header.error ()->what ()
                                                                 : "STREAM header encode failed");
        }
        const auto payload_bytes = message_bytes (payload);
        std::vector<std::uint8_t> frame;
        frame.reserve (6 + encoded_header.value ().size () + payload_bytes.size ());
        const auto header_size = encoded_header.value ().size ();
        frame.push_back (static_cast<std::uint8_t> ((header_size >> 8) & 0xff));
        frame.push_back (static_cast<std::uint8_t> (header_size & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 24) & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 16) & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 8) & 0xff));
        frame.push_back (static_cast<std::uint8_t> (payload_bytes.size () & 0xff));
        frame.insert (frame.end (), encoded_header.value ().begin (),
                      encoded_header.value ().end ());
        frame.insert (frame.end (), payload_bytes.begin (), payload_bytes.end ());
        trace_stream_host ("write-frame", _stream, header,
                           "payload_bytes=" + std::to_string (payload_bytes.size ()));
        boost::asio::write (socket, boost::asio::buffer (frame));
        trace_stream_host ("write-completion", _stream, header, "result=success");
    }

    frame_t read_frame_native (native_tcp_connection_t &connection)
    {
        auto prefix = read_exact_native (connection, 6);
        const auto header_size = static_cast<std::size_t> ((prefix[0] << 8) | prefix[1]);
        const auto payload_size = (static_cast<std::size_t> (prefix[2]) << 24)
                                  | (static_cast<std::size_t> (prefix[3]) << 16)
                                  | (static_cast<std::size_t> (prefix[4]) << 8)
                                  | static_cast<std::size_t> (prefix[5]);
        auto header_bytes = read_exact_native (connection, header_size);
        auto payload_bytes = read_exact_native (connection, payload_size);
        auto header = _runtime.decode_header (header_bytes);
        if (!header) {
            throw framework_exception_t (header.error_kind (), header.error ()
                                                                 ? header.error ()->what ()
                                                                 : "STREAM header decode failed");
        }
        auto decoded_header = header.value ();
        trace_stream_host ("read-frame", _stream, decoded_header,
                           "payload_bytes=" + std::to_string (payload_bytes.size ()));
        return {decoded_header, message_from_bytes (payload_bytes)};
    }

    void write_all_native (native_tcp_connection_t &connection,
                           const std::vector<std::uint8_t> &bytes)
    {
        std::size_t offset = 0;
        while (offset < bytes.size ()) {
            int fd = -1;
            {
                const std::lock_guard<std::mutex> lock (connection.mutex);
                fd = connection.fd;
            }
            if (fd < 0) {
                throw framework_exception_t (framework_error_kind_t::disconnected,
                                             "stream native socket closed");
            }
            const auto sent = ::send (fd, bytes.data () + offset, bytes.size () - offset,
                                      MSG_NOSIGNAL);
            const int sent_errno = errno;
            if (sent < 0) {
                if (sent_errno == EINTR) {
                    continue;
                }
                throw framework_exception_t (framework_error_kind_t::disconnected,
                                             std::strerror (sent_errno));
            }
            if (sent == 0) {
                throw framework_exception_t (framework_error_kind_t::disconnected,
                                             "STREAM TCP write made no progress");
            }
            offset += static_cast<std::size_t> (sent);
        }
    }

    void write_frame_native (native_tcp_connection_t &connection,
                             const stream_header_t &header,
                             const zlink::message_t &payload)
    {
        auto encoded_header = _runtime.encode_header (header);
        if (!encoded_header) {
            throw framework_exception_t (encoded_header.error_kind (),
                                         encoded_header.error () ? encoded_header.error ()->what ()
                                                                 : "STREAM header encode failed");
        }
        const auto payload_bytes = message_bytes (payload);
        std::vector<std::uint8_t> frame;
        frame.reserve (6 + encoded_header.value ().size () + payload_bytes.size ());
        const auto header_size = encoded_header.value ().size ();
        frame.push_back (static_cast<std::uint8_t> ((header_size >> 8) & 0xff));
        frame.push_back (static_cast<std::uint8_t> (header_size & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 24) & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 16) & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 8) & 0xff));
        frame.push_back (static_cast<std::uint8_t> (payload_bytes.size () & 0xff));
        frame.insert (frame.end (), encoded_header.value ().begin (),
                      encoded_header.value ().end ());
        frame.insert (frame.end (), payload_bytes.begin (), payload_bytes.end ());
        trace_stream_host ("write-frame", _stream, header,
                           "payload_bytes=" + std::to_string (payload_bytes.size ()));
        write_all_native (connection, frame);
        trace_stream_host ("write-completion", _stream, header, "result=success");
    }

    template <typename TStream>
    void write_error_frame (TStream &socket,
                            const stream_header_t &request_header,
                            const result_t<void> &error)
    {
        if (!request_header.request_seq ()) {
            return;
        }
        auto message = error.error () ? error.error ()->what () : "STREAM request failed";
        stream_header_t error_header (stream_message_kind_t::error, stream_codec_t::json,
                                      stream_header_flags_t::has_request_seq,
                                      request_header.request_seq (),
                                      std::string (request_header.packet_name ()), {});
        // Echo the request correlation id so a stream request FAILURE is traceable
        // by the same corr as its inbound `received`.
        if (auto correlation = request_header.correlation_id ()) {
            error_header.with_correlation_id (std::string (*correlation));
        }
        write_frame (socket, error_header,
                     zlink::message_t::from (std::string ("{\"error\":\"") + message + "\"}"));
    }

    void write_error_frame_native (native_tcp_connection_t &connection,
                                   const stream_header_t &request_header,
                                   const result_t<void> &error)
    {
        if (!request_header.request_seq ()) {
            return;
        }
        auto message = error.error () ? error.error ()->what () : "STREAM request failed";
        stream_header_t error_header (stream_message_kind_t::error, stream_codec_t::json,
                                      stream_header_flags_t::has_request_seq,
                                      request_header.request_seq (),
                                      std::string (request_header.packet_name ()), {});
        if (auto correlation = request_header.correlation_id ()) {
            error_header.with_correlation_id (std::string (*correlation));
        }
        write_frame_native (connection, error_header,
                            zlink::message_t::from (std::string ("{\"error\":\"") + message
                                                    + "\"}"));
    }

    template <typename TStream>
    void
    flush_writes (TStream &socket, stream_t &stream, std::size_t &flushed, std::mutex &write_mutex)
    {
        const auto headers = _runtime.written_headers (stream);
        const auto payloads = _runtime.written_payloads (stream);
        const std::lock_guard<std::mutex> lock (write_mutex);
        for (; flushed < headers.size (); ++flushed) {
            write_frame (socket, headers[flushed], payloads[flushed]);
        }
    }

    void flush_writes_native (native_tcp_connection_t &connection,
                              stream_t &stream,
                              std::size_t &flushed,
                              std::mutex &write_mutex)
    {
        const auto headers = _runtime.written_headers (stream);
        const auto payloads = _runtime.written_payloads (stream);
        const std::lock_guard<std::mutex> lock (write_mutex);
        for (; flushed < headers.size (); ++flushed) {
            write_frame_native (connection, headers[flushed], payloads[flushed]);
        }
    }

    template <typename TStream>
    void handle_stream_connection (std::shared_ptr<TStream> connection,
                                   tcp::socket *tracked_socket,
                                   bool attach_immediate_writer)
    {
        auto cleanup = std::unique_ptr<tcp::socket, std::function<void (tcp::socket *)>> (
          tracked_socket, [this] (tcp::socket *socket) {
              const std::lock_guard<std::mutex> lock (_sockets_mutex);
              _sockets.erase (socket);
          });
        auto scope = _services->create_scope (service_scope_kind_t::stream_session);
        auto &session = _session_factory (scope.provider ());
        auto stream = _runtime.open_session (_stream.name);
        auto write_mutex = std::make_shared<std::mutex> ();
        if (attach_immediate_writer) {
            _runtime.attach_transport_writer (
              stream,
              [this, connection, write_mutex] (const stream_header_t &header,
                                               const zlink::message_t &payload) -> result_t<void> {
                  try {
                      const std::lock_guard<std::mutex> lock (*write_mutex);
                      write_frame (*connection, header, payload);
                      return result_t<void>::success ();
                  }
                  catch (const framework_exception_t &error) {
                      return result_t<void>::failure (error.kind (), error.what (),
                                                      error.is_retriable ());
                  }
                  catch (const std::exception &error) {
                      return result_t<void>::failure (framework_error_kind_t::disconnected,
                                                      error.what ());
                  }
              });
        }
        std::size_t flushed = 0;
        bool connected_session = false;
        try {
            if (auto connected = _runtime.dispatch_connected (session, stream); !connected) {
                return;
            }
            connected_session = true;
            flush_writes (*connection, stream, flushed, *write_mutex);
            while (!_stop->load (std::memory_order_acquire)) {
                auto frame = read_frame (*connection);
                if (frame.header.kind () == stream_message_kind_t::control) {
                    continue;
                }
                trace_stream_host ("dispatch", _stream, frame.header);
                if (auto dispatched =
                      _runtime.dispatch_packet (session, stream, frame.header, frame.payload);
                    !dispatched) {
                    if (frame.header.kind () == stream_message_kind_t::request) {
                        const std::lock_guard<std::mutex> lock (*write_mutex);
                        write_error_frame (*connection, frame.header, dispatched);
                    }
                }
                flush_writes (*connection, stream, flushed, *write_mutex);
            }
        }
        catch (const boost::system::system_error &error) {
            if (stream_trace_enabled ()) {
                std::cerr << "zlink-cpp-stream-trace side=server stage=connection-error stream="
                          << _stream.name << " endpoint=" << _stream.bind_endpoint
                          << " error=\"" << error.what () << "\"" << std::endl;
            }
        }
        catch (const framework_exception_t &error) {
            if (stream_trace_enabled ()) {
                std::cerr << "zlink-cpp-stream-trace side=server stage=connection-error stream="
                          << _stream.name << " endpoint=" << _stream.bind_endpoint
                          << " error=\"" << error.what () << "\"" << std::endl;
            }
        }
        catch (const std::exception &error) {
            if (stream_trace_enabled ()) {
                std::cerr << "zlink-cpp-stream-trace side=server stage=connection-error stream="
                          << _stream.name << " endpoint=" << _stream.bind_endpoint
                          << " error=\"" << error.what () << "\"" << std::endl;
            }
        }
        catch (...) {
            if (stream_trace_enabled ()) {
                std::cerr << "zlink-cpp-stream-trace side=server stage=connection-error stream="
                          << _stream.name << " endpoint=" << _stream.bind_endpoint
                          << " error=\"unknown\"" << std::endl;
            }
        }
        if (connected_session) {
            if (_stop->load (std::memory_order_acquire)) {
                _runtime.mark_disconnected (stream);
            } else {
                trace_stream_host ("dispatch-disconnected-begin", _stream);
                (void) _runtime.dispatch_disconnected (session, stream);
                trace_stream_host ("dispatch-disconnected-end", _stream);
            }
            if (!_stop->load (std::memory_order_acquire)) {
                try {
                    flush_writes (*connection, stream, flushed, *write_mutex);
                }
                catch (...) {
                }
            }
        }
        close_connection (*connection);
    }

    void handle_native_connection (std::shared_ptr<native_tcp_connection_t> connection)
    {
        auto cleanup = std::unique_ptr<native_tcp_connection_t, std::function<void (native_tcp_connection_t *)>> (
          connection.get (), [this] (native_tcp_connection_t *native_connection) {
              const std::lock_guard<std::mutex> lock (_sockets_mutex);
              for (auto it = _native_connections.begin (); it != _native_connections.end ();) {
                  auto tracked = it->lock ();
                  if (!tracked || tracked.get () == native_connection)
                      it = _native_connections.erase (it);
                  else
                      ++it;
              }
          });
        auto scope = _services->create_scope (service_scope_kind_t::stream_session);
        auto &session = _session_factory (scope.provider ());
        auto stream = _runtime.open_session (_stream.name);
        auto write_mutex = std::make_shared<std::mutex> ();
        _runtime.attach_transport_writer (
          stream,
          [this, connection, write_mutex] (const stream_header_t &header,
                                           const zlink::message_t &payload) -> result_t<void> {
              try {
                  const std::lock_guard<std::mutex> lock (*write_mutex);
                  write_frame_native (*connection, header, payload);
                  return result_t<void>::success ();
              }
              catch (const framework_exception_t &error) {
                  return result_t<void>::failure (error.kind (), error.what (),
                                                  error.is_retriable ());
              }
              catch (const std::exception &error) {
                  return result_t<void>::failure (framework_error_kind_t::disconnected,
                                                  error.what ());
              }
          });
        std::size_t flushed = 0;
        bool connected_session = false;
        try {
            if (auto connected = _runtime.dispatch_connected (session, stream); !connected) {
                return;
            }
            connected_session = true;
            flush_writes_native (*connection, stream, flushed, *write_mutex);
            while (!_stop->load (std::memory_order_acquire)) {
                auto frame = read_frame_native (*connection);
                if (frame.header.kind () == stream_message_kind_t::control) {
                    continue;
                }
                trace_stream_host ("dispatch", _stream, frame.header);
                if (auto dispatched =
                      _runtime.dispatch_packet (session, stream, frame.header, frame.payload);
                    !dispatched) {
                    if (frame.header.kind () == stream_message_kind_t::request) {
                        const std::lock_guard<std::mutex> lock (*write_mutex);
                        write_error_frame_native (*connection, frame.header, dispatched);
                    }
                }
                flush_writes_native (*connection, stream, flushed, *write_mutex);
            }
        }
        catch (const framework_exception_t &) {
        }
        catch (...) {
        }
        if (connected_session) {
            if (_stop->load (std::memory_order_acquire)) {
                _runtime.mark_disconnected (stream);
            } else {
                trace_stream_host ("dispatch-disconnected-begin", _stream);
                (void) _runtime.dispatch_disconnected (session, stream);
                trace_stream_host ("dispatch-disconnected-end", _stream);
            }
            if (!_stop->load (std::memory_order_acquire)) {
                try {
                    flush_writes_native (*connection, stream, flushed, *write_mutex);
                }
                catch (...) {
                }
            }
        }
        close_connection (*connection);
    }

#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
    void handle_tls_connection (std::shared_ptr<tcp_connection_t> owner,
                                std::shared_ptr<ssl::stream<tcp::socket>> connection)
    {
        try {
            connection->handshake (ssl::stream_base::server);
        }
        catch (const boost::system::system_error &) {
            const std::lock_guard<std::mutex> lock (_sockets_mutex);
            _sockets.erase (&connection->next_layer ());
            return;
        }
        (void) owner;
        handle_stream_connection (connection, &connection->next_layer (), true);
    }
#endif

    void handle_connection (std::shared_ptr<tcp_connection_t> connection)
    {
        handle_stream_connection (std::shared_ptr<tcp::socket> (connection, &connection->socket),
                                  &connection->socket, true);
    }

    detail::stream_runtime_t _runtime;
    stream_snapshot_t _stream;
    detail::stream_session_factory_t _session_factory;
    service_provider_t *_services;
    std::atomic_bool *_stop;
    asio::io_context _io;
    std::mutex _io_mutex;
    tcp::acceptor _acceptor;
    std::mutex _native_accept_mutex;
    int _native_accept_fd = -1;
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
    std::optional<ssl::context> _tls_context;
#endif
    std::mutex _sockets_mutex;
    std::unordered_set<tcp::socket *> _sockets;
    std::vector<std::weak_ptr<native_tcp_connection_t>> _native_connections;
    std::mutex _workers_mutex;
    std::vector<std::thread> _workers;
    std::mutex _ready_mutex;
    std::condition_variable _ready_cv;
    bool _started = false;
    bool _start_failed = false;
    std::string _start_error;
};

stream_host_service_t::stream_host_service_t (
  detail::stream_runtime_t runtime,
  std::vector<stream_snapshot_t> streams,
  std::map<std::string, detail::stream_session_factory_t> session_factories) :
    _runtime (std::move (runtime)),
    _streams (std::move (streams)),
    _session_factories (std::move (session_factories))
{
}

stream_host_service_t::~stream_host_service_t () = default;

void stream_host_service_t::start (service_provider_t &services)
{
    _services = &services;
    _stop.store (false, std::memory_order_release);
    for (const auto &stream : _streams) {
        auto factory = _session_factories.find (stream.packet_session_name);
        if (factory == _session_factories.end ()) {
            continue;
        }
        auto listener =
          std::make_unique<listener_t> (_runtime, stream, factory->second, services, _stop);
        auto *raw = listener.get ();
        _listeners.push_back (std::move (listener));
        _threads.emplace_back ([raw] { raw->run (); });
        raw->wait_started ();
    }
}

void stream_host_service_t::request_stop () noexcept
{
    _stop.store (true, std::memory_order_release);
    for (auto &listener : _listeners) {
        listener->request_stop ();
    }
}

void stream_host_service_t::stop () noexcept
{
    request_stop ();
    for (auto &listener : _listeners) {
        listener->stop_connections ();
    }
    for (auto &thread : _threads) {
        if (thread.joinable ()) {
            thread.join ();
        }
    }
    _threads.clear ();
    for (auto &listener : _listeners) {
        listener->stop_connections ();
    }
    _listeners.clear ();
    _services = nullptr;
}

} // namespace zlink::framework::runtime
