/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ACTOR_SAMPLE_COMMON_HPP_INCLUDED
#define ZLINK_CPP_ACTOR_SAMPLE_COMMON_HPP_INCLUDED

#include "sample_common.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

struct actor_sample_capture_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool joined = false;
    bool actor_read = false;
    zlink::request_result_t join_result = zlink::request_result_t::internal_error;
    std::string payload;
};

struct actor_sample_dispatch_state_t
{
    zlink::service::spot_t *spot;
    zlink::service::spot_node_t *node;
    zlink::service::actor_t *actor;
    actor_sample_capture_t *capture;
};

inline bool wait_until_flag (actor_sample_capture_t &capture_,
                             bool actor_sample_capture_t::*flag_)
{
    std::unique_lock<std::mutex> lock (capture_.mutex);
    return capture_.cv.wait_for (
      lock, std::chrono::seconds (2),
      [&]() { return capture_.*flag_; });
}

inline zlink::routing_id_t sample_rid (const char *text_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (text_), std::strlen (text_));
}

struct actor_sample_stream_session_t
{
    zlink::stream_socket_t stream;
    std::unique_ptr<detail::raw_tcp_client_t> client;
    zlink::routing_id_t session;

    explicit actor_sample_stream_session_t (zlink::context_t &ctx_)
        : stream (ctx_), client (), session (sample_rid ("placeholder"))
    {
        zlink::monitor_handle_t monitor = stream.monitor_handle ();
        stream.options ().notify (false);
        stream.bind ("tcp://127.0.0.1:0");
        const std::string endpoint = stream.options ().last_endpoint ();
        assert (!endpoint.empty ());
        client.reset (new detail::raw_tcp_client_t (endpoint));
        assert (detail::wait_stream_connected (monitor));
        client->send_all ("session", 7);

        zlink::received_t inbound;
        assert (stream.recv (inbound) == 0);
        assert (inbound.routing_id ().has_value ());
        session = *inbound.routing_id ();
    }
};

inline void actor_sample_dispatch (
  actor_sample_dispatch_state_t &state_,
  const zlink::spot_dispatch_info_t &info_)
{
    if (info_.event == zlink::spot_dispatch_event_t::actor_join_readable) {
        auto request = state_.spot->recv_actor_join (ZLINK_DONTWAIT);
        assert (request.has_value ());
        zlink::message_t reply = zlink::message_t::from_string ("accepted");
        state_.spot->reply_actor_join (*request, true).message (reply).submit ();
        return;
    }

    if (info_.event == zlink::spot_dispatch_event_t::actor_readable) {
        assert (info_.actor.has_value ());
        for (;;) {
            auto part = state_.actor->recv_part (ZLINK_DONTWAIT);
            if (!part.has_value ())
                break;
            std::lock_guard<std::mutex> lock (state_.capture->mutex);
            state_.capture->payload += part->part.to_string ();
            state_.capture->actor_read = true;
            state_.capture->cv.notify_all ();
        }
    }
}

inline void actor_sample_join_reply (
  actor_sample_capture_t &capture_,
  const zlink::actor_join_result_t &result_,
  std::vector<zlink::message_t>)
{
    std::lock_guard<std::mutex> lock (capture_.mutex);
    capture_.join_result = result_.result;
    capture_.joined = true;
    capture_.cv.notify_all ();
}

#endif
