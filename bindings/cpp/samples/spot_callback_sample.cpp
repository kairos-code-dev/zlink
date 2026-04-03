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
    zlink::service::spot_node_t pub_node (ctx);
    zlink::service::spot_node_t sub_node (ctx);
    assert (pub_node.valid ());
    assert (sub_node.valid ());

    zlink::service::spot_t pub_spot (pub_node);
    zlink::service::spot_t sub_spot (sub_node);
    assert (pub_spot.valid ());
    assert (sub_spot.valid ());

    assert (pub_node.bind ("tcp://127.0.0.1:0") == 0);
    const std::string endpoint = pub_node.last_endpoint ();
    assert (!endpoint.empty ());
    assert (sub_node.connect_peer (endpoint) == 0);

    const std::string topic = detail::k_spot_topic;
    std::promise<callback_result_t> result_promise;
    std::future<callback_result_t> result_future = result_promise.get_future ();
    assert (sub_spot.on_subscribe (&subscribe_callback, &result_promise)
            == 0);
    assert (sub_spot.set_subscription (topic) == 0);

    const std::string sent = detail::k_spot_payload;
    zlink::message_t outbound = detail::make_message (sent);
    pub_spot.publish (topic, outbound);

    const callback_result_t result = detail::wait_future (result_future, 10000);
    assert (result.topic == topic);
    assert (result.payload == detail::k_spot_payload);
    std::printf (
      "[spot/callback] publish: \"%s/%s\" → subscribe: \"%s/%s\"\n",
      topic.c_str (), sent.c_str (), result.topic.c_str (), result.payload.c_str ());
    assert (sub_spot.close () == 0);
    assert (pub_spot.close () == 0);
    assert (sub_node.close () == 0);
    assert (pub_node.close () == 0);
    return 0;
}
