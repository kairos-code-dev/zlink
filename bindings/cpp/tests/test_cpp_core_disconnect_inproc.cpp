#include "test_helpers.hpp"

#include <cstring>

namespace {

void test_disconnect_inproc ()
{
    int publications_received = 0;
    bool is_subscribed = false;

    zlink::context_t ctx;
    zlink::socket_t pub_socket (ctx, zlink::socket_type::xpub);
    zlink::socket_t sub_socket (ctx, zlink::socket_type::sub);

    assert (sub_socket.set (zlink::socket_option::subscribe, "foo", 3) == 0);
    assert (pub_socket.bind ("inproc://someInProcDescriptor") == 0);

    int more = 0;
    size_t more_size = sizeof (more);

    for (int iteration = 0;; ++iteration) {
        zlink_pollitem_t items[] = {
          {sub_socket.handle (), 0, ZLINK_POLLIN, 0},
          {pub_socket.handle (), 0, ZLINK_POLLIN, 0},
        };
        const int rc = zlink_poll (items, 2, 100);

        if (items[1].revents & ZLINK_POLLIN) {
            for (more = 1; more;) {
                zlink_msg_t msg;
                assert (zlink_msg_init (&msg) == 0);
                assert (zlink_msg_recv (&msg, pub_socket.handle (), 0) >= 0);
                const char *buffer =
                  static_cast<const char *> (zlink_msg_data (&msg));
                assert (buffer != NULL);

                if (buffer[0] == 0) {
                    assert (is_subscribed);
                    is_subscribed = false;
                } else {
                    assert (!is_subscribed);
                    is_subscribed = true;
                }

                assert (zlink_getsockopt (
                          pub_socket.handle (), ZLINK_RCVMORE, &more, &more_size)
                        == 0);
                assert (zlink_msg_close (&msg) == 0);
            }
        }

        if (items[0].revents & ZLINK_POLLIN) {
            for (more = 1; more;) {
                zlink_msg_t msg;
                assert (zlink_msg_init (&msg) == 0);
                assert (zlink_msg_recv (&msg, sub_socket.handle (), 0) >= 0);
                assert (zlink_getsockopt (
                          sub_socket.handle (), ZLINK_RCVMORE, &more, &more_size)
                        == 0);
                assert (zlink_msg_close (&msg) == 0);
            }
            ++publications_received;
        }

        if (iteration == 1) {
            assert (sub_socket.connect ("inproc://someInProcDescriptor") == 0);
            sleep_ms (300);
        }
        if (iteration == 4)
            assert (sub_socket.disconnect ("inproc://someInProcDescriptor") == 0);
        if (iteration > 4 && rc == 0)
            break;

        assert (pub_socket.send ("foo", 4, zlink::send_flag::sndmore) == 4);
        assert (pub_socket.send ("this is foo!", 13) == 13);
    }

    assert (publications_received == 3);
    assert (!is_subscribed);
}

} // namespace

int main ()
{
    test_disconnect_inproc ();
    return 0;
}
