#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
namespace websocket = boost::beast::websocket;
using tcp = asio::ip::tcp;

struct bench_result {
    double throughput;
    double latency_us;
};

class stopwatch_t {
public:
    void start() { _start = std::chrono::high_resolution_clock::now(); }
    double elapsed_ms() const {
        const auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - _start)
            .count();
    }

private:
    std::chrono::high_resolution_clock::time_point _start;
};

static void uint32_to_be(uint32_t val, unsigned char *buf) {
    buf[0] = static_cast<unsigned char>((val >> 24) & 0xFF);
    buf[1] = static_cast<unsigned char>((val >> 16) & 0xFF);
    buf[2] = static_cast<unsigned char>((val >> 8) & 0xFF);
    buf[3] = static_cast<unsigned char>(val & 0xFF);
}

static uint32_t be_to_uint32(const unsigned char *buf) {
    return (static_cast<uint32_t>(buf[0]) << 24)
        | (static_cast<uint32_t>(buf[1]) << 16)
        | (static_cast<uint32_t>(buf[2]) << 8)
        | static_cast<uint32_t>(buf[3]);
}

static int resolve_msg_count(size_t size) {
    int count = (size <= 1024) ? 200000 : 20000;
    if (const char *env = std::getenv("BENCH_MSG_COUNT")) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            count = static_cast<int>(override);
    }
    return count;
}

static int resolve_bench_count(const char *env_name, int default_value) {
    if (const char *env = std::getenv(env_name)) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            return static_cast<int>(override);
    }
    return default_value;
}

static double median(std::vector<double> values) {
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 0)
        return (values[mid - 1] + values[mid]) / 2.0;
    return values[mid];
}

