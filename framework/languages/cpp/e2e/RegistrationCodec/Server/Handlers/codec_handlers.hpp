/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::registration_codec::server
{

class json_roundtrip_handler_t
{
  public:
    using request_type = json_roundtrip_t;
    using reply_type = json_roundtrip_reply_t;

    json_roundtrip_reply_t handle (const json_roundtrip_t &request,
                                   const zlink::framework::request_context_t &context)
    {
        return {.value = "json:" + request.value, .content_type = context.content_type};
    }
};

class json_codec_send_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using message_type = json_codec_send_t;

    explicit json_codec_send_handler_t (scenario_state_t &state) : _state (state) {}

    void handle (const json_codec_send_t &message,
                 const zlink::framework::send_context_t &context)
    {
        _state.record ("RC-B1-send", context.content_type + ":" + message.value);
    }

  private:
    scenario_state_t &_state;
};

class protobuf_roundtrip_handler_t
{
  public:
    using request_type = protobuf_roundtrip_t;
    using reply_type = protobuf_roundtrip_reply_t;

    protobuf_roundtrip_reply_t handle (const protobuf_roundtrip_t &request,
                                       const zlink::framework::request_context_t &context)
    {
        return {.value = "protobuf:" + request.value, .content_type = context.content_type};
    }
};

class protobuf_codec_send_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using message_type = protobuf_codec_send_t;

    explicit protobuf_codec_send_handler_t (scenario_state_t &state) : _state (state) {}

    void handle (const protobuf_codec_send_t &message,
                 const zlink::framework::send_context_t &context)
    {
        _state.record ("RC-B2-send", context.content_type + ":" + message.value);
    }

  private:
    scenario_state_t &_state;
};

class messagepack_roundtrip_handler_t
{
  public:
    using request_type = messagepack_roundtrip_t;
    using reply_type = messagepack_roundtrip_reply_t;

    messagepack_roundtrip_reply_t handle (const messagepack_roundtrip_t &request,
                                          const zlink::framework::request_context_t &context)
    {
        return {.value = "messagepack:" + request.value, .content_type = context.content_type};
    }
};

class messagepack_codec_send_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using message_type = messagepack_codec_send_t;

    explicit messagepack_codec_send_handler_t (scenario_state_t &state) : _state (state) {}

    void handle (const messagepack_codec_send_t &message,
                 const zlink::framework::send_context_t &context)
    {
        _state.record ("RC-B3-send", context.content_type + ":" + message.value);
    }

  private:
    scenario_state_t &_state;
};

class custom_roundtrip_handler_t
{
  public:
    using request_type = custom_roundtrip_t;
    using reply_type = custom_roundtrip_reply_t;

    custom_roundtrip_reply_t handle (const custom_roundtrip_t &request)
    {
        return {.value = "custom:" + request.value};
    }
};

class mismatch_roundtrip_handler_t
{
  public:
    using request_type = mismatch_roundtrip_t;
    using reply_type = mismatch_roundtrip_reply_t;

    mismatch_roundtrip_reply_t handle (const mismatch_roundtrip_t &request)
    {
        return {.value = "mismatch:" + request.value};
    }
};

} // namespace zlink::framework::e2e::registration_codec::server
