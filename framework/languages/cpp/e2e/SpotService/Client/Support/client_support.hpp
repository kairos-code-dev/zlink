/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::spot_service::client
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

inline zlink::framework::actor_ref_t to_actor_ref (const actor_ref_dto_t &actor)
{
    return zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string (actor.node_rid), actor.actor_type, actor.actor_id,
      actor.generation);
}

template <typename T> zlink::message_t encode_json (const T &value)
{
    return zlink::message_t::from (nlohmann::json (value).dump ());
}

template <typename TResult> std::string stream_error_text (const TResult &result)
{
    if (result.error ()) {
        return result.error ()->message;
    }
    return "unknown stream error";
}

class client_channel_state_t
{
  public:
    void record (std::string marker, std::string value)
    {
        std::lock_guard lock (_mutex);
        entries.push_back ({std::move (marker), std::move (value)});
    }

    bool has (const std::string &marker, const std::string &value) const
    {
        std::lock_guard lock (_mutex);
        for (const auto &entry : entries) {
            if (entry.first == marker && entry.second == value) {
                return true;
            }
        }
        return false;
    }

  private:
    mutable std::mutex _mutex;
    std::vector<std::pair<std::string, std::string>> entries;
};

class channel_echo_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<client_channel_state_t>;
    using request_type = channel_echo_req_t;
    using reply_type = channel_echo_res_t;

    explicit channel_echo_handler_t (client_channel_state_t &state) : _state (state) {}

    channel_echo_res_t handle (const channel_echo_req_t &request)
    {
        _state.record ("ChannelEcho", request.value);
        return {.value = "channel:" + request.value, .handled_by = "client-api"};
    }

  private:
    client_channel_state_t &_state;
};

class channel_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<client_channel_state_t>;
    using message_type = channel_msg_t;

    explicit channel_command_handler_t (client_channel_state_t &state) : _state (state) {}

    void handle (const channel_msg_t &command)
    {
        _state.record ("ChannelMsg", command.command_id);
    }

  private:
    client_channel_state_t &_state;
};

} // namespace zlink::framework::e2e::spot_service::client
