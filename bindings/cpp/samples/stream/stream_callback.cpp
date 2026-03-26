/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

namespace {

struct callback_state_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool ready;
    zlink_routing_id_t routing_id;
    std::string payload;
};

void stream_callback (const zlink_routing_id_t *source_rid_,
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
        state->routing_id = *source_rid_;
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
#if defined(ZLINK_HAVE_WINDOWS)
    return 0;
#else
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::stream);

    const std::string endpoint =
      zlink_cpp_sample::unique_tcp ("stream-callback");
    assert (server.bind (endpoint) == 0);

    callback_state_t state;
    state.ready = false;
    std::memset (&state.routing_id, 0, sizeof (state.routing_id));
    assert (server.recv_handler (&stream_callback, &state) == 0);

    const int raw_fd = zlink_cpp_sample::connect_raw_tcp (endpoint);
    assert (raw_fd >= 0);

    const char request[] = "stream-callback";
    assert (zlink_cpp_sample::send_raw_tcp (
              raw_fd, request, sizeof (request) - 1)
            == static_cast<int> (sizeof (request) - 1));

    std::unique_lock<std::mutex> lock (state.mutex);
    assert (zlink_cpp_sample::wait_until (state.cv, lock, state.ready, 2000));
    assert (state.payload == "stream-callback");
    lock.unlock ();

    zlink::message_t reply =
      zlink_cpp_sample::make_message ("stream-callback-reply");
    assert (server.send (state.routing_id, reply) == 0);

    char response[64];
    const int received =
      zlink_cpp_sample::recv_raw_tcp (raw_fd, response, sizeof (response));
    assert (received
            == static_cast<int> (std::strlen ("stream-callback-reply")));
    assert (std::memcmp (response, "stream-callback-reply", received) == 0);

    zlink_cpp_sample::close_raw_tcp (raw_fd);
    return 0;
#endif
}
