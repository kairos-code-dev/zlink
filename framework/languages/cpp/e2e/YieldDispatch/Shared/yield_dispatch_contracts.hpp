/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <zlink/framework/codecs/json.hpp>

#include <nlohmann/json.hpp>

namespace zlink::stream_connector
{
enum class codec_t : std::uint8_t;
}

namespace zlink::framework::e2e::yield_dispatch
{

inline constexpr const char *control_channel = "yield.control";
inline constexpr const char *delay_channel = "yield.delay";
inline constexpr const char *spot_channel = "yield.spot";
inline constexpr const char *spot_route_channel = "yield.spot.route";
inline constexpr const char *stream_node = "yield.stream";
inline constexpr const char *actor_type = "yield.actor";
inline constexpr const char *handler_group = "yield-dispatch";
inline constexpr const char *actor_id_metadata = "actor-id";
inline constexpr const char *spot_rid_metadata = "spot-rid";
inline constexpr const char *target_node_rid_metadata = "target-node-rid";
inline constexpr const char *probe_spot_name = "YieldProbeSpot";

struct ensure_spot_req_t
{
    static constexpr const char *packet_name = "EnsureSpotReq";
    std::string spot_rid;
};

struct ensure_spot_reply_t
{
    static constexpr const char *packet_name = "EnsureSpotReply";
    std::string spot_rid;
    std::string node_rid;
};

struct yield_evidence_req_t
{
    static constexpr const char *packet_name = "YieldEvidenceReq";
    std::string request_id;
};

struct yield_evidence_reply_t
{
    static constexpr const char *packet_name = "YieldEvidenceReply";
    std::string request_id;
    std::vector<std::string> evidence;
};

struct yield_evidence_wait_req_t
{
    static constexpr const char *packet_name = "YieldEvidenceWaitReq";
    std::string request_id;
    std::string marker;
    int timeout_milliseconds = 20000;
};

struct delay_req_t
{
    static constexpr const char *packet_name = "DelayReq";
    std::string request_id;
    int delay_ms = 0;
    std::string marker;
};

struct delay_reply_t
{
    static constexpr const char *packet_name = "DelayReply";
    std::string request_id;
    std::string marker;
    std::string node_rid;
};

struct yield_actor_binding_t
{
    std::string actor_id;
    std::string node_rid;
    std::uint64_t generation = 0;
};

struct bind_yield_actors_req_t
{
    static constexpr const char *packet_name = "BindYieldActorsReq";
    std::string spot_rid;
    std::vector<std::string> actor_ids;
};

struct bind_yield_actors_reply_t
{
    static constexpr const char *packet_name = "BindYieldActorsReply";
    std::string spot_rid;
    std::vector<yield_actor_binding_t> actors;
};

struct hold_req_t
{
    static constexpr const char *packet_name = "HoldReq";
    std::string request_id;
    int delay_ms = 0;
};

struct hold_command_t
{
    static constexpr const char *packet_name = "HoldCommand";
    std::string request_id;
    int delay_ms = 0;
};

struct yield_req_t
{
    static constexpr const char *packet_name = "YieldReq";
    std::string request_id;
    int delay_ms = 0;
    std::string correlation_id;
};

struct yield_command_t
{
    static constexpr const char *packet_name = "YieldCommand";
    std::string request_id;
    int delay_ms = 0;
    std::string correlation_id;
};

struct worker_yield_req_t
{
    static constexpr const char *packet_name = "WorkerYieldReq";
    std::string request_id;
    int delay_ms = 0;
};

struct worker_yield_command_t
{
    static constexpr const char *packet_name = "WorkerYieldCommand";
    std::string request_id;
    int delay_ms = 0;
};

struct yield_timeout_req_t
{
    static constexpr const char *packet_name = "YieldTimeoutReq";
    std::string request_id;
    int delay_ms = 0;
    int timeout_ms = 0;
};

struct yield_timeout_command_t
{
    static constexpr const char *packet_name = "YieldTimeoutCommand";
    std::string request_id;
    int delay_ms = 0;
    int timeout_ms = 0;
};

struct remote_spot_yield_req_t
{
    static constexpr const char *packet_name = "RemoteSpotYieldReq";
    std::string request_id;
    std::string target_spot_rid;
    int delay_ms = 0;
};

struct timer_start_command_t
{
    static constexpr const char *packet_name = "TimerStartCommand";
    std::string request_id;
    std::string timer_name;
    std::string mode;
    int period_ms = 0;
    int delay_ms = 0;
};

struct timer_stop_command_t
{
    static constexpr const char *packet_name = "TimerStopCommand";
    std::string request_id;
};

struct probe_req_t
{
    static constexpr const char *packet_name = "ProbeReq";
    std::string request_id;
    std::string marker;
};

struct probe_command_t
{
    static constexpr const char *packet_name = "ProbeCommand";
    std::string request_id;
    std::string marker;
};

struct actor_yield_req_t
{
    static constexpr const char *packet_name = "ActorYieldReq";
    std::string request_id;
    int delay_ms = 0;
};

struct actor_fast_req_t
{
    static constexpr const char *packet_name = "ActorFastReq";
    std::string request_id;
    std::string marker;
};

struct actor_join_yield_req_t
{
    static constexpr const char *packet_name = "ActorJoinYieldReq";
    std::string request_id;
    std::string target_node_rid;
};

struct actor_push_yield_req_t
{
    static constexpr const char *packet_name = "ActorPushYieldReq";
    std::string request_id;
    int delay_ms = 0;
    std::string value;
};

struct actor_push_notify_t
{
    static constexpr const char *packet_name = "ActorPushNotify";
    std::string actor_id;
    std::string request_id;
    std::string value;
    std::string node_rid;
};

struct actor_yield_reply_t
{
    static constexpr const char *packet_name = "ActorYieldReply";
    std::string scenario_id;
    std::string request_id;
    std::string actor_id;
    std::string spot_rid;
    std::string node_rid;
    std::string marker;
};

struct yield_dispatch_reply_t
{
    static constexpr const char *packet_name = "YieldDispatchReply";
    std::string scenario_id;
    std::string request_id;
    std::string spot_rid;
    std::string node_rid;
    std::string marker;
};

struct yield_timeout_reply_t
{
    static constexpr const char *packet_name = "YieldTimeoutReply";
    std::string scenario_id;
    std::string request_id;
    std::string spot_rid;
    std::string node_rid;
    bool timed_out = false;
    std::string error;
};

} // namespace zlink::framework::e2e::yield_dispatch

