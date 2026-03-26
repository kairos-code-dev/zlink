/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

namespace {

struct callback_state_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool ready;
    std::string payload;
};

void pair_callback (const zlink_routing_id_t *,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    void *userdata_)
{
    callback_state_t *state = static_cast<callback_state_t *> (userdata_);
    assert (state != NULL);
    assert (part_count_ == 1);

    {
        std::lock_guard<std::mutex> lock (state->mutex);
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
    zlink::socket_t server (ctx, zlink::socket_type::pair);
    zlink::socket_t client (ctx, zlink::socket_type::pair);
    zlink::monitor_handle_t server_monitor (server, zlink::monitor_event::all);
    zlink::monitor_handle_t client_monitor (client, zlink::monitor_event::all);

    const std::string endpoint =
      zlink_cpp_sample::unique_tcp ("pair-callback");
    assert (server.bind (endpoint) == 0);
    assert (client.connect (endpoint) == 0);
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      server_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));
    assert (zlink_cpp_sample::wait_for_socket_monitor_event (
      client_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
      2000, 1));

    callback_state_t state;
    state.ready = false;
    assert (server.recv_handler (&pair_callback, &state) == 0);

    zlink::message_t outbound =
      zlink_cpp_sample::make_message ("pair-callback");
    assert (client.send (outbound) == 0);

    std::unique_lock<std::mutex> lock (state.mutex);
    assert (zlink_cpp_sample::wait_until (state.cv, lock, state.ready, 2000));
    assert (state.payload == "pair-callback");
    return 0;
}
