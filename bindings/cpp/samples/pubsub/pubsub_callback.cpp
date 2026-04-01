/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

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

    zlink_multipart_close (parts_, part_count_);
    state->cv.notify_one ();
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t publisher (ctx);
    zlink::sub_socket_t subscriber (ctx);
    zlink::monitor_handle_t pub_monitor = publisher.monitor_handle ();
    zlink::monitor_handle_t sub_monitor = subscriber.monitor_handle ();

    const std::string endpoint =
      detail::unique_tcp ("pubsub-callback");
    assert (publisher.bind (endpoint) == 0);
    assert (subscriber.connect (endpoint) == 0);
    assert (detail::wait_for_socket_monitor_event (
      pub_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (detail::wait_for_socket_monitor_event (
      sub_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    const std::string topic = "prices";
    callback_state_t state;
    state.ready = false;
    assert (subscriber.set_subscription (topic) == 0);
    assert (subscriber.subscribe_handler (&subscribe_callback, &state) == 0);

    const zlink::subscription_event_t event =
      publisher.receive_subscription_event ();
    assert (event.subscribed);
    assert (event.topic == topic);

    const std::string sent = "101.25";
    zlink::message_t outbound = detail::make_message (sent);
    publisher.publish (topic, outbound);

    std::unique_lock<std::mutex> lock (state.mutex);
    assert (detail::wait_until (state.cv, lock, state.ready, 2000));
    assert (state.topic == topic);
    assert (state.payload == "101.25");
    std::printf (
      "[pubsub/callback] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
      topic.c_str (), sent.c_str (), state.topic.c_str (), state.payload.c_str ());
    return 0;
}