namespace test_certs {

static const char *ca_cert_pem =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDlzCCAn+gAwIBAgIUbGLNLbwV7np9Q07zD9ZWvmA+nkAwDQYJKoZIhvcNAQEL\n"
  "BQAwWzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
  "FjAUBgNVBAoMDVpMaW5rIFRlc3QgQ0ExFjAUBgNVBAMMDVpMaW5rIFRlc3QgQ0Ew\n"
  "HhcNMjYwMTEyMTEyMjUzWhcNMzYwMTEwMTEyMjUzWjBbMQswCQYDVQQGEwJVUzEN\n"
  "MAsGA1UECAwEVGVzdDENMAsGA1UEBwwEVGVzdDEWMBQGA1UECgwNWkxpbmsgVGVz\n"
  "dCBDQTEWMBQGA1UEAwwNWkxpbmsgVGVzdCBDQTCCASIwDQYJKoZIhvcNAQEBBQAD\n"
  "ggEPADCCAQoCggEBAKHAdjzB5SsoFlce8T4XBvQa0LAbYP9hQ+jcLXSzoF/QDmeP\n"
  "sxGSE1WINM7ZT9BOqNa8OKl7kWWWYS45XeeqrNLVHDQbz9DvUAqUVaSsoxyAxCtV\n"
  "8Zq+F6Zy01qbLXi+Nv1jWz685X9KSc5SCKz9acoOSBU7IOtJKCQ+QM+/x9PMqQeg\n"
  "B+aRNkv+WE4RRLbpQnIGqSiZkUsNI6Z97o2otsHkGa1oVWWXmKqzUAmembVHjiCl\n"
  "Rn9Ut4/HqqopLn/k2m7/Lj62QT6sOcB8ixDe+H4TwDF6sbxgHcs/1sdobys6VsUF\n"
  "gFSJ5Dm33yYBjQmLfxXRaKMxKGukLmAofa+f28sCAwEAAaNTMFEwHQYDVR0OBBYE\n"
  "FO3BqMenuNdTJuCz5tywoNrd11KjMB8GA1UdIwQYMBaAFO3BqMenuNdTJuCz5tyw\n"
  "oNrd11KjMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBADF2GjWc\n"
  "BuvU/3bG2406XNFtl7pb4V70zClo269Gb/SYVrF0k6EXp2I8UQ7cPXM+ueWu8JeG\n"
  "XCbSTRADWxw702VxryCXLIYYMZ5hwF5ZtDGOagZQWSz38UFy2acCRNqY2ijyISQn\n"
  "3M8YtRdeEGOan+gtTC6/xB3IIRX1tFohT35G/wjld8hs6kJVokYhVfKhk4EZKSxH\n"
  "IiHsVaafpjUwm4EkAwCmwAWkOalKijbo5Jdq9h3UNfOn4RblN80FU/jD2cBFP+L8\n"
  "U/Juz13KFa/4NXp9flzUl/1w5o//V1UXUpfYOMsVT8BaP3dV1pa9lDwhoJERyiI1\n"
  "xj0kGsPBIt3nVwE=\n"
  "-----END CERTIFICATE-----\n";

static const char *server_cert_pem =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDrTCCApWgAwIBAgIUH3bva6lTINNSQ2BpgpJStZpT5NQwDQYJKoZIhvcNAQEL\n"
  "BQAwWzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
  "FjAUBgNVBAoMDVpMaW5rIFRlc3QgQ0ExFjAUBgNVBAMMDVpMaW5rIFRlc3QgQ0Ew\n"
  "HhcNMjYwMTEyMTEyMzAxWhcNMjcwMTEyMTEyMzAxWjBUMQswCQYDVQQGEwJVUzEN\n"
  "MAsGA1UECAwEVGVzdDENMAsGA1UEBwwEVGVzdDETMBEGA1UECgwKWkxpbmsgVGVz\n"
  "dDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"
  "CgKCAQEAxZ5FpHxoY5JaTfbS3D1nSlz+BdvnrsZ5PqG+P/H1oGXJnY/2MMZGEeUZ\n"
  "SZg9pVn6ZRURyGTwAHN1X+xarpX057pKfqWtHLztj2+WSJLbBfzSzwPdYNMP/h1C\n"
  "MX9zMbui6ui8Tbys1g5IKO/ZEMRN8bVNHOJ4xkK829RzEu6f/4YCuf4Lz+Z1X4en\n"
  "VBi7DGkWRSUiACjlGvVyZ24KHkLCggbAO3HhhyjZ4FwVd9JuE+d2/jm/neUu6HTt\n"
  "J/9d/5GCovUamkuYWn+e62HA1FkpSnXNbgRrkmAkOrliJG1uCqh3btVzuF1c91Jj\n"
  "8wjm0wm23lDeGVrCWExvyFhk3LBFCwIDAQABo3AwbjAsBgNVHREEJTAjgglsb2Nh\n"
  "bGhvc3SHBH8AAAGHEAAAAAAAAAAAAAAAAAAAAAEwHQYDVR0OBBYEFFrMgnC8k4I0\n"
  "XMjURlF0zXV59HJYMB8GA1UdIwQYMBaAFO3BqMenuNdTJuCz5tywoNrd11KjMA0G\n"
  "CSqGSIb3DQEBCwUAA4IBAQCcXiKLN5y7rumetdr55PMDdx+4EV1Wl28fWCOB5nur\n"
  "kFZRy876pFphFqZppjGCHWiiHzUIsZXUej/hBmY+OhsL13ojfGiACz/44OFzqCUa\n"
  "I83V1M9ywbty09zhdqFc9DFfpiC2+ltDCn7o+eF7THUzgDg4fRZYHYM1njZElZaG\n"
  "ecFImsQzqFIpmhB/TfZIZVmBQryYN+V1fl4sUJFiYEOr49RjWnATf6RKY3J5VKHp\n"
  "TWSm7rTd4jB0CvyNlPpS+fYBdGC72m6R3zrce8Scfto+HPH4YdIU5AdoRHCCtOrA\n"
  "Mq9brLTPUzAqlzC7zDw41hI/MS1Cdcxb1dZkKHgMXu8W\n"
  "-----END CERTIFICATE-----\n";

static const char *server_key_pem =
  "-----BEGIN PRIVATE KEY-----\n"
  "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDFnkWkfGhjklpN\n"
  "9tLcPWdKXP4F2+euxnk+ob4/8fWgZcmdj/YwxkYR5RlJmD2lWfplFRHIZPAAc3Vf\n"
  "7FqulfTnukp+pa0cvO2Pb5ZIktsF/NLPA91g0w/+HUIxf3Mxu6Lq6LxNvKzWDkgo\n"
  "79kQxE3xtU0c4njGQrzb1HMS7p//hgK5/gvP5nVfh6dUGLsMaRZFJSIAKOUa9XJn\n"
  "bgoeQsKCBsA7ceGHKNngXBV30m4T53b+Ob+d5S7odO0n/13/kYKi9RqaS5haf57r\n"
  "YcDUWSlKdc1uBGuSYCQ6uWIkbW4KqHdu1XO4XVz3UmPzCObTCbbeUN4ZWsJYTG/I\n"
  "WGTcsEULAgMBAAECggEACAoWclsKcmqN71yaf7ZbyBZBP95XW9UAn7byx25UDn5H\n"
  "3woUsgr8nehSyJuIx6CULMKPGVs3lXP4bpXbqyG4CeAss/H+XeekkL5D0nO4IsE5\n"
  "BSBkaL/Wh275kbCA8HyU9gAZkQLkZbPFCb+XCKLfOpntcHWGut2CLs/VVzCLbX1A\n"
  "hHerqJf3qEW+cU1Va5On+A2BEK7XtYFIR6IabS2LN5ecoZUfQ4EoeypdpQPRKwqM\n"
  "m1tSet4CsRfovguLdY5Z/hAhFLZCMKF5zs8zzGln9+S+G5y2fdJ4VxwbeR0OqyAh\n"
  "cB56xJo3L7rLm6hAoIb0mVXaiyRRGEuCBE/t9/pmSQKBgQD2hQgHpC20bQCyh08B\n"
  "1CyJKz1ObZJeYCWR6hE0stUKKq9QizY9Ci8Q1Hg8eEAtKCKjW74DbJ7bgGJBm6rS\n"
  "yNgpZZ3zw6NDSm4wY33y4alB5jzMR+H7izb6vxMPVcXn3DpjzoklxkN4l8JvgTbt\n"
  "KxZWxD3hS+C6NuNKE4LHipJO1wKBgQDNN89O/71ktIBpxiEZk4sKzdq3JZMErFBi\n"
  "cFJ4vATJ1LstrWdOAtOgRqQN81GhCSZ79vybrcOaq4Q4qLzsOWrAo7nb53gq684Y\n"
  "GaVAZfxzA+qECyEY3CzrKnwIbSFvJY+IfA1QL/ricce8oL7lIRIP1+MuhvGUdw55\n"
  "vXs01Wv47QKBgDo1sW60esJW1spRHvvMkPOWzTQetWgphdWNkqCB9cIf0CPRq24A\n"
  "YJq1wOpubqD7ECrIt/ZxCJXGG+1oB48cM8aaoxBzSrLR+XDdnVjjpibUadjGxHq0\n"
  "JbhRs/t0AnY8T2FP3JyZ00a/dv8DYOfhu7WjQwVW+GqgGU1djAz4EJIjAoGBAJe+\n"
  "iOBVYmowvjN4eck7vDiE9xEuC4QNFnNzssfr326Oism/yv94P5voIC7gmJ+G8JoB\n"
  "i9BhsJ2R7fcnbmsOGc3QQwJEKisyqfZQIE16HC2/240/3X1QcTaC96wTZgGVuIin\n"
  "kgCVOeJvV8423nD2/zAP5sDkr4Wkc2O5pHzwwyIRAoGAID2/HQQbczTqQlEAXltB\n"
  "K8YbNLP75FY+9w10SH3B0hUnEP+9YdeHvxkXdWtewn+TjkXnc3AYlb9A9u7GUuB+\n"
  "K2AF/TMl2YdHFOEDtMAZ8IT6womo6JHYj4+FfbxPiMmOfBmOKrdxQ/WrqfCnZwEs\n"
  "Dhpkrp6xWJWSNvXS0XcWGfM=\n"
  "-----END PRIVATE KEY-----\n";

}  // namespace test_certs

