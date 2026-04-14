/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

namespace {

struct callback_result_t
{
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
    std::promise<callback_result_t> *result_promise =
      static_cast<std::promise<callback_result_t> *> (userdata_);
    assert (result_promise != NULL);
    assert (topic_ != NULL);
    assert (part_count_ == 1);

    callback_result_t result;
    result.topic.assign (topic_, topic_len_);
    result.payload.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[0])),
      zlink_msg_size (&parts_[0]));

    zlink_multipart_close (parts_, part_count_);
    result_promise->set_value (result);
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::xpub_socket_t publisher (ctx);
    zlink::sub_socket_t subscriber (ctx);
    zlink::monitor_handle_t pub_monitor = publisher.monitor_handle ();
    zlink::monitor_handle_t sub_monitor = subscriber.monitor_handle ();

    assert (publisher.bind ("tcp://127.0.0.1:0") == 0);
    const std::string endpoint = publisher.options ().last_endpoint ();
    assert (!endpoint.empty ());
    assert (subscriber.connect (endpoint) == 0);
    assert (detail::wait_connected (pub_monitor, sub_monitor));

    const std::string topic = detail::k_pubsub_topic;
    std::promise<callback_result_t> result_promise;
    std::future<callback_result_t> result_future = result_promise.get_future ();
    subscriber.set_subscription (topic);
    subscriber.on_subscribe (&subscribe_callback, &result_promise);

    const zlink::subscription_event_t event =
      publisher.receive_subscription_event ();
    assert (event.subscribed);
    assert (event.topic == topic);

    const std::string sent = detail::k_pubsub_payload;
    zlink::message_t outbound = detail::make_message (sent);
    publisher.publish (topic, outbound);

    const callback_result_t result = detail::wait_future (result_future, 2000);
    assert (result.topic == topic);
    assert (result.payload == detail::k_pubsub_payload);
    std::printf (
      "[pubsub/callback] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
      topic.c_str (), sent.c_str (), result.topic.c_str (), result.payload.c_str ());
    return 0;
}
