/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/streams/stream_host_service.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
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
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
        ,
        _tls_context (ssl::context::tls_server)
#endif
    {
    }

    void run ()
    {
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

        while (!_stop->load (std::memory_order_acquire)) {
            boost::system::error_code error;
            tcp::socket socket (_io);
            _acceptor.accept (socket, error);
            if (error || _stop->load (std::memory_order_acquire)) {
                continue;
            }
            trace_stream_host ("accept", _stream);
            auto connection = std::make_shared<tcp::socket> (std::move (socket));
            {
                const std::lock_guard<std::mutex> lock (_sockets_mutex);
                _sockets.insert (connection.get ());
            }
            if (stream_uses_tls (_stream)) {
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
                auto tls_connection = std::make_shared<ssl::stream<tcp::socket>> (
                  std::move (*connection), _tls_context);
                {
                    const std::lock_guard<std::mutex> lock (_sockets_mutex);
                    _sockets.erase (connection.get ());
                    _sockets.insert (&tls_connection->next_layer ());
                }
                {
                    const std::lock_guard<std::mutex> lock (_workers_mutex);
                    _workers.emplace_back (
                      [this, tls_connection] { handle_tls_connection (tls_connection); });
                }
#else
                connection->close ();
#endif
            } else {
                const std::lock_guard<std::mutex> lock (_workers_mutex);
                _workers.emplace_back ([this, connection] { handle_connection (connection); });
            }
        }
    }

    void stop () noexcept
    {
        try {
            const auto endpoint = parse_stream_endpoint (_stream);
            boost::system::error_code ignored;
            tcp::socket wakeup (_io);
            wakeup.connect (tcp::endpoint (asio::ip::make_address (endpoint.host),
                                           static_cast<unsigned short> (std::stoi (endpoint.port))),
                            ignored);
        }
        catch (...) {
        }
        boost::system::error_code ignored;
        _acceptor.close (ignored);
        {
            const std::lock_guard<std::mutex> lock (_sockets_mutex);
            for (auto *socket : _sockets) {
                socket->shutdown (tcp::socket::shutdown_both, ignored);
                socket->close (ignored);
            }
        }
        {
            const std::lock_guard<std::mutex> lock (_workers_mutex);
            for (auto &worker : _workers) {
                if (worker.joinable ()) {
                    worker.join ();
                }
            }
            _workers.clear ();
        }
    }

  private:
    struct frame_t
    {
        stream_header_t header;
        zlink::message_t payload;
    };

    void configure_tls_context ()
    {
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
        if (_stream.tls_certificate_file.empty () || _stream.tls_private_key_file.empty ()) {
            throw framework_exception_t (
              framework_error_kind_t::request_protocol_error,
              "STREAM TLS endpoint requires certificate and private key");
        }
        _tls_context.use_certificate_chain_file (_stream.tls_certificate_file);
        _tls_context.use_private_key_file (_stream.tls_private_key_file, ssl::context::pem);
#else
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM TLS support requires OpenSSL");
#endif
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
        std::vector<std::thread> dispatch_workers;
        try {
            if (auto connected = _runtime.dispatch_connected (session, stream); !connected) {
                return;
            }
            flush_writes (*connection, stream, flushed, *write_mutex);
            while (!_stop->load (std::memory_order_acquire)) {
                auto frame = read_frame (*connection);
                if (frame.header.kind () == stream_message_kind_t::control) {
                    continue;
                }
                trace_stream_host ("dispatch", _stream, frame.header);
                dispatch_workers.emplace_back (
                  [this, &session, &stream, connection, write_mutex, frame = std::move (frame)] {
                      try {
                          if (auto dispatched = _runtime.dispatch_packet (
                                session, stream, frame.header, frame.payload);
                              !dispatched) {
                              if (frame.header.kind () == stream_message_kind_t::request) {
                                  const std::lock_guard<std::mutex> lock (*write_mutex);
                                  write_error_frame (*connection, frame.header, dispatched);
                              }
                          }
                      }
                      catch (...) {
                      }
                  });
                flush_writes (*connection, stream, flushed, *write_mutex);
            }
            for (auto &worker : dispatch_workers) {
                if (worker.joinable ()) {
                    worker.join ();
                }
            }
        }
        catch (const boost::system::system_error &) {
        }
        catch (const framework_exception_t &) {
        }
        catch (...) {
        }
        for (auto &worker : dispatch_workers) {
            if (worker.joinable ()) {
                worker.join ();
            }
        }
        (void) _runtime.dispatch_disconnected (session, stream);
        try {
            flush_writes (*connection, stream, flushed, *write_mutex);
        }
        catch (...) {
        }
    }

#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
    void handle_tls_connection (std::shared_ptr<ssl::stream<tcp::socket>> connection)
    {
        try {
            connection->handshake (ssl::stream_base::server);
        }
        catch (const boost::system::system_error &) {
            const std::lock_guard<std::mutex> lock (_sockets_mutex);
            _sockets.erase (&connection->next_layer ());
            return;
        }
        handle_stream_connection (connection, &connection->next_layer (), true);
    }
#endif

    void handle_connection (std::shared_ptr<tcp::socket> connection)
    {
        handle_stream_connection (connection, connection.get (), true);
    }

    detail::stream_runtime_t _runtime;
    stream_snapshot_t _stream;
    detail::stream_session_factory_t _session_factory;
    service_provider_t *_services;
    std::atomic_bool *_stop;
    asio::io_context _io;
    tcp::acceptor _acceptor;
#ifdef ZLINK_FRAMEWORK_STREAM_WITH_OPENSSL
    ssl::context _tls_context;
#endif
    std::mutex _sockets_mutex;
    std::unordered_set<tcp::socket *> _sockets;
    std::mutex _workers_mutex;
    std::vector<std::thread> _workers;
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
    }
}

void stream_host_service_t::stop () noexcept
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
    _services = nullptr;
}

} // namespace zlink::framework::runtime