static std::string write_temp_cert(const char *content, const char *suffix) {
    std::string path = "/tmp/bench_beast_";
    path += suffix;
    path += ".pem";
    std::ofstream ofs(path.c_str(), std::ios::out | std::ios::trunc);
    if (ofs)
        ofs << content;
    return path;
}

struct tls_paths_t {
    std::string ca;
    std::string cert;
    std::string key;
};

static const tls_paths_t &get_tls_paths() {
    static tls_paths_t paths = {
        write_temp_cert(test_certs::ca_cert_pem, "ca"),
        write_temp_cert(test_certs::server_cert_pem, "server_cert"),
        write_temp_cert(test_certs::server_key_pem, "server_key")
    };
    return paths;
}

static void setup_server_ctx(ssl::context &ctx) {
    const tls_paths_t &paths = get_tls_paths();
    ctx.set_options(ssl::context::default_workarounds
                    | ssl::context::no_sslv2
                    | ssl::context::no_sslv3);
    ctx.use_certificate_chain_file(paths.cert.c_str());
    ctx.use_private_key_file(paths.key.c_str(), ssl::context::pem);
}

static void setup_client_ctx(ssl::context &ctx) {
    const tls_paths_t &paths = get_tls_paths();
    ctx.set_verify_mode(ssl::verify_peer);
    ctx.load_verify_file(paths.ca.c_str());
}

