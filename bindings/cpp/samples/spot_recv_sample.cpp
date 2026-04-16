/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <condition_variable>
#include <mutex>

namespace {
struct spot_recv_state_t
{
    zlink::service::spot_t *spot = nullptr;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool failed = false;
    std::string service_name;
    std::string topic;
    std::string payload;
};

void on_spot_dispatch_event (void *, zlink_spot_dispatch_event_t event_, void *userdata_)
{
    if (event_ != ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE)
        return;

    spot_recv_state_t &state = *static_cast<spot_recv_state_t *> (userdata_);
    try {
        const zlink::topic_message_t inbound =
          state.spot->subscribe (zlink::recv_flags_t::dontwait);
        std::lock_guard<std::mutex> lock (state.mutex);
        state.service_name = inbound.service_name ().value_or ("");
        state.topic = inbound.topic ();
        state.payload = inbound.parts ().at (0).to_string ();
        state.done = true;
    }
    catch (...) {
        std::lock_guard<std::mutex> lock (state.mutex);
        state.failed = true;
        state.done = true;
    }
    state.cv.notify_one ();
}
} // namespace

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    zlink::pub_socket_t pub_socket (ctx);
    zlink::sub_socket_t sub_socket (ctx);
    assert (node.valid ());
    assert (spot.valid ());
    assert (pub_socket.valid ());
    assert (sub_socket.valid ());

    const std::string service_name = detail::k_spot_service;
    const std::string service_endpoint = detail::unique_tcp ("spot-recv-service");
    assert (pub_socket.bind (service_endpoint) == 0);
    assert (sub_socket.connect (service_endpoint) == 0);
    node.attach_pubsub (service_name, pub_socket, sub_socket);

    const std::string topic = detail::k_spot_topic;
    const std::string sent = detail::k_spot_payload;
    spot_recv_state_t state;
    state.spot = &spot;
    spot.on_dispatch_event (&on_spot_dispatch_event, &state);
    spot.set_subscription (topic);

    zlink::message_t outbound = detail::make_message (sent);
    spot.publish (service_name, topic, outbound);

    std::unique_lock<std::mutex> lock (state.mutex);
    const bool delivered =
      state.cv.wait_for (lock, std::chrono::seconds (5), [&state] { return state.done; });
    assert (delivered);
    assert (!state.failed);
    assert (state.service_name == service_name);
    assert (state.topic == topic);
    assert (state.payload == sent);
    std::printf (
      "[spot/recv] service: \"%s\" tick: 1 publish: \"%s/%s\" -> recv: \"%s/%s\"\n",
      service_name.c_str (), topic.c_str (), sent.c_str (),
      state.topic.c_str (), state.payload.c_str ());
    return 0;
}
