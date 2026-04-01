/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return 0;
#else
    zlink::context_t ctx;
    zlink::stream_socket_t server (ctx);

    const std::string endpoint = detail::unique_tcp ("stream-recv");
    assert (server.bind (endpoint) == 0);

    const int raw_fd = detail::connect_raw_tcp (endpoint);
    assert (raw_fd >= 0);

    const char request[] = "hello-stream";
    assert (detail::send_raw_tcp (
              raw_fd, request, sizeof (request) - 1)
            == static_cast<int> (sizeof (request) - 1));

    const zlink::received_t inbound = server.receive ();
    assert (inbound.routing_id.size > 0);
    assert (inbound.parts.size () == 1);
    assert (inbound.parts[0].to_string () == "hello-stream");

    zlink::message_t reply = detail::make_message ("hello-stream");
    server.send (inbound.routing_id, reply);

    char response[64];
    const int received =
      detail::recv_raw_tcp (raw_fd, response, sizeof (response));
    assert (received == static_cast<int> (std::strlen ("hello-stream")));
    assert (std::memcmp (response, "hello-stream", received) == 0);
    std::printf ("[stream/recv] send: \"%s\" → recv: \"%.*s\"\n",
                 request, received, response);

    detail::close_raw_tcp (raw_fd);
    return 0;
#endif
}