template <typename Stream>
static void send_stream_msg(Stream &stream,
                            const std::vector<char> &payload,
                            const std::array<asio::const_buffer, 2> &bufs) {
    (void)payload;
    asio::write(stream, bufs);
}

template <typename Stream>
static void recv_stream_msg(Stream &stream, std::vector<char> &payload) {
    unsigned char header[4];
    asio::read(stream, asio::buffer(header, sizeof(header)));
    const uint32_t len = be_to_uint32(header);
    if (len > payload.size())
        payload.resize(len);
    if (len > 0)
        asio::read(stream, asio::buffer(payload.data(), len));
}

template <typename WsStream>
static void configure_ws(WsStream &ws, size_t msg_size) {
    ws.binary(true);
    ws.auto_fragment(false);
    ws.read_message_max(msg_size + 64);
}

template <typename WsStream>
static void ws_send_msg(WsStream &ws, const std::vector<char> &payload) {
    ws.write(asio::buffer(payload));
}

template <typename WsStream>
static void ws_recv_msg(WsStream &ws, boost::beast::flat_buffer &buffer) {
    buffer.consume(buffer.size());
    ws.read(buffer);
}

static bench_result run_tcp(size_t msg_size,
                            int msg_count,
                            int warmup_count,
                            int lat_count) {
    asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0));
    const tcp::endpoint endpoint = acceptor.local_endpoint();

    std::exception_ptr server_error;
    std::thread server([&]() {
        try {
            tcp::socket sock(server_io);
            acceptor.accept(sock);
            sock.set_option(tcp::no_delay(true));
            std::vector<char> recv_buf(msg_size);
            std::vector<char> echo_buf(msg_size, 'b');
            unsigned char header_raw[4];
            uint32_to_be(static_cast<uint32_t>(msg_size), header_raw);
            std::array<asio::const_buffer, 2> echo_bufs = {
                asio::buffer(header_raw, sizeof(header_raw)),
                asio::buffer(echo_buf)
            };

            for (int i = 0; i < warmup_count; ++i)
                recv_stream_msg(sock, recv_buf);

            for (int i = 0; i < lat_count; ++i) {
                recv_stream_msg(sock, recv_buf);
                send_stream_msg(sock, echo_buf, echo_bufs);
            }

            for (int i = 0; i < msg_count; ++i)
                recv_stream_msg(sock, recv_buf);
        } catch (...) {
            server_error = std::current_exception();
        }
    });

    asio::io_context client_io;
    tcp::socket client(client_io);
    client.connect(endpoint);
    client.set_option(tcp::no_delay(true));

    std::vector<char> payload(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    unsigned char header_raw[4];
    uint32_to_be(static_cast<uint32_t>(msg_size), header_raw);
    std::array<asio::const_buffer, 2> bufs = {
        asio::buffer(header_raw, sizeof(header_raw)),
        asio::buffer(payload)
    };

    for (int i = 0; i < warmup_count; ++i)
        send_stream_msg(client, payload, bufs);

    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        send_stream_msg(client, payload, bufs);
        recv_stream_msg(client, recv_buf);
    }
    const double latency_us = (sw.elapsed_ms() * 1000.0)
        / (lat_count * 2);

    sw.start();
    for (int i = 0; i < msg_count; ++i)
        send_stream_msg(client, payload, bufs);
    server.join();
    const double throughput = static_cast<double>(msg_count)
        / (sw.elapsed_ms() / 1000.0);

    if (server_error)
        std::rethrow_exception(server_error);

    bench_result result = {throughput, latency_us};
    return result;
}