namespace zlink::framework::e2e::yield_dispatch
{

inline void to_json (nlohmann::json &json, const ensure_spot_req_t &value)
{
    json = nlohmann::json{{"spot_rid", value.spot_rid}};
}

inline void from_json (const nlohmann::json &json, ensure_spot_req_t &value)
{
    value.spot_rid = json.value ("spot_rid", "");
}

inline void to_json (nlohmann::json &json, const ensure_spot_reply_t &value)
{
    json = nlohmann::json{{"spot_rid", value.spot_rid}, {"node_rid", value.node_rid}};
}

inline void from_json (const nlohmann::json &json, ensure_spot_reply_t &value)
{
    value.spot_rid = json.value ("spot_rid", "");
    value.node_rid = json.value ("node_rid", "");
}

inline void to_json (nlohmann::json &json, const yield_evidence_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}};
}

inline void from_json (const nlohmann::json &json, yield_evidence_req_t &value)
{
    value.request_id = json.value ("request_id", "");
}

inline void to_json (nlohmann::json &json, const yield_evidence_reply_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"evidence", value.evidence}};
}

inline void from_json (const nlohmann::json &json, yield_evidence_reply_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.evidence = json.value ("evidence", std::vector<std::string>{});
}

inline void to_json (nlohmann::json &json, const yield_evidence_wait_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"marker", value.marker},
                          {"timeout_milliseconds", value.timeout_milliseconds}};
}

inline void from_json (const nlohmann::json &json, yield_evidence_wait_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.marker = json.value ("marker", "");
    value.timeout_milliseconds = json.value ("timeout_milliseconds", 20000);
}

inline void to_json (nlohmann::json &json, const delay_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"delay_ms", value.delay_ms},
                          {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, delay_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
    value.marker = json.value ("marker", "");
}

inline void to_json (nlohmann::json &json, const delay_reply_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"marker", value.marker},
                          {"node_rid", value.node_rid}};
}

inline void from_json (const nlohmann::json &json, delay_reply_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.marker = json.value ("marker", "");
    value.node_rid = json.value ("node_rid", "");
}

inline void to_json (nlohmann::json &json, const yield_actor_binding_t &value)
{
    json = nlohmann::json{{"actor_id", value.actor_id},
                          {"node_rid", value.node_rid},
                          {"generation", value.generation}};
}

