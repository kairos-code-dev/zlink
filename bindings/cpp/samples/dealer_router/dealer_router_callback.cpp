/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

namespace {

struct callback_state_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool ready;
    std::string routing_id;
    std::string payload;
};

void router_callback (const zlink_routing_id_t *source_rid_,
                      zlink_msg_t *parts_,
                      size_t part_count_,
                      void *userdata_)
{
    callback_state_t *state = static_cast<callback_state_t *> (userdata_);
    assert (state != NULL);
    assert (source_rid_ != NULL);
    assert (part_count_ == 1);

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->routing_id = zlink::routing_id_to_string (*source_rid_);
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
    zlink::socket_t router (ctx, zlink::socket_type::router);
    zlink::socket_t dealer (ctx, zlink::socket_type::dealer);
    zlink::monitor_handle_t router_monitor (router, zlink::monitor_event::all);
    zlink::monitor_handle_t dealer_monitor (dealer, zlink::monitor_event::all);

    const std::string endpoint =
      zlink_cpp_sample::unique_tcp ("dealer-router-callback");
    assert (router.bind (endpoint) == 0);
    assert (dealer.connect (endpoint) == 0);
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    callback_state_t state;
    state.ready = false;
    assert (router.recv_handler (&router_callback, &state) == 0);

    zlink::message_t outbound =
      zlink_cpp_sample::make_message ("dealer-router-callback");
    assert (dealer.send (outbound) == 0);

    std::unique_lock<std::mutex> lock (state.mutex);
    assert (zlink_cpp_sample::wait_until (state.cv, lock, state.ready, 2000));
    assert (!state.routing_id.empty ());
    assert (state.payload == "dealer-router-callback");
    return 0;
}