#if defined(BEAST_HAVE_TLS)
static bench_result run_tls(size_t msg_size,
                            int msg_count,
                            int warmup_count,
                            int lat_count) {
    asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0));
    const tcp::endpoint endpoint = acceptor.local_endpoint();

    std::exception_ptr server_error;
    std::thread server([&]() {
        try {
            ssl::context ctx(ssl::context::tlsv12_server);
            setup_server_ctx(ctx);
            ssl::stream<tcp::socket> stream(server_io, ctx);
            acceptor.accept(stream.next_layer());
            stream.next_layer().set_option(tcp::no_delay(true));
            stream.handshake(ssl::stream_base::server);

            std::vector<char> recv_buf(msg_size);
            std::vector<char> echo_buf(msg_size, 'b');
            unsigned char header_raw[4];
            uint32_to_be(static_cast<uint32_t>(msg_size), header_raw);
            std::array<asio::const_buffer, 2> echo_bufs = {
                asio::buffer(header_raw, sizeof(header_raw)),
                asio::buffer(echo_buf)
            };

            for (int i = 0; i < warmup_count; ++i)
                recv_stream_msg(stream, recv_buf);

            for (int i = 0; i < lat_count; ++i) {
                recv_stream_msg(stream, recv_buf);
                send_stream_msg(stream, echo_buf, echo_bufs);
            }

            for (int i = 0; i < msg_count; ++i)
                recv_stream_msg(stream, recv_buf);
        } catch (...) {
            server_error = std::current_exception();
        }
    });

    asio::io_context client_io;
    ssl::context ctx(ssl::context::tlsv12_client);
    setup_client_ctx(ctx);
    ssl::stream<tcp::socket> client(client_io, ctx);
    client.next_layer().connect(endpoint);
    client.next_layer().set_option(tcp::no_delay(true));
    client.handshake(ssl::stream_base::client);

    std::vector<char> payload(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    unsigned char header_raw[4];
    uint32_to_be(static_cast<uint32_t>(msg_size), header_raw);
    std::array<asio::const_buffer, 2> bufs = {
        asio::buffer(header_raw, sizeof(header_raw)),
        asio::buffer(payload)
    };

    for (int i = 0; i < warmup_count; ++i)
        send_stream_msg(client, payload, bufs);

    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        send_stream_msg(client, payload, bufs);
        recv_stream_msg(client, recv_buf);
    }
    const double latency_us = (sw.elapsed_ms() * 1000.0)
        / (lat_count * 2);

    sw.start();
    for (int i = 0; i < msg_count; ++i)
        send_stream_msg(client, payload, bufs);
    server.join();
    const double throughput = static_cast<double>(msg_count)
        / (sw.elapsed_ms() / 1000.0);

    if (server_error)
        std::rethrow_exception(server_error);

    bench_result result = {throughput, latency_us};
    return result;
}
#endif

