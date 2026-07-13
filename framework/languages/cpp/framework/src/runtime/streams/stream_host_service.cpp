/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/streams/stream_host_service.hpp"

#include "runtime/diagnostics/dispatch_error_reporter.hpp"
#include "runtime/diagnostics/runtime_metrics.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket.hpp>
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
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using websocket_stream_t = websocket::stream<tcp::socket>;
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

parsed_tcp_endpoint_t parse_websocket_endpoint (const std::string &endpoint)
{
    constexpr std::string_view prefix = "ws://";
    if (endpoint.rfind (prefix, 0) != 0) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM WebSocket host requires ws://host:port endpoint");
    }
    const auto authority_start = prefix.size ();
    const auto path = endpoint.find ('/', authority_start);
    const auto authority = endpoint.substr (
      authority_start, path == std::string::npos ? std::string::npos : path - authority_start);
    const auto separator = authority.rfind (':');
    if (separator == std::string::npos || separator == 0
        || separator + 1 >= authority.size ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM WebSocket endpoint must be ws://host:port[/path]");
    }
    return {authority.substr (0, separator), authority.substr (separator + 1)};
}

bool stream_uses_websocket (const stream_snapshot_t &stream)
{
    return stream.bind_endpoint.rfind ("ws://", 0) == 0;
}

bool stream_uses_tls (const stream_snapshot_t &stream)
{
    return !stream.tls_certificate_file.empty () || !stream.tls_private_key_file.empty ()
           || stream.bind_endpoint.rfind ("tls://", 0) == 0;
}