inline void from_json (const nlohmann::json &json, yield_actor_binding_t &value)
{
    value.actor_id = json.value ("actor_id", "");
    value.node_rid = json.value ("node_rid", "");
    value.generation = json.value ("generation", std::uint64_t{0});
}

inline void to_json (nlohmann::json &json, const bind_yield_actors_req_t &value)
{
    json = nlohmann::json{{"spot_rid", value.spot_rid}, {"actor_ids", value.actor_ids}};
}

inline void from_json (const nlohmann::json &json, bind_yield_actors_req_t &value)
{
    value.spot_rid = json.value ("spot_rid", "");
    value.actor_ids = json.value ("actor_ids", std::vector<std::string>{});
}

inline void to_json (nlohmann::json &json, const bind_yield_actors_reply_t &value)
{
    json = nlohmann::json{{"spot_rid", value.spot_rid}, {"actors", value.actors}};
}

inline void from_json (const nlohmann::json &json, bind_yield_actors_reply_t &value)
{
    value.spot_rid = json.value ("spot_rid", "");
    value.actors = json.value ("actors", std::vector<yield_actor_binding_t>{});
}

inline void to_json (nlohmann::json &json, const hold_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, hold_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
}

inline void to_json (nlohmann::json &json, const hold_command_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, hold_command_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
}

inline void to_json (nlohmann::json &json, const yield_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"delay_ms", value.delay_ms},
                          {"correlation_id", value.correlation_id}};
}

inline void from_json (const nlohmann::json &json, yield_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
    value.correlation_id = json.value ("correlation_id", "");
}

inline void to_json (nlohmann::json &json, const yield_command_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"delay_ms", value.delay_ms},
                          {"correlation_id", value.correlation_id}};
}

inline void from_json (const nlohmann::json &json, yield_command_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
    value.correlation_id = json.value ("correlation_id", "");
}

inline void to_json (nlohmann::json &json, const worker_yield_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, worker_yield_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
}

inline void to_json (nlohmann::json &json, const worker_yield_command_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, worker_yield_command_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
}

inline void to_json (nlohmann::json &json, const yield_timeout_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"delay_ms", value.delay_ms},
                          {"timeout_ms", value.timeout_ms}};
}

inline void from_json (const nlohmann::json &json, yield_timeout_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
    value.timeout_ms = json.value ("timeout_ms", 0);
}

inline void to_json (nlohmann::json &json, const yield_timeout_command_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"delay_ms", value.delay_ms},
                          {"timeout_ms", value.timeout_ms}};
}

inline void from_json (const nlohmann::json &json, yield_timeout_command_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
    value.timeout_ms = json.value ("timeout_ms", 0);
}

inline void to_json (nlohmann::json &json, const remote_spot_yield_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"target_spot_rid", value.target_spot_rid},
                          {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, remote_spot_yield_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.target_spot_rid = json.value ("target_spot_rid", "");
    value.delay_ms = json.value ("delay_ms", 0);
}

inline void to_json (nlohmann::json &json, const timer_start_command_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"timer_name", value.timer_name},
                          {"mode", value.mode},
                          {"period_ms", value.period_ms},
                          {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, timer_start_command_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.timer_name = json.value ("timer_name", "");
    value.mode = json.value ("mode", "");
    value.period_ms = json.value ("period_ms", 0);
    value.delay_ms = json.value ("delay_ms", 0);
}

inline void to_json (nlohmann::json &json, const timer_stop_command_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}};
}

inline void from_json (const nlohmann::json &json, timer_stop_command_t &value)
{
    value.request_id = json.value ("request_id", "");
}

inline void to_json (nlohmann::json &json, const probe_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, probe_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.marker = json.value ("marker", "");
}

inline void to_json (nlohmann::json &json, const probe_command_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, probe_command_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.marker = json.value ("marker", "");
}

inline void to_json (nlohmann::json &json, const actor_yield_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, actor_yield_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
}

inline void to_json (nlohmann::json &json, const actor_fast_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id}, {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, actor_fast_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.marker = json.value ("marker", "");
}

inline void to_json (nlohmann::json &json, const actor_join_yield_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"target_node_rid", value.target_node_rid}};
}

inline void from_json (const nlohmann::json &json, actor_join_yield_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.target_node_rid = json.value ("target_node_rid", "");
}

