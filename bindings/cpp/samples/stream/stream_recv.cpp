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

    const char request[] = "stream-recv";
    assert (detail::send_raw_tcp (
              raw_fd, request, sizeof (request) - 1)
            == static_cast<int> (sizeof (request) - 1));

    zlink_routing_id_t source_rid;
    zlink::message_t inbound;
    assert (server.recv (source_rid, inbound) == 0);
    assert (inbound.to_string () == "stream-recv");

    zlink::message_t reply = detail::make_message ("stream-reply");
    assert (server.send (source_rid, reply) == 0);

    char response[64];
    const int received =
      detail::recv_raw_tcp (raw_fd, response, sizeof (response));
    assert (received == static_cast<int> (std::strlen ("stream-reply")));
    assert (std::memcmp (response, "stream-reply", received) == 0);

    detail::close_raw_tcp (raw_fd);
    return 0;
#endif
}
