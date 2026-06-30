/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <openssl/sha.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace zlink::framework::e2e::registry_messaging
{

inline constexpr const char *api_channel = "registry.messaging.api";
inline constexpr const char *workflow_channel = "registry.messaging.workflow";
inline constexpr const char *route_channel = "registry.messaging.route";
inline constexpr const char *dealer_channel = "registry.messaging.dealer";
inline constexpr const char *handler_group = "registry-messaging";

struct profile_request_t
{
    std::string value;
};

struct profile_reply_t
{
    std::string value;
    std::string provider_rid;
    std::string instance_id;
};

struct profile_command_t
{
    std::string command_id;
};

struct payload_request_t
{
    std::string marker;
    std::string payload;
};

struct payload_reply_t
{
    std::string marker;
    int length = 0;
    std::string sha256;
};

struct workflow_request_t
{
    std::string value;
};

struct workflow_reply_t
{
    std::string value;
    std::string provider_rid;
};

struct route_ping_t
{
    std::string value;
};

struct route_pong_t
{
    std::string value;
    std::string target_rid;
    std::string source_rid;
};

struct evidence_entry_t
{
    std::string marker;
    std::string provider_rid;
    std::string value;
};

struct evidence_snapshot_t
{
    std::string provider_rid;
    std::vector<evidence_entry_t> entries;
};

inline std::string sha256_hex (const std::string &value)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256 (reinterpret_cast<const unsigned char *> (value.data ()), value.size (), digest);
    std::ostringstream output;
    output << std::hex << std::setfill ('0');
    for (const auto byte : digest) {
        output << std::setw (2) << static_cast<int> (byte);
    }
    return output.str ();
}

inline void to_json (nlohmann::json &json, const profile_request_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, profile_request_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const profile_reply_t &value)
{
    json = nlohmann::json{{"value", value.value},
                          {"provider_rid", value.provider_rid},
                          {"instance_id", value.instance_id}};
}

inline void from_json (const nlohmann::json &json, profile_reply_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("provider_rid").get_to (value.provider_rid);
    if (json.contains ("instance_id")) {
        json.at ("instance_id").get_to (value.instance_id);
    }
}

inline void to_json (nlohmann::json &json, const profile_command_t &value)
{
    json = nlohmann::json{{"command_id", value.command_id}};
}

inline void from_json (const nlohmann::json &json, profile_command_t &value)
{
    json.at ("command_id").get_to (value.command_id);
}

inline void to_json (nlohmann::json &json, const payload_request_t &value)
{
    json = nlohmann::json{{"marker", value.marker}, {"payload", value.payload}};
}

inline void from_json (const nlohmann::json &json, payload_request_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("payload").get_to (value.payload);
}

inline void to_json (nlohmann::json &json, const payload_reply_t &value)
{
    json = nlohmann::json{
      {"marker", value.marker}, {"length", value.length}, {"sha256", value.sha256}};
}

inline void from_json (const nlohmann::json &json, payload_reply_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("length").get_to (value.length);
    json.at ("sha256").get_to (value.sha256);
}

inline void to_json (nlohmann::json &json, const workflow_request_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, workflow_request_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const workflow_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}, {"provider_rid", value.provider_rid}};
}

inline void from_json (const nlohmann::json &json, workflow_reply_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("provider_rid").get_to (value.provider_rid);
}

inline void to_json (nlohmann::json &json, const route_ping_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, route_ping_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const route_pong_t &value)
{
    json = nlohmann::json{
      {"value", value.value}, {"target_rid", value.target_rid}, {"source_rid", value.source_rid}};
}

inline void from_json (const nlohmann::json &json, route_pong_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("target_rid").get_to (value.target_rid);
    json.at ("source_rid").get_to (value.source_rid);
}

inline void to_json (nlohmann::json &json, const evidence_entry_t &value)
{
    json = nlohmann::json{
      {"marker", value.marker}, {"provider_rid", value.provider_rid}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, evidence_entry_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("provider_rid").get_to (value.provider_rid);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const evidence_snapshot_t &value)
{
    json = nlohmann::json{{"provider_rid", value.provider_rid}, {"entries", value.entries}};
}

inline void from_json (const nlohmann::json &json, evidence_snapshot_t &value)
{
    json.at ("provider_rid").get_to (value.provider_rid);
    json.at ("entries").get_to (value.entries);
}

} // namespace zlink::framework::e2e::registry_messaging