inline void to_json (nlohmann::json &json, const actor_push_yield_req_t &value)
{
    json = nlohmann::json{{"request_id", value.request_id},
                          {"delay_ms", value.delay_ms},
                          {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, actor_push_yield_req_t &value)
{
    value.request_id = json.value ("request_id", "");
    value.delay_ms = json.value ("delay_ms", 0);
    value.value = json.value ("value", "");
}

inline zlink::message_t to_stream_payload (const actor_push_yield_req_t &value)
{
    return zlink::message_t::from_json (value);
}

inline void from_stream_payload (const zlink::message_t &payload, actor_push_yield_req_t &value)
{
    value = payload.parse_json<actor_push_yield_req_t> ();
}

inline void from_stream_payload (zlink::stream_connector::codec_t,
                                 const zlink::message_t &payload,
                                 actor_push_yield_req_t &value)
{
    from_stream_payload (payload, value);
}

inline void to_json (nlohmann::json &json, const actor_push_notify_t &value)
{
    json = nlohmann::json{{"actor_id", value.actor_id},
                          {"request_id", value.request_id},
                          {"value", value.value},
                          {"node_rid", value.node_rid}};
}

inline void from_json (const nlohmann::json &json, actor_push_notify_t &value)
{
    value.actor_id = json.value ("actor_id", "");
    value.request_id = json.value ("request_id", "");
    value.value = json.value ("value", "");
    value.node_rid = json.value ("node_rid", "");
}

inline zlink::message_t to_stream_payload (const actor_push_notify_t &value)
{
    return zlink::message_t::from_json (value);
}

inline void from_stream_payload (const zlink::message_t &payload, actor_push_notify_t &value)
{
    value = payload.parse_json<actor_push_notify_t> ();
}

inline void from_stream_payload (zlink::stream_connector::codec_t,
                                 const zlink::message_t &payload,
                                 actor_push_notify_t &value)
{
    from_stream_payload (payload, value);
}

inline void to_json (nlohmann::json &json, const actor_yield_reply_t &value)
{
    json = nlohmann::json{{"scenario_id", value.scenario_id},
                          {"request_id", value.request_id},
                          {"actor_id", value.actor_id},
                          {"spot_rid", value.spot_rid},
                          {"node_rid", value.node_rid},
                          {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, actor_yield_reply_t &value)
{
    value.scenario_id = json.value ("scenario_id", "");
    value.request_id = json.value ("request_id", "");
    value.actor_id = json.value ("actor_id", "");
    value.spot_rid = json.value ("spot_rid", "");
    value.node_rid = json.value ("node_rid", "");
    value.marker = json.value ("marker", "");
}

inline void to_json (nlohmann::json &json, const yield_dispatch_reply_t &value)
{
    json = nlohmann::json{{"scenario_id", value.scenario_id},
                          {"request_id", value.request_id},
                          {"spot_rid", value.spot_rid},
                          {"node_rid", value.node_rid},
                          {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, yield_dispatch_reply_t &value)
{
    value.scenario_id = json.value ("scenario_id", "");
    value.request_id = json.value ("request_id", "");
    value.spot_rid = json.value ("spot_rid", "");
    value.node_rid = json.value ("node_rid", "");
    value.marker = json.value ("marker", "");
}

inline void to_json (nlohmann::json &json, const yield_timeout_reply_t &value)
{
    json = nlohmann::json{{"scenario_id", value.scenario_id},
                          {"request_id", value.request_id},
                          {"spot_rid", value.spot_rid},
                          {"node_rid", value.node_rid},
                          {"timed_out", value.timed_out},
                          {"error", value.error}};
}

inline void from_json (const nlohmann::json &json, yield_timeout_reply_t &value)
{
    value.scenario_id = json.value ("scenario_id", "");
    value.request_id = json.value ("request_id", "");
    value.spot_rid = json.value ("spot_rid", "");
    value.node_rid = json.value ("node_rid", "");
    value.timed_out = json.value ("timed_out", false);
    value.error = json.value ("error", "");
}

template <typename T> inline zlink::message_t to_stream_payload (const T &value)
{
    return zlink::message_t::from_json (value);
}

template <typename T>
inline void from_stream_payload (const zlink::message_t &payload, T &value)
{
    value = payload.parse_json<T> ();
}

} // namespace zlink::framework::e2e::yield_dispatch
