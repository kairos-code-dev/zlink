#include <zlink.hpp>

#include <cerrno>
#include <cassert>

int main ()
{
    assert (zlink_socket (NULL, ZLINK_PAIR) == NULL);
    assert (zlink_errno () == EFAULT);

    assert (zlink_close (NULL) == -1);
    assert (zlink_errno () == ENOTSOCK);

    int hwm = 100;
    size_t hwm_size = sizeof (hwm);
    assert (zlink_setsockopt (NULL, ZLINK_SNDHWM, &hwm, hwm_size) == -1);
    assert (zlink_errno () == ENOTSOCK);

    assert (zlink_getsockopt (NULL, ZLINK_SNDHWM, &hwm, &hwm_size) == -1);
    assert (zlink_errno () == ENOTSOCK);

    assert (zlink_socket_monitor (NULL, "inproc://monitor", ZLINK_EVENT_ALL) == -1);
    assert (zlink_errno () == ENOTSOCK);

    assert (zlink_bind (NULL, "inproc://socket") == -1);
    assert (zlink_errno () == ENOTSOCK);

    assert (zlink_connect (NULL, "inproc://socket") == -1);
    assert (zlink_errno () == ENOTSOCK);

    assert (zlink_unbind (NULL, "inproc://socket") == -1);
    assert (zlink_errno () == ENOTSOCK);

    assert (zlink_disconnect (NULL, "inproc://socket") == -1);
    assert (zlink_errno () == ENOTSOCK);

    return 0;
}
