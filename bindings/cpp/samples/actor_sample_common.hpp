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
    zlink::actor_ref_t joined_actor;
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
        state_.spot->reply_actor_join (*request, 0).message (reply).submit ();
        return;
    }

    if (info_.event == zlink::spot_dispatch_event_t::actor_readable) {
        assert (info_.actor.has_value ());
        for (;;) {
            zlink_actor_recv_info_t native_info;
            std::memset (&native_info, 0, sizeof (native_info));
            zlink_msg_t native_part;
            std::memset (&native_part, 0, sizeof (native_part));
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            const zlink_recv_result_t rc = zlink_spot_node_actor_recv_part (
              zlink::detail::native_handle (*state_.node),
              zlink::detail::actor_ref_native (*info_.actor), &native_info,
              &native_part, &has_more, ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc == ZLINK_RECV_NO_DATA)
                break;
            assert (rc == ZLINK_RECV_OK);
            zlink::message_t part;
            assert (zlink_msg_move (zlink::detail::native_handle (part),
                                    &native_part)
                    == 0);
            std::lock_guard<std::mutex> lock (state_.capture->mutex);
            state_.capture->payload += part.to_string ();
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
    if (result_.result == zlink::request_result_t::ok)
        capture_.joined_actor = result_.actor;
    capture_.joined = true;
    capture_.cv.notify_all ();
}

#endif
