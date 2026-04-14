/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/sample_common.hpp"

#include <chrono>
#include <future>
#include <thread>

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
    assert (part_count_ == 1);

    callback_result_t result;
    result.topic.assign (topic_, topic_len_);
    result.payload.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[0])),
      zlink_msg_size (&parts_[0]));

    zlink_multipart_close (parts_, part_count_);
    result_promise->set_value (result);
}

bool wait_for_spot_ready (zlink::service::spot_node_t &node_,
                          bool require_subject_,
                          uint32_t min_active_peer_count_,
                          int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink::spot_node_status_t status;
        try {
            status = node_.status_snapshot ();
        } catch (...) {
            std::this_thread::yield ();
            continue;
        }

        const bool pub_ready = !status.local_endpoint.empty ();
        const bool sub_ready = status.active_peer_count >= min_active_peer_count_
                               && (!require_subject_ || status.subject_count > 0);
        if (require_subject_ ? sub_ready : pub_ready)
            return true;
        std::this_thread::yield ();
    }

    return false;
}

} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t pub_node (ctx);
    zlink::service::spot_node_t sub_node (ctx);
    assert (pub_node.valid ());
    assert (sub_node.valid ());

    zlink::service::spot_t pub_spot = pub_node.create_spot ();
    zlink::service::spot_t sub_spot = sub_node.create_spot ();
    assert (pub_spot.valid ());
    assert (sub_spot.valid ());

    std::promise<callback_result_t> result_promise;
    std::future<callback_result_t> result_future = result_promise.get_future ();
    sub_spot.on_subscribe (&subscribe_callback, &result_promise);
    sub_spot.set_subscription ("topic:alpha");

    pub_node.bind ("tcp://127.0.0.1:0");
    const std::string endpoint = pub_node.last_endpoint ();
    assert (!endpoint.empty ());
    sub_node.connect_peer (endpoint);
    assert (wait_for_spot_ready (pub_node, false, 1u, 10000));
    assert (wait_for_spot_ready (sub_node, true, 1u, 10000));

    zlink::message_t outbound =
      detail::make_message ("spot-callback");
    pub_spot.publish ("topic:alpha", outbound);

    const callback_result_t result = detail::wait_future (result_future, 10000);
    assert (result.topic == "topic:alpha");
    assert (result.payload == "spot-callback");
    sub_spot.close ();
    pub_spot.close ();
    sub_node.close ();
    pub_node.close ();
    return 0;
}
