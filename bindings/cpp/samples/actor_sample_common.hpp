/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ACTOR_SAMPLE_COMMON_HPP_INCLUDED
#define ZLINK_CPP_ACTOR_SAMPLE_COMMON_HPP_INCLUDED

#include <zlink.hpp>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
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

inline void actor_sample_dispatch (
  actor_sample_dispatch_state_t &state_,
  const zlink::spot_dispatch_info_t &info_)
{
    if (info_.event == zlink::spot_dispatch_event_t::actor_join_readable) {
        auto request = state_.spot->recv_actor_join (ZLINK_DONTWAIT);
        assert (request.has_value ());
        zlink::message_t reply = zlink::message_t::from_string ("accepted");
        state_.spot->reply_actor_join (*request, true, reply);
        return;
    }

    if (info_.event == zlink::spot_dispatch_event_t::actor_readable) {
        assert (info_.actor.has_value ());
        for (;;) {
            std::optional<zlink::actor_part_t> part =
              state_.actor->recv_part (ZLINK_DONTWAIT);
            if (!part)
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
  zlink::request_result_t result_,
  std::vector<zlink::message_t>)
{
    std::lock_guard<std::mutex> lock (capture_.mutex);
    capture_.join_result = result_;
    capture_.joined = true;
    capture_.cv.notify_all ();
}

#endif
