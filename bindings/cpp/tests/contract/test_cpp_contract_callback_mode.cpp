/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/common/sample_common.hpp"

namespace {

struct callback_state_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool ready;
    std::string topic;
    std::string payload;
};

void subscribe_callback (const zlink_routing_id_t *,
                         const char *topic_,
                         size_t topic_len_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         void *userdata_)
{
    callback_state_t *state = static_cast<callback_state_t *> (userdata_);
    assert (state != NULL);
    assert (part_count_ == 1);

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->topic.assign (topic_, topic_len_);
        state->payload.assign (
          static_cast<const char *> (zlink_msg_data (&parts_[0])),
          zlink_msg_size (&parts_[0]));
        state->ready = true;
    }
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
    state->cv.notify_one ();
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_t spot (ctx);
    assert (spot.valid ());
    zlink::service_monitor_handle_t sub_monitor (
      spot, zlink::service_monitor_event::spot_filter_applied
              | zlink::service_monitor_event::error);
    assert (sub_monitor.valid ());
    zlink::service_monitor_handle_t pub_monitor (
      spot.handle (), zlink::service_monitor_event::spot_first_delivery_ready_changed
                        | zlink::service_monitor_event::error);
    assert (pub_monitor.valid ());

    callback_state_t state;
    state.ready = false;
    assert (spot.subscribe_handler (&subscribe_callback, &state) == 0);
    assert (spot.subscribe ("topic:alpha") == 0);
    assert (detail::wait_for_service_monitor_event (
      sub_monitor,
      static_cast<uint32_t> (
        zlink::service_monitor_event::spot_filter_applied),
      10000));
    assert (detail::wait_for_service_monitor_state (
      pub_monitor, ZLINK_MONITOR_STATE_SEND_READY, 10000));

    zlink::message_t outbound =
      detail::make_message ("spot-callback");
    spot.publish ("topic:alpha", outbound);

    std::unique_lock<std::mutex> lock (state.mutex);
    assert (detail::wait_until (state.cv, lock, state.ready, 10000));
    assert (state.topic == "topic:alpha");
    assert (state.payload == "spot-callback");
    lock.unlock ();
    assert (pub_monitor.close () == 0);
    assert (sub_monitor.close () == 0);
    assert (spot.destroy () == 0);
    assert (ctx.shutdown () == 0);
    assert (ctx.term () == 0);
    return 0;
}
