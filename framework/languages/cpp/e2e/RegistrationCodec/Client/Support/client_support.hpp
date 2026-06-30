/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/registration_codec_contracts.hpp"

#include <zlink/codecs/messagepack.hpp>
#include <zlink/codecs/protobuf.hpp>
#include <zlink/framework.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::registration_codec::client
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

inline zlink::framework::encoded_payload_t encode_text (const std::string &prefix,
                                                        const std::string &value)
{
    return zlink::framework::encoded_payload_t::from_string (prefix + ":" + value);
}

inline std::string decode_text (const zlink::framework::encoded_payload_t &payload,
                                const std::string &prefix)
{
    const auto text = payload.to_string ();
    const auto expected = prefix + ":";
    if (text.rfind (expected, 0) != 0) {
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::payload_decode_failed,
          "custom payload prefix mismatch");
    }
    return text.substr (expected.size ());
}

inline void add_json_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<echo_auto_t,
                    echo_auto_reply_t,
                    echo_auto_send_t,
                    echo_manual_t,
                    echo_manual_reply_t,
                    json_roundtrip_t,
                    json_roundtrip_reply_t,
                    json_codec_send_t,
                    scoped_lifecycle_req_t,
                    scoped_lifecycle_reply_t,
                    scoped_lifecycle_stats_req_t,
                    scoped_lifecycle_stats_reply_t,
                    filter_order_req_t,
                    filter_order_reply_t> ();
}

inline void add_binary_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.use (zlink::framework_codecs::protobuf ());
    zlink::framework_codecs::protobuf_codec_extension_t::register_payload_serializer<
      protobuf_roundtrip_t> (codecs);
    zlink::framework_codecs::protobuf_codec_extension_t::register_payload_serializer<
      protobuf_roundtrip_reply_t> (codecs);
    zlink::framework_codecs::protobuf_codec_extension_t::register_payload_serializer<
      protobuf_codec_send_t> (codecs);
    codecs.use (zlink::framework_codecs::messagepack ());
    zlink::framework_codecs::messagepack_codec_extension_t::register_payload_serializer<
      messagepack_roundtrip_t> (codecs);
    zlink::framework_codecs::messagepack_codec_extension_t::register_payload_serializer<
      messagepack_roundtrip_reply_t> (codecs);
    zlink::framework_codecs::messagepack_codec_extension_t::register_payload_serializer<
      messagepack_codec_send_t> (codecs);
}

inline void add_custom_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs
      .add_serializer<custom_roundtrip_t> (
        [] (const custom_roundtrip_t &value) {
            return encode_text ("custom-request", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return custom_roundtrip_t{.value = decode_text (payload, "custom-request")};
        })
      .add_serializer<custom_roundtrip_reply_t> (
        [] (const custom_roundtrip_reply_t &value) {
            return encode_text ("custom-reply", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return custom_roundtrip_reply_t{.value = decode_text (payload, "custom-reply")};
        })
      .add_serializer<mismatch_roundtrip_t> (
        [] (const mismatch_roundtrip_t &value) {
            return encode_text ("mismatch-request", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return mismatch_roundtrip_t{.value = decode_text (payload, "mismatch-request")};
        })
      .add_serializer<mismatch_roundtrip_reply_t> (
        [] (const mismatch_roundtrip_reply_t &value) {
            return encode_text ("client-wrong-reply", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return mismatch_roundtrip_reply_t{
              .value = decode_text (payload, "client-wrong-reply")};
        });
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
        throw std::runtime_error ("HTTP endpoint must start with http://");
    }
    const auto host_start = std::string (prefix).size ();
    const auto port_separator = endpoint.find (':', host_start);
    if (port_separator == std::string::npos) {
        throw std::runtime_error ("HTTP endpoint must include a port");
    }
    auto path_separator = endpoint.find ('/', port_separator + 1);
    if (path_separator == std::string::npos) {
        path_separator = endpoint.size ();
    }
    return {.host = endpoint.substr (host_start, port_separator - host_start),
            .port = endpoint.substr (port_separator + 1, path_separator - port_separator - 1)};
}

inline std::string http_get (const std::string &base_url, const std::string &path)
{
    const auto endpoint = parse_http_endpoint (base_url);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *resolved = nullptr;
    if (getaddrinfo (endpoint.host.c_str (), endpoint.port.c_str (), &hints, &resolved) != 0) {
        throw std::runtime_error ("failed to resolve HTTP endpoint");
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
        throw std::runtime_error ("failed to connect to HTTP endpoint");
    }
    const auto request = "GET " + path + " HTTP/1.1\r\nHost: " + endpoint.host + ":"
      + endpoint.port + "\r\nConnection: close\r\n\r\n";
    const char *cursor = request.data ();
    auto remaining = request.size ();
    while (remaining > 0) {
        const auto sent = send (socket_fd, cursor, remaining, 0);
        if (sent <= 0) {
            close (socket_fd);
            throw std::runtime_error ("failed to send HTTP request");
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
            throw std::runtime_error ("failed to read HTTP response");
        }
        if (received == 0) {
            break;
        }
        response.append (buffer, static_cast<std::size_t> (received));
    }
    close (socket_fd);
    if (response.rfind ("HTTP/1.1 200", 0) != 0 && response.rfind ("HTTP/1.0 200", 0) != 0) {
        throw std::runtime_error ("HTTP request failed");
    }
    const auto body_start = response.find ("\r\n\r\n");
    return body_start == std::string::npos ? "" : response.substr (body_start + 4);
}

inline bool evidence_contains (const std::string &http_endpoint,
                               const std::string &marker,
                               const std::string &value)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto body = http_get (http_endpoint, "/evidence");
        const auto snapshot = nlohmann::json::parse (body).get<evidence_snapshot_t> ();
        if (std::any_of (snapshot.entries.begin (), snapshot.entries.end (),
                         [&] (const evidence_entry_t &entry) {
                             return entry.marker == marker && entry.value == value;
                         })) {
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    return false;
}

} // namespace zlink::framework::e2e::registration_codec::client
