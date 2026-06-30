/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/pubsub_contracts.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace zlink::framework::e2e::pubsub::client
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

inline void touch_file (const std::string &path)
{
    if (path.empty ()) {
        return;
    }
    std::ofstream file (path);
    file << "ready\n";
}

inline void wait_for_file (const std::string &path)
{
    if (path.empty ()) {
        return;
    }
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (std::filesystem::exists (path)) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    throw std::runtime_error ("timed out waiting for " + path);
}

struct http_endpoint_t
{
    std::string host;
    std::string port;
};

inline http_endpoint_t parse_http_endpoint (const std::string &endpoint)
{
    constexpr const char *prefix = "http://";
    if (endpoint.rfind (prefix, 0) != 0) {
        throw std::runtime_error ("publisher URL must start with http://");
    }
    const auto host_start = std::string (prefix).size ();
    const auto port_separator = endpoint.find (':', host_start);
    if (port_separator == std::string::npos) {
        throw std::runtime_error ("publisher URL must include a port");
    }
    auto path_separator = endpoint.find ('/', port_separator + 1);
    if (path_separator == std::string::npos) {
        path_separator = endpoint.size ();
    }
    return {.host = endpoint.substr (host_start, port_separator - host_start),
            .port = endpoint.substr (port_separator + 1, path_separator - port_separator - 1)};
}

inline std::string url_encode (const std::string &value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
            || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back (static_cast<char> (ch));
        } else {
            encoded.push_back ('%');
            encoded.push_back (hex[ch >> 4]);
            encoded.push_back (hex[ch & 0x0F]);
        }
    }
    return encoded;
}

inline void post_empty (const std::string &base_url, const std::string &path_and_query)
{
    const auto endpoint = parse_http_endpoint (base_url);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *resolved = nullptr;
    if (getaddrinfo (endpoint.host.c_str (), endpoint.port.c_str (), &hints, &resolved) != 0) {
        throw std::runtime_error ("failed to resolve " + endpoint.host + ":" + endpoint.port);
    }
    int socket_fd = -1;
    for (auto *address = resolved; address != nullptr; address = address->ai_next) {
        socket_fd = socket (address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }
        if (connect (socket_fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        close (socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo (resolved);
    if (socket_fd < 0) {
        throw std::runtime_error ("failed to connect to publisher HTTP endpoint");
    }

    const auto request = "POST " + path_and_query + " HTTP/1.1\r\nHost: " + endpoint.host + ":"
      + endpoint.port + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    const char *cursor = request.data ();
    auto remaining = request.size ();
    while (remaining > 0) {
        const auto sent = send (socket_fd, cursor, remaining, 0);
        if (sent <= 0) {
            close (socket_fd);
            throw std::runtime_error ("failed to send publisher HTTP request");
        }
        cursor += sent;
        remaining -= static_cast<std::size_t> (sent);
    }

    std::string response;
    char buffer[1024];
    while (true) {
        const auto received = recv (socket_fd, buffer, sizeof (buffer), 0);
        if (received < 0) {
            close (socket_fd);
            throw std::runtime_error ("failed to read publisher HTTP response");
        }
        if (received == 0) {
            break;
        }
        response.append (buffer, static_cast<std::size_t> (received));
    }
    close (socket_fd);
    if (response.rfind ("HTTP/1.1 200", 0) != 0 && response.rfind ("HTTP/1.0 200", 0) != 0) {
        throw std::runtime_error ("publisher HTTP request failed: " + response.substr (0, 80));
    }
}

inline void publish (const std::string &publisher_url,
                     const std::string &topic,
                     const std::string &value,
                     bool missing_packet = false)
{
    const auto path = std::string (missing_packet ? "/publish/missing" : "/publish/event")
      + "?topic=" + url_encode (topic) + "&value=" + url_encode (value);
    post_empty (publisher_url, path);
}

} // namespace zlink::framework::e2e::pubsub::client
