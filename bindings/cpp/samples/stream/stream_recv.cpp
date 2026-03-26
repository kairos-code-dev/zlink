/* SPDX-License-Identifier: MPL-2.0 */

#include "../common/sample_common.hpp"

int main ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return 0;
#else
    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::stream);

    const std::string endpoint = zlink_cpp_sample::unique_tcp ("stream-recv");
    assert (server.bind (endpoint) == 0);

    const int raw_fd = zlink_cpp_sample::connect_raw_tcp (endpoint);
    assert (raw_fd >= 0);

    const char request[] = "stream-recv";
    assert (zlink_cpp_sample::send_raw_tcp (
              raw_fd, request, sizeof (request) - 1)
            == static_cast<int> (sizeof (request) - 1));

    zlink_routing_id_t source_rid;
    zlink::message_t inbound;
    assert (server.recv (source_rid, inbound) == 0);
    assert (inbound.to_string () == "stream-recv");

    zlink::message_t reply = zlink_cpp_sample::make_message ("stream-reply");
    assert (server.send (source_rid, reply) == 0);

    char response[64];
    const int received =
      zlink_cpp_sample::recv_raw_tcp (raw_fd, response, sizeof (response));
    assert (received == static_cast<int> (std::strlen ("stream-reply")));
    assert (std::memcmp (response, "stream-reply", received) == 0);

    zlink_cpp_sample::close_raw_tcp (raw_fd);
    return 0;
#endif
}
