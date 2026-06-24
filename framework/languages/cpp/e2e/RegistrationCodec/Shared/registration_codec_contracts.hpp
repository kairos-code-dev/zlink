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

struct scoped_lifecycle_req_t
{
    static constexpr const char *packet_name = "ScopedLifecycle";
    std::string value;
};

struct scoped_lifecycle_reply_t
{
    int scoped_id = 0;
    int singleton_id = 0;
    int destroyed_before = 0;
};

struct scoped_lifecycle_stats_req_t
{
    static constexpr const char *packet_name = "ScopedLifecycleStats";
    std::string value;
};

struct scoped_lifecycle_stats_reply_t
{
    int destroyed_count = 0;
};

struct filter_order_req_t
{
    static constexpr const char *packet_name = "FilterOrder";
    std::string value;
};

struct filter_order_reply_t
{
    std::string value;
    std::vector<std::string> order;
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

inline void to_json (nlohmann::json &json, const scoped_lifecycle_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, scoped_lifecycle_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const scoped_lifecycle_reply_t &value)
{
    json = nlohmann::json{{"scoped_id", value.scoped_id},
                          {"singleton_id", value.singleton_id},
                          {"destroyed_before", value.destroyed_before}};
}

inline void from_json (const nlohmann::json &json, scoped_lifecycle_reply_t &value)
{
    json.at ("scoped_id").get_to (value.scoped_id);
    json.at ("singleton_id").get_to (value.singleton_id);
    json.at ("destroyed_before").get_to (value.destroyed_before);
}

inline void to_json (nlohmann::json &json, const scoped_lifecycle_stats_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, scoped_lifecycle_stats_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const scoped_lifecycle_stats_reply_t &value)
{
    json = nlohmann::json{{"destroyed_count", value.destroyed_count}};
}

inline void from_json (const nlohmann::json &json, scoped_lifecycle_stats_reply_t &value)
{
    json.at ("destroyed_count").get_to (value.destroyed_count);
}

inline void to_json (nlohmann::json &json, const filter_order_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, filter_order_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const filter_order_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}, {"order", value.order}};
}

inline void from_json (const nlohmann::json &json, filter_order_reply_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("order").get_to (value.order);
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