static bench_result run_ws(size_t msg_size,
                           int msg_count,
                           int warmup_count,
                           int lat_count) {
    asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0));
    const tcp::endpoint endpoint = acceptor.local_endpoint();

    std::exception_ptr server_error;
    std::thread server([&]() {
        try {
            tcp::socket sock(server_io);
            acceptor.accept(sock);
            sock.set_option(tcp::no_delay(true));
            websocket::stream<tcp::socket> ws(std::move(sock));
            configure_ws(ws, msg_size);
            ws.accept();

            boost::beast::flat_buffer buffer;
            std::vector<char> echo_buf(msg_size, 'b');

            for (int i = 0; i < warmup_count; ++i)
                ws_recv_msg(ws, buffer);

            for (int i = 0; i < lat_count; ++i) {
                ws_recv_msg(ws, buffer);
                ws_send_msg(ws, echo_buf);
            }

            for (int i = 0; i < msg_count; ++i)
                ws_recv_msg(ws, buffer);
        } catch (...) {
            server_error = std::current_exception();
        }
    });

    asio::io_context client_io;
    tcp::socket sock(client_io);
    sock.connect(endpoint);
    sock.set_option(tcp::no_delay(true));
    websocket::stream<tcp::socket> ws(std::move(sock));
    configure_ws(ws, msg_size);
    ws.handshake("127.0.0.1", "/");

    std::vector<char> payload(msg_size, 'a');
    boost::beast::flat_buffer buffer;

    for (int i = 0; i < warmup_count; ++i)
        ws_send_msg(ws, payload);

    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        ws_send_msg(ws, payload);
        ws_recv_msg(ws, buffer);
    }
    const double latency_us = (sw.elapsed_ms() * 1000.0)
        / (lat_count * 2);

    sw.start();
    for (int i = 0; i < msg_count; ++i)
        ws_send_msg(ws, payload);
    server.join();
    const double throughput = static_cast<double>(msg_count)
        / (sw.elapsed_ms() / 1000.0);

    if (server_error)
        std::rethrow_exception(server_error);

    bench_result result = {throughput, latency_us};
    return result;
}

