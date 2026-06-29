/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/http/http_host_service.hpp"
#include "runtime/http/http_request_pipeline.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#ifdef ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/ssl.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <utility>

namespace zlink::framework::runtime
{
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class http_host_service_t::listener_t
{
  public:
    listener_t (const http_endpoint_t &endpoint,
                const http_options_snapshot_t &options,
                health_builder_t &health,
                service_provider_t &services,
                std::atomic_bool &stop) :
        _endpoint (&endpoint),
        _options (&options),
        _health (&health),
        _services (&services),
        _stop (&stop),
        _active_connections (0),
        _active_requests (0),
        _io_workers (std::max<std::size_t> (2, std::thread::hardware_concurrency ())),
        _acceptor (_io)
    {
    }

    ~listener_t () { stop_workers (); }

    void run ()
    {
        try {
            _parsed = parse_http_endpoint (_endpoint->uri);
            tcp::resolver resolver (_io);
            const auto endpoints = resolver.resolve (_parsed.host, _parsed.port);
            _acceptor.open (endpoints.begin ()->endpoint ().protocol ());
            _acceptor.set_option (tcp::acceptor::reuse_address (true));
            _acceptor.bind (endpoints.begin ()->endpoint ());
            _acceptor.listen ();
            configure_tls_context ();
        }
        catch (const boost::system::system_error &) {
            if (_stop->load (std::memory_order_acquire)) {
                return;
            }
            throw;
        }

        while (!_stop->load (std::memory_order_acquire)) {
            beast::error_code ec;
            tcp::socket socket (_io);
            _acceptor.accept (socket, ec);
            if (ec) {
                continue;
            }
            if (_stop->load (std::memory_order_acquire)) {
                socket.close (ec);
                break;
            }
            if (_active_connections.load (std::memory_order_acquire)
                >= _options->server.max_connections) {
                reject_overloaded (std::move (socket));
                continue;
            }
            _active_connections.fetch_add (1, std::memory_order_acq_rel);
            boost::asio::post (_io_workers, [this, socket = std::move (socket)] () mutable {
                auto guard = std::unique_ptr<void, void (*) (void *)> (this, [] (void *listener) {
                    static_cast<listener_t *> (listener)->_active_connections.fetch_sub (
                      1, std::memory_order_acq_rel);
                });
                if (_parsed.scheme == "https") {
                    handle_https (std::move (socket));
                } else {
                    handle_http (std::move (socket));
                }
            });
        }
    }

    void stop () noexcept
    {
        try {
            beast::error_code ignored;
            tcp::socket wakeup (_io);
            wakeup.connect (tcp::endpoint (asio::ip::make_address (_parsed.host),
                                           static_cast<unsigned short> (std::stoi (_parsed.port))),
                            ignored);
        }
        catch (...) {
        }
        beast::error_code ignored;
        _acceptor.close (ignored);
        (void) wait_for_active_requests (_options->server.graceful_shutdown_timeout);
        close_open_connections ();
        wait_for_workers ();
    }

  private:
    void configure_tls_context ()
    {
#ifdef ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL
        if (_parsed.scheme != "https") {
            return;
        }
        _tls_context.emplace (asio::ssl::context::tls_server);
        _tls_context->use_certificate_chain_file (_endpoint->tls->certificate_file);
        _tls_context->use_private_key_file (_endpoint->tls->private_key_file,
                                            asio::ssl::context::pem);
#endif
    }

    void stop_workers () noexcept
    {
        stop_and_join_workers ();
    }

    void wait_for_workers () noexcept
    {
        stop_and_join_workers ();
    }

    void stop_and_join_workers () noexcept
    {
        std::lock_guard lock (_worker_stop_mutex);
        if (_workers_stopped.load (std::memory_order_acquire)) {
            return;
        }
        _workers_stopped.store (true, std::memory_order_release);
        _io_workers.stop ();
        _io_workers.join ();
    }

    bool wait_for_active_requests (std::chrono::milliseconds timeout) const noexcept
    {
        if (timeout <= std::chrono::milliseconds::zero ()) {
            return _active_requests.load (std::memory_order_acquire) == 0;
        }
        const auto deadline = std::chrono::steady_clock::now () + timeout;
        while (_active_requests.load (std::memory_order_acquire) != 0
               && std::chrono::steady_clock::now () < deadline) {
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        return _active_requests.load (std::memory_order_acquire) == 0;
    }

    // Keep-alive clients hold connections open between requests, and the
    // worker's synchronous read cannot be cancelled by a timer. Stop must
    // shut the open sockets down so blocked workers unblock and join.
    void close_open_connections () noexcept
    {
        const std::lock_guard<std::mutex> lock (_sockets_mutex);
        for (auto *socket : _sockets) {
            beast::error_code ignored;
            socket->shutdown (tcp::socket::shutdown_both, ignored);
        }
    }

    class connection_registration_t
    {
      public:
        connection_registration_t (listener_t &listener, tcp::socket &socket) :
            _listener (listener), _socket (socket)
        {
            const std::lock_guard<std::mutex> lock (_listener._sockets_mutex);
            _listener._sockets.insert (&_socket);
            if (_listener._stop->load (std::memory_order_acquire)) {
                beast::error_code ignored;
                _socket.shutdown (tcp::socket::shutdown_both, ignored);
            }
        }

        ~connection_registration_t ()
        {
            const std::lock_guard<std::mutex> lock (_listener._sockets_mutex);
            _listener._sockets.erase (&_socket);
        }

        connection_registration_t (const connection_registration_t &) = delete;
        connection_registration_t &operator= (const connection_registration_t &) = delete;

      private:
        listener_t &_listener;
        tcp::socket &_socket;
    };

    void reject_overloaded (tcp::socket socket)
    {
        if (_parsed.scheme != "http") {
            beast::error_code ignored;
            socket.shutdown (tcp::socket::shutdown_both, ignored);
            return;
        }
        beast::error_code ec;
        auto response = make_http_status_response (http::status::service_unavailable, 11,
                                                   R"({"error":"server overloaded"})", false);
        http::write (socket, response, ec);
        socket.shutdown (tcp::socket::shutdown_send, ec);
    }

    template <typename TStream> void set_request_timeout (TStream &, std::chrono::milliseconds) {}

    void set_request_timeout (beast::tcp_stream &stream, std::chrono::milliseconds timeout)
    {
        stream.expires_after (timeout);
    }

#ifdef ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL
    void set_request_timeout (beast::ssl_stream<beast::tcp_stream> &stream,
                              std::chrono::milliseconds timeout)
    {
        stream.next_layer ().expires_after (timeout);
    }
#endif

    template <typename TStream> bool serve_requests (TStream &stream)
    {
        beast::flat_buffer buffer;
        std::size_t served = 0;
        while (!_stop->load (std::memory_order_acquire)
               && served < _options->server.max_keep_alive_requests) {
            http::request_parser<http::string_body> parser;
            parser.body_limit (_options->server.max_request_body_size);
            parser.header_limit (_options->server.max_header_size);
            beast::error_code ec;
            set_request_timeout (stream, served == 0 ? _options->server.request_headers_timeout
                                                     : _options->server.keep_alive_timeout);
            http::read_header (stream, buffer, parser, ec);
            if (ec == http::error::end_of_stream) {
                return true;
            }
            if (ec) {
                auto response = make_http_parser_error_response (ec, 11);
                http::write (stream, response, ec);
                return false;
            }
            if (parser.content_length ()
                && *parser.content_length () > _options->server.max_request_body_size) {
                auto response =
                  make_http_status_response (http::status::payload_too_large, 11,
                                             R"({"error":"request body too large"})", false);
                http::write (stream, response, ec);
                return false;
            }
            set_request_timeout (stream, _options->server.request_body_timeout);
            http::read (stream, buffer, parser, ec);
            if (ec) {
                auto response = make_http_parser_error_response (ec, 11);
                http::write (stream, response, ec);
                return false;
            }
            auto request = parser.release ();
            _active_requests.fetch_add (1, std::memory_order_acq_rel);
            auto request_guard = std::unique_ptr<void, void (*) (void *)> (
              this, [] (void *listener) {
                  static_cast<listener_t *> (listener)->_active_requests.fetch_sub (
                    1, std::memory_order_acq_rel);
              });
            auto response = handle_http_request (*_options, *_services, *_health, request);
            request_guard.reset ();
            response.keep_alive (request.keep_alive ()
                                 && served + 1 < _options->server.max_keep_alive_requests
                                 && !_stop->load (std::memory_order_acquire));
            set_request_timeout (stream, _options->server.write_timeout);
            http::write (stream, response, ec);
            if (ec || !response.keep_alive ()) {
                return false;
            }
            ++served;
        }
        return true;
    }

    void handle_http (tcp::socket socket)
    {
        beast::tcp_stream stream (std::move (socket));
        connection_registration_t registration (*this, stream.socket ());
        beast::error_code ec;
        serve_requests (stream);
        stream.socket ().shutdown (tcp::socket::shutdown_send, ec);
    }

    void handle_https (tcp::socket socket)
    {
#ifdef ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL
        if (!_tls_context) {
            return;
        }
        beast::ssl_stream<beast::tcp_stream> stream (std::move (socket), *_tls_context);
        connection_registration_t registration (*this, stream.next_layer ().socket ());
        beast::error_code ec;
        set_request_timeout (stream, _options->server.request_headers_timeout);
        stream.handshake (asio::ssl::stream_base::server, ec);
        if (ec) {
            return;
        }
        serve_requests (stream);
        stream.shutdown (ec);
#else
        (void) socket;
#endif
    }

    const http_endpoint_t *_endpoint;
    const http_options_snapshot_t *_options;
    health_builder_t *_health;
    service_provider_t *_services;
    std::atomic_bool *_stop;
    std::atomic_size_t _active_connections;
    std::atomic_size_t _active_requests;
    std::atomic_bool _workers_stopped{false};
    std::mutex _worker_stop_mutex;
    std::mutex _sockets_mutex;
    std::unordered_set<tcp::socket *> _sockets;
    parsed_http_endpoint_t _parsed;
    asio::io_context _io;
    asio::thread_pool _io_workers;
    tcp::acceptor _acceptor;
#ifdef ZLINK_FRAMEWORK_HTTP_WITH_OPENSSL
    std::optional<asio::ssl::context> _tls_context;
#endif
};

http_host_service_t::http_host_service_t (http_options_snapshot_t options,
                                          health_builder_t &health) :
    _options (std::move (options)), _health (&health)
{
}

http_host_service_t::~http_host_service_t () = default;

void http_host_service_t::start (service_provider_t &services)
{
    _stop.store (false, std::memory_order_release);
    for (const auto &endpoint : _options.endpoints) {
        auto listener =
          std::make_unique<listener_t> (endpoint, _options, *_health, services, _stop);
        auto *raw = listener.get ();
        _listeners.push_back (std::move (listener));
        _threads.emplace_back ([raw] { raw->run (); });
    }
}

void http_host_service_t::stop () noexcept
{
    _stop.store (true, std::memory_order_release);
    for (auto &listener : _listeners) {
        listener->stop ();
    }
    for (auto &thread : _threads) {
        if (thread.joinable ()) {
            thread.join ();
        }
    }
    _threads.clear ();
    _listeners.clear ();
}

} // namespace zlink::framework::runtime
