/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/errors/result.hpp>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace zlink::framework::runtime::messaging
{

enum class message_kind_t
{
    request = 1,
    response = 2,
    command = 3,
    publish = 4,
    error = 5
};

struct envelope_header_t
{
    message_kind_t kind = message_kind_t::request;
    std::string channel_name;
    std::string message_name;
    std::string content_type = "application/json";
    std::string correlation_id;
    std::optional<std::string> deadline;
    std::optional<std::string> topic;
    std::optional<std::string> error_code;
    std::optional<std::string> error_message;
    std::optional<std::string> source;
    std::map<std::string, std::string> metadata;
};

class message_parts_t
{
  public:
    message_parts_t () = default;
    message_parts_t (zlink::message_t header, zlink::message_t body);
    explicit message_parts_t (std::vector<zlink::message_t> parts);

    std::size_t size () const noexcept { return _parts.size (); }
    const zlink::message_t &operator[] (std::size_t index) const;
    const std::vector<zlink::message_t> &items () const noexcept { return _parts; }

  private:
    std::vector<zlink::message_t> _parts;
};

class envelope_codec_t
{
  public:
    static constexpr const char *default_content_type = "application/json";

    message_parts_t encode_raw_body_parts (const envelope_header_t &header, zlink::message_t body) const;
    message_parts_t encode_parts (const envelope_header_t &header,
                                  std::type_index body_type,
                                  const void *body,
                                  const serializer_registry_t &serializers) const;

    zlink::message_t encode_header (const envelope_header_t &header) const;
    result_t<envelope_header_t> decode_header (const zlink::message_t &message) const;
    result_t<envelope_header_t> decode_header (const message_parts_t &parts) const;
    result_t<zlink::message_t> decode_body (const message_parts_t &parts) const;
};

} // namespace zlink::framework::runtime::messaging
