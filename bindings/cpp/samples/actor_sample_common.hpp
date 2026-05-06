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

inline void actor_sample_dispatch (void *,
                                   const zlink_spot_dispatch_info_t *info_,
                                   void *userdata_)
{
    actor_sample_dispatch_state_t *state =
      static_cast<actor_sample_dispatch_state_t *> (userdata_);
    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE) {
        auto request = state->spot->recv_actor_join (zlink::recv_flags_t::dontwait);
        assert (request.has_value ());
        zlink::message_t reply = zlink::message_t::from_string ("accepted");
        state->spot->reply_actor_join (request->first, true, reply);
        return;
    }

    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE) {
        for (;;) {
            zlink_actor_recv_info_t recv_info;
            zlink_msg_t native_part;
            zlink_part_flag_t more = ZLINK_PART_FINAL;
            zlink_recv_result_t rc = zlink_spot_node_actor_recv_part (
              state->node->handle (),
              static_cast<const zlink_actor_ref_t *> (info_->subject),
              &recv_info, &native_part, &more, ZLINK_RECV_FLAGS_DONTWAIT);
            if (rc == ZLINK_RECV_NO_DATA)
                break;
            assert (rc == ZLINK_RECV_OK);
            zlink::message_t part;
            assert (zlink_msg_move (part.handle (), &native_part) == 0);
            std::lock_guard<std::mutex> lock (state->capture->mutex);
            state->capture->payload += part.to_string ();
            state->capture->actor_read = true;
            state->capture->cv.notify_all ();
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
