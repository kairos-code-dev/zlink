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
    zlink::stream_socket_t server (ctx);

    const std::string endpoint =
      detail::unique_tcp ("stream-callback");
    assert (server.bind (endpoint) == 0);

    callback_state_t state;
    state.ready = false;
    std::memset (&state.routing_id, 0, sizeof (state.routing_id));
    assert (server.recv_handler (&stream_callback, &state) == 0);

    const int raw_fd = detail::connect_raw_tcp (endpoint);
    assert (raw_fd >= 0);

    const char request[] = "hello-stream";
    assert (detail::send_raw_tcp (
              raw_fd, request, sizeof (request) - 1)
            == static_cast<int> (sizeof (request) - 1));

    std::unique_lock<std::mutex> lock (state.mutex);
    assert (detail::wait_until (state.cv, lock, state.ready, 2000));
    assert (state.payload == "hello-stream");
    lock.unlock ();

    zlink::message_t reply =
      detail::make_message ("hello-stream");
    server.send (state.routing_id, reply);

    char response[64];
    const int received =
      detail::recv_raw_tcp (raw_fd, response, sizeof (response));
    assert (received
            == static_cast<int> (std::strlen ("hello-stream")));
    assert (std::memcmp (response, "hello-stream", received) == 0);
    std::printf ("[stream/callback] send: \"%s\" → recv: \"%.*s\"\n",
                 request, received, response);

    detail::close_raw_tcp (raw_fd);
    return 0;
#endif
}
