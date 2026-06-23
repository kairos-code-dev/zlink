/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace zlink::framework::e2e::registration_codec
{

inline constexpr const char *api_channel = "registration.codec.api";
inline constexpr const char *route_channel = "registration.codec.route";
inline constexpr const char *handler_group = "registration-codec";

struct echo_auto_t
{
    static constexpr const char *packet_name = "EchoAuto";
    std::string value;
};

struct echo_auto_reply_t
{
    std::string value;
};

struct echo_auto_send_t
{
    static constexpr const char *packet_name = "EchoAutoSend";
    std::string value;
};

struct echo_manual_t
{
    std::string value;
};

struct echo_manual_reply_t
{
    std::string value;
    std::string packet_name;
    std::string content_type;
};

struct json_roundtrip_t
{
    static constexpr const char *packet_name = "JsonRoundtrip";
    std::string value;
};

struct json_roundtrip_reply_t
{
    std::string value;
};

struct custom_roundtrip_t
{
    static constexpr const char *packet_name = "CustomRoundtrip";
    std::string value;
};

struct custom_roundtrip_reply_t
{
    std::string value;
};

struct mismatch_roundtrip_t
{
    static constexpr const char *packet_name = "MismatchRoundtrip";
    std::string value;
};

struct mismatch_roundtrip_reply_t
{
    std::string value;
};

struct evidence_entry_t
{
    std::string marker;
    std::string value;
};

struct evidence_snapshot_t
{
    std::vector<evidence_entry_t> entries;
};

inline void to_json (nlohmann::json &json, const echo_auto_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, echo_auto_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const echo_auto_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, echo_auto_reply_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const echo_auto_send_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, echo_auto_send_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const echo_manual_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, echo_manual_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const echo_manual_reply_t &value)
{
    json = nlohmann::json{{"value", value.value},
                          {"packet_name", value.packet_name},
                          {"content_type", value.content_type}};
}

inline void from_json (const nlohmann::json &json, echo_manual_reply_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("packet_name").get_to (value.packet_name);
    json.at ("content_type").get_to (value.content_type);
}

inline void to_json (nlohmann::json &json, const json_roundtrip_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, json_roundtrip_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const json_roundtrip_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, json_roundtrip_reply_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const custom_roundtrip_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, custom_roundtrip_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const custom_roundtrip_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, custom_roundtrip_reply_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const mismatch_roundtrip_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, mismatch_roundtrip_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const mismatch_roundtrip_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, mismatch_roundtrip_reply_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const evidence_entry_t &value)
{
    json = nlohmann::json{{"marker", value.marker}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, evidence_entry_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const evidence_snapshot_t &value)
{
    json = nlohmann::json{{"entries", value.entries}};
}

inline void from_json (const nlohmann::json &json, evidence_snapshot_t &value)
{
    json.at ("entries").get_to (value.entries);
}

} // namespace zlink::framework::e2e::registration_codec