#if defined(BEAST_HAVE_TLS)
static bench_result run_wss(size_t msg_size,
                            int msg_count,
                            int warmup_count,
                            int lat_count) {
    asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0));
    const tcp::endpoint endpoint = acceptor.local_endpoint();

    std::exception_ptr server_error;
    std::thread server([&]() {
        try {
            ssl::context ctx(ssl::context::tlsv12_server);
            setup_server_ctx(ctx);
            websocket::stream<ssl::stream<tcp::socket> > ws(server_io,
                                                            ctx);
            acceptor.accept(ws.next_layer().next_layer());
            ws.next_layer().next_layer().set_option(tcp::no_delay(true));
            ws.next_layer().handshake(ssl::stream_base::server);
            configure_ws(ws, msg_size);
            ws.accept();

            boost::beast::flat_buffer buffer;
            std::vector<char> echo_buf(msg_size, 'b');

            for (int i = 0; i < warmup_count; ++i)
                ws_recv_msg(ws, buffer);

            for (int i = 0; i < lat_count; ++i) {
                ws_recv_msg(ws, buffer);
                ws_send_msg(ws, echo_buf);
            }

            for (int i = 0; i < msg_count; ++i)
                ws_recv_msg(ws, buffer);
        } catch (...) {
            server_error = std::current_exception();
        }
    });

    asio::io_context client_io;
    ssl::context ctx(ssl::context::tlsv12_client);
    setup_client_ctx(ctx);
    websocket::stream<ssl::stream<tcp::socket> > ws(client_io, ctx);
    ws.next_layer().next_layer().connect(endpoint);
    ws.next_layer().next_layer().set_option(tcp::no_delay(true));
    ws.next_layer().handshake(ssl::stream_base::client);
    configure_ws(ws, msg_size);
    ws.handshake("127.0.0.1", "/");

    std::vector<char> payload(msg_size, 'a');
    boost::beast::flat_buffer buffer;

    for (int i = 0; i < warmup_count; ++i)
        ws_send_msg(ws, payload);

    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        ws_send_msg(ws, payload);
        ws_recv_msg(ws, buffer);
    }
    const double latency_us = (sw.elapsed_ms() * 1000.0)
        / (lat_count * 2);

    sw.start();
    for (int i = 0; i < msg_count; ++i)
        ws_send_msg(ws, payload);
    server.join();
    const double throughput = static_cast<double>(msg_count)
        / (sw.elapsed_ms() / 1000.0);

    if (server_error)
        std::rethrow_exception(server_error);

    bench_result result = {throughput, latency_us};
    return result;
}
#endif

static void print_result(const std::string &transport,
                         size_t msg_size,
                         const bench_result &result) {
    std::cout << "RESULT,beast,STREAM," << transport << "," << msg_size
              << ",throughput," << result.throughput << std::endl;
    std::cout << "RESULT,beast,STREAM," << transport << "," << msg_size
              << ",latency," << result.latency_us << std::endl;
}

static void usage() {
    std::cerr << "Usage: bench_beast_stream <transport> <size> "
                 "[--runs N]\n";
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage();
        return 1;
    }

    std::string transport = argv[1];
    const size_t msg_size =
        static_cast<size_t>(std::strtoul(argv[2], NULL, 10));

    int runs = 1;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            runs = std::atoi(argv[i + 1]);
            ++i;
        } else if (std::strncmp(argv[i], "--runs=", 7) == 0) {
            runs = std::atoi(argv[i] + 7);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
    }

    if (runs < 1)
        runs = 1;

    const int msg_count = resolve_msg_count(msg_size);
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);

#if !defined(BEAST_HAVE_TLS)
    if (transport == "tls" || transport == "wss") {
        std::cerr << "TLS/WSS not available in this build." << std::endl;
        return 1;
    }
#endif

    std::vector<double> throughputs;
    std::vector<double> latencies;
    throughputs.reserve(runs);
    latencies.reserve(runs);

    try {
        for (int i = 0; i < runs; ++i) {
            bench_result result;
            if (transport == "tcp") {
                result = run_tcp(msg_size, msg_count, warmup_count, lat_count);
            } else if (transport == "ws") {
                result = run_ws(msg_size, msg_count, warmup_count, lat_count);
#if defined(BEAST_HAVE_TLS)
            } else if (transport == "tls") {
                result = run_tls(msg_size, msg_count, warmup_count, lat_count);
            } else if (transport == "wss") {
                result = run_wss(msg_size, msg_count, warmup_count, lat_count);
#endif
            } else {
                std::cerr << "Unknown transport: " << transport << std::endl;
                return 1;
            }
            throughputs.push_back(result.throughput);
            latencies.push_back(result.latency_us);
        }
    } catch (const std::exception &ex) {
        std::cerr << "benchmark failed: " << ex.what() << std::endl;
        return 1;
    }

    bench_result final_result;
    final_result.throughput = median(throughputs);
    final_result.latency_us = median(latencies);
    print_result(transport, msg_size, final_result);
    return 0;
}