parsed_tcp_endpoint_t parse_stream_endpoint (const stream_snapshot_t &stream)
{
    if (stream_uses_tls (stream)) {
        return parse_tls_endpoint (stream.bind_endpoint);
    }
    if (stream_uses_websocket (stream)) {
        return parse_websocket_endpoint (stream.bind_endpoint);
    }
    return parse_tcp_endpoint (stream.bind_endpoint);
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
                std::atomic_bool &stop,
                std::shared_ptr<std::atomic_bool> drain_flag,
                std::shared_ptr<framework::detail::monitoring_runtime_state_t> monitoring) :
        _runtime (std::move (runtime)),
        _stream (std::move (stream)),
        _session_factory (std::move (session_factory)),
        _services (&services),
        _stop (&stop),
        _drain_flag (std::move (drain_flag)),
        _monitoring (std::move (monitoring)),
        _acceptor (_io)
    {
    }

    /* Server liveness policy (graceful-drain-handoff §7.2): fixed 1s ping /
     * 5s pong / 30s application idle — the owning service drives ONE sweep
     * loop per node across all listeners, no per-session timers. Control
     * packets never refresh the application idle clock; a same-cycle double
     * expiry resolves to heartbeat_timeout. */
    struct session_liveness_t
    {
        std::mutex gate;
        std::chrono::steady_clock::time_point last_application_inbound =
          std::chrono::steady_clock::now ();
        std::chrono::steady_clock::time_point last_ping = std::chrono::steady_clock::now ();
        bool heartbeat_outstanding = false;
        bool terminated = false;
        std::optional<stream_close_reason_t> forced_reason;

        enum class decision_t
        {
            none,
            send_heartbeat,
            idle_timeout,
            heartbeat_timeout
        };

        void record_application_inbound ()
        {
            const std::lock_guard<std::mutex> lock (gate);
            last_application_inbound = std::chrono::steady_clock::now ();
        }

        void record_pong ()
        {
            const std::lock_guard<std::mutex> lock (gate);
            heartbeat_outstanding = false;
        }

        void record_ping ()
        {
            const std::lock_guard<std::mutex> lock (gate);
            last_ping = std::chrono::steady_clock::now ();
            heartbeat_outstanding = true;
        }

        decision_t evaluate ()
        {
            const std::lock_guard<std::mutex> lock (gate);
            if (terminated) {
                return decision_t::none;
            }
            const auto now = std::chrono::steady_clock::now ();
            if (heartbeat_outstanding && now - last_ping >= std::chrono::seconds (5)) {
                return decision_t::heartbeat_timeout;
            }
            if (now - last_application_inbound >= std::chrono::seconds (30)) {
                return decision_t::idle_timeout;
            }
            if (!heartbeat_outstanding && now - last_ping >= std::chrono::seconds (1)) {
                return decision_t::send_heartbeat;
            }
            return decision_t::none;
        }

        // The terminal close runs once even when sweeps race the reader exit.
        bool try_terminate (stream_close_reason_t reason)
        {
            const std::lock_guard<std::mutex> lock (gate);
            if (terminated) {
                return false;
            }
            terminated = true;
            forced_reason = reason;
            return true;
        }

        std::optional<stream_close_reason_t> forced ()
        {
            const std::lock_guard<std::mutex> lock (gate);
            return forced_reason;
        }
    };

    struct active_session_t
    {
        stream_t stream;
        std::shared_ptr<session_liveness_t> liveness;
        std::function<void ()> force_close;
    };

    /* zlink.stream.connections.* (runtime-metrics §4.1): session accept and
     * close on the server edge; reconnect attempts belong to the connector. */
    void register_active_stream (const stream_t &stream,
                                 std::shared_ptr<session_liveness_t> liveness,
                                 std::function<void ()> force_close)
    {
        const std::lock_guard<std::mutex> lock (_active_streams_mutex);
        _active_streams.push_back (
          active_session_t{stream, std::move (liveness), std::move (force_close)});
    }

    void unregister_active_stream (const stream_t &stream)
    {
        const std::lock_guard<std::mutex> lock (_active_streams_mutex);
        for (auto it = _active_streams.begin (); it != _active_streams.end (); ++it) {
            if (it->stream.session_id () == stream.session_id ()) {
                _active_streams.erase (it);
                break;
            }
        }
    }

    void notify_sessions_closing (stream_close_reason_t reason, std::string_view diagnostic)
    {
        std::vector<active_session_t> sessions;
        {
            const std::lock_guard<std::mutex> lock (_active_streams_mutex);
            sessions = _active_streams;
        }
        for (auto &entry : sessions) {
            _runtime.send_session_closing (entry.stream, reason, diagnostic);
        }
    }

    /* Forced teardown (graceful-drain-handoff §7): the notified sessions are
     * closed so the reason the client keeps is the forced one — a lingering
     * connection would later be re-labeled by the liveness loop. */
    void force_close_sessions (stream_close_reason_t reason, std::string_view diagnostic)
    {
        std::vector<active_session_t> sessions;
        {
            const std::lock_guard<std::mutex> lock (_active_streams_mutex);
            sessions = _active_streams;
        }
        for (auto &entry : sessions) {
            terminate_session (entry, reason, diagnostic);
        }
    }

    void terminate_session (active_session_t &entry,
                            stream_close_reason_t reason,
                            std::string_view diagnostic)
    {
        if (!entry.liveness->try_terminate (reason)) {
            return;
        }
        _runtime.send_session_closing (entry.stream, reason, diagnostic);
        if (entry.force_close) {
            entry.force_close ();
        }
    }

    /* One liveness pass over this listener's sessions; the owning service
     * drives all listeners from a single per-node sweep loop (§7.2: no
     * per-session timers, one loop per node). */
    void sweep_liveness_once ()
    {
        std::vector<active_session_t> sessions;
        {
            const std::lock_guard<std::mutex> lock (_active_streams_mutex);
            sessions = _active_streams;
        }
        for (auto &entry : sessions) {
            switch (entry.liveness->evaluate ()) {
                case session_liveness_t::decision_t::none:
                    break;
                case session_liveness_t::decision_t::send_heartbeat:
                    /* Stamp before writing: a same-instant pong then clears
                     * the outstanding flag instead of racing the stamp and
                     * being treated as stale. A failed write leaves the stamp
                     * in place — the transport is broken and the 5s pong
                     * timeout closes the session. */
                    entry.liveness->record_ping ();
                    _runtime.send_heartbeat_ping (entry.stream);
                    break;
                case session_liveness_t::decision_t::idle_timeout:
                    terminate_session (entry, stream_close_reason_t::idle_timeout,
                                       "application idle timeout");
                    break;
                case session_liveness_t::decision_t::heartbeat_timeout:
                    terminate_session (entry, stream_close_reason_t::heartbeat_timeout,
                                       "heartbeat pong timeout");
                    break;
            }
        }
    }

    static const char *liveness_close_label (stream_close_reason_t reason) noexcept
    {
        switch (reason) {
            case stream_close_reason_t::idle_timeout:
                return "idle_timeout";
            case stream_close_reason_t::heartbeat_timeout:
                return "heartbeat_timeout";
            case stream_close_reason_t::server_drain:
                return "server_drain";
            case stream_close_reason_t::protocol_error:
                return "protocol_error";
            default:
                return "transport_error";
        }
    }

    void record_connection_opened () const
    {
        framework::runtime::runtime_metrics_t metrics (_monitoring);
        if (metrics.enabled ()) {
            metrics.counter ("zlink.stream.connections.opened", "{connection}", 1);
            metrics.updown ("zlink.stream.connections.active", "{connection}", 1);
        }
    }

    void record_connection_closed (const char *close_reason) const
    {
        framework::runtime::runtime_metrics_t metrics (_monitoring);
        if (metrics.enabled ()) {
            metrics.counter ("zlink.stream.connections.closed", "{connection}", 1,
                             {{"close_reason", close_reason}});
            metrics.updown ("zlink.stream.connections.active", "{connection}", -1);
        }
    }

    bool draining () const noexcept
    {
        return _drain_flag && _drain_flag->load (std::memory_order_acquire);
    }

    /* flow-correlation §4.3: session dispatch failures surface as error
     * events carrying the inbound flow pair, so one grep catches the
     * healthy and failing lines of the same flow. */
    void report_packet_dispatch_error (const detail::stream_header_t &header,
                                       const result_t<void> &dispatched) const
    {
        message_dispatch_error_event_t event{
          dispatch_error_surface_t::stream_session,
          header.kind () == stream_message_kind_t::request ? dispatch_message_kind_t::request
                                                           : dispatch_message_kind_t::send,
          detail::dispatch_reason_from_error (dispatched.error_kind ()),
          header.kind () == stream_message_kind_t::request
            ? dispatch_error_action_t::reply_error
            : dispatch_error_action_t::drop,
          std::string (header.packet_name ()),
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          header.correlation_id () ? std::make_optional (std::string (*header.correlation_id ()))
                                   : std::nullopt,
          dispatched.error () != nullptr ? std::make_exception_ptr (*dispatched.error ())
                                         : std::exception_ptr{}};
        if (auto flow = header.flow_id ()) {
            event.flow_id = std::string (*flow);
            event.flow_origin = header.flow_origin ();
        }
        detail::dispatch_error_reporter_t (_runtime.dispatch_options_ref ()).report (event);
    }

    void run ()
    {
        if (!stream_uses_tls (_stream) && !stream_uses_websocket (_stream)) {
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
        if (!stream_uses_tls (_stream) && !stream_uses_websocket (_stream)) {
            wake_native_accept ();
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
        if (!stream_uses_tls (_stream) && !stream_uses_websocket (_stream)) {
            wake_native_accept ();
        }
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
        if (stream_uses_websocket (_stream)) {
            auto websocket_connection =
              std::make_shared<websocket_stream_t> (std::move (connection->socket));
            {
                const std::lock_guard<std::mutex> lock (_sockets_mutex);
                _sockets.erase (&connection->socket);
                _sockets.insert (&websocket_connection->next_layer ());
            }
            {
                const std::lock_guard<std::mutex> lock (_workers_mutex);
                _workers.emplace_back ([this, connection, websocket_connection] {
                    handle_websocket_connection (connection, websocket_connection);
                });
            }
        } else if (stream_uses_tls (_stream)) {
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

    void close_connection (websocket_stream_t &stream) noexcept
    {
        boost::system::error_code ignored;
        {
            const std::lock_guard<std::mutex> lock (_io_mutex);
            if (stream.is_open ()) {
                stream.close (websocket::close_code::normal, ignored);
            }
        }
        close_tcp_socket (stream.next_layer ());
    }

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
                throw detail::make_boundary_exception (detail::boundary_error_t::disconnected,
                                             "stream native socket closed");
            }
            const auto received = ::recv (fd, bytes.data () + offset, bytes.size () - offset, 0);
            const int received_errno = errno;
            if (received < 0) {
                if (received_errno == EINTR) {
                    continue;
                }
                throw detail::make_boundary_exception (detail::boundary_error_t::disconnected,
                                             std::strerror (received_errno));
            }
            if (received == 0) {
                throw detail::make_boundary_exception (detail::boundary_error_t::disconnected,
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

    frame_t read_frame (websocket_stream_t &socket)
    {
        beast::flat_buffer buffer;
        socket.read (buffer);
        if (socket.got_text ()) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM WebSocket messages must be binary");
        }
        const auto size = buffer.size ();
        if (size < 6) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM WebSocket frame prefix is incomplete");
        }
        std::vector<std::uint8_t> bytes (size);
        asio::buffer_copy (asio::buffer (bytes), buffer.data ());
        const auto header_size = static_cast<std::size_t> ((bytes[0] << 8) | bytes[1]);
        const auto payload_size = (static_cast<std::size_t> (bytes[2]) << 24)
                                  | (static_cast<std::size_t> (bytes[3]) << 16)
                                  | (static_cast<std::size_t> (bytes[4]) << 8)
                                  | static_cast<std::size_t> (bytes[5]);
        if (bytes.size () != 6 + header_size + payload_size) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM WebSocket message size does not match its prefix");
        }
        std::vector<std::uint8_t> header_bytes (bytes.begin () + 6,
                                                bytes.begin () + 6 + header_size);
        std::vector<std::uint8_t> payload_bytes (bytes.begin () + 6 + header_size, bytes.end ());
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

    void write_frame (websocket_stream_t &socket,
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
        socket.binary (true);
        socket.write (asio::buffer (frame));
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
                throw detail::make_boundary_exception (detail::boundary_error_t::disconnected,
                                             "stream native socket closed");
            }
            const auto sent = ::send (fd, bytes.data () + offset, bytes.size () - offset,
                                      MSG_NOSIGNAL);
            const int sent_errno = errno;
            if (sent < 0) {
                if (sent_errno == EINTR) {
                    continue;
                }
                throw detail::make_boundary_exception (detail::boundary_error_t::disconnected,
                                             std::strerror (sent_errno));
            }
            if (sent == 0) {
                throw detail::make_boundary_exception (detail::boundary_error_t::disconnected,
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
        if (draining ()) {
            /* graceful-drain-handoff §5: brand-new connections on a draining
             * node receive session-closing(server_drain) and are closed. */
            try {
                const auto payload_bytes = detail::stream_runtime_t::encode_session_closing_payload (
                  stream_close_reason_t::server_drain, "node is draining");
                detail::stream_header_t closing (stream_message_kind_t::control,
                                                 stream_codec_t::raw, stream_header_flags_t::none,
                                                 std::nullopt, "session-closing", {});
                const auto closing_payload = zlink::message_t::from (
                  std::string (payload_bytes.begin (), payload_bytes.end ()));
                if constexpr (std::is_same_v<TStream, native_tcp_connection_t>) {
                    write_frame_native (*connection, closing, closing_payload);
                } else {
                    write_frame (*connection, closing, closing_payload);
                }
            }
            catch (...) {
            }
            return;
        }
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
                      return detail::result_access_t::failure<void> (error);
                  }
                  catch (const std::exception &error) {
                      return detail::boundary_failure<void> (detail::boundary_error_t::disconnected,
                                                      error.what ());
                  }
              });
        }
        std::size_t flushed = 0;
        bool connected_session = false;
        auto liveness = std::make_shared<session_liveness_t> ();
        try {
            if (auto connected = _runtime.dispatch_connected (session, stream); !connected) {
                return;
            }
            connected_session = true;
            record_connection_opened ();
            // close_connection owns the _io_mutex acquisition itself.
            register_active_stream (stream, liveness,
                                    [this, connection] { close_connection (*connection); });
            flush_writes (*connection, stream, flushed, *write_mutex);
            while (!_stop->load (std::memory_order_acquire)) {
                auto frame = read_frame (*connection);
                if (frame.header.kind () == stream_message_kind_t::control) {
                    if (frame.header.packet_name () == "$zlink.heartbeat.pong") {
                        liveness->record_pong ();
                    } else if (frame.header.packet_name () == "$zlink.heartbeat.ping") {
                        detail::stream_header_t pong (stream_message_kind_t::control,
                                                      stream_codec_t::raw,
                                                      stream_header_flags_t::none, std::nullopt,
                                                      "$zlink.heartbeat.pong", {});
                        const std::lock_guard<std::mutex> lock (*write_mutex);
                        write_frame (*connection, pong, zlink::message_t{});
                    }
                    continue;
                }
                liveness->record_application_inbound ();
                trace_stream_host ("dispatch", _stream, frame.header);
                if (auto dispatched =
                      _runtime.dispatch_packet (session, stream, frame.header, frame.payload);
                    !dispatched) {
                    report_packet_dispatch_error (frame.header, dispatched);
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
                unregister_active_stream (stream);
                const auto forced = liveness->forced ();
                record_connection_closed (forced ? liveness_close_label (*forced)
                                                 : "client_close");
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
        if (draining ()) {
            /* graceful-drain-handoff §5: brand-new connections on a draining
             * node receive session-closing(server_drain) and are closed. */
            try {
                const auto payload_bytes = detail::stream_runtime_t::encode_session_closing_payload (
                  stream_close_reason_t::server_drain, "node is draining");
                detail::stream_header_t closing (stream_message_kind_t::control,
                                                 stream_codec_t::raw, stream_header_flags_t::none,
                                                 std::nullopt, "session-closing", {});
                const auto closing_payload = zlink::message_t::from (
                  std::string (payload_bytes.begin (), payload_bytes.end ()));
                write_frame_native (*connection, closing, closing_payload);
            }
            catch (...) {
            }
            return;
        }
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
                  return detail::result_access_t::failure<void> (error);
              }
              catch (const std::exception &error) {
                  return detail::boundary_failure<void> (detail::boundary_error_t::disconnected,
                                                  error.what ());
              }
          });
        std::size_t flushed = 0;
        bool connected_session = false;
        auto liveness = std::make_shared<session_liveness_t> ();
        try {
            if (auto connected = _runtime.dispatch_connected (session, stream); !connected) {
                return;
            }
            connected_session = true;
            record_connection_opened ();
            register_active_stream (stream, liveness,
                                    [this, connection] { close_connection (*connection); });
            flush_writes_native (*connection, stream, flushed, *write_mutex);
            while (!_stop->load (std::memory_order_acquire)) {
                auto frame = read_frame_native (*connection);
                if (frame.header.kind () == stream_message_kind_t::control) {
                    if (frame.header.packet_name () == "$zlink.heartbeat.pong") {
                        liveness->record_pong ();
                    } else if (frame.header.packet_name () == "$zlink.heartbeat.ping") {
                        detail::stream_header_t pong (stream_message_kind_t::control,
                                                      stream_codec_t::raw,
                                                      stream_header_flags_t::none, std::nullopt,
                                                      "$zlink.heartbeat.pong", {});
                        const std::lock_guard<std::mutex> lock (*write_mutex);
                        write_frame_native (*connection, pong, zlink::message_t{});
                    }
                    continue;
                }
                liveness->record_application_inbound ();
                trace_stream_host ("dispatch", _stream, frame.header);
                if (auto dispatched =
                      _runtime.dispatch_packet (session, stream, frame.header, frame.payload);
                    !dispatched) {
                    report_packet_dispatch_error (frame.header, dispatched);
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
                unregister_active_stream (stream);
                const auto forced = liveness->forced ();
                record_connection_closed (forced ? liveness_close_label (*forced)
                                                 : "client_close");
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

    void handle_websocket_connection (std::shared_ptr<tcp_connection_t> owner,
                                      std::shared_ptr<websocket_stream_t> connection)
    {
        try {
            connection->accept ();
            connection->binary (true);
        }
        catch (const boost::system::system_error &) {
            const std::lock_guard<std::mutex> lock (_sockets_mutex);
            _sockets.erase (&connection->next_layer ());
            return;
        }
        (void) owner;
        handle_stream_connection (connection, &connection->next_layer (), true);
    }

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
    std::shared_ptr<std::atomic_bool> _drain_flag;
    std::shared_ptr<framework::detail::monitoring_runtime_state_t> _monitoring;
    std::mutex _active_streams_mutex;
    std::vector<active_session_t> _active_streams;
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
          std::make_unique<listener_t> (_runtime, stream, factory->second, services, _stop,
                                        _drain_flag, _monitoring);
        auto *raw = listener.get ();
        _listeners.push_back (std::move (listener));
        _threads.emplace_back ([raw] { raw->run (); });
        raw->wait_started ();
    }
    if (!_listeners.empty ()) {
        /* One liveness sweep loop per node (graceful-drain-handoff §7.2):
         * the service drives every listener's sessions from a single
         * thread; no per-session timers exist. */
        _liveness_thread = std::thread ([this] {
            auto last_sweep = std::chrono::steady_clock::now ();
            while (!_stop.load (std::memory_order_acquire)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
                const auto now = std::chrono::steady_clock::now ();
                if (now - last_sweep < std::chrono::seconds (1)) {
                    continue;
                }
                last_sweep = now;
                for (const auto &listener : _listeners) {
                    if (listener) {
                        listener->sweep_liveness_once ();
                    }
                }
            }
        });
    }
}

void stream_host_service_t::notify_sessions_closing (stream_close_reason_t reason,
                                                     std::string_view diagnostic) noexcept
{
    for (const auto &listener : _listeners) {
        if (listener) {
            try {
                listener->notify_sessions_closing (reason, diagnostic);
            }
            catch (...) {
            }
        }
    }
}

void stream_host_service_t::force_close_sessions (stream_close_reason_t reason,
                                                  std::string_view diagnostic) noexcept
{
    for (const auto &listener : _listeners) {
        if (listener) {
            try {
                listener->force_close_sessions (reason, diagnostic);
            }
            catch (...) {
            }
        }
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
    if (_liveness_thread.joinable ()) {
        _liveness_thread.join ();
    }
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
