#include <zlink.hpp>

#include <cassert>
#include <cerrno>

int main ()
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    zlink_version (&major, &minor, &patch);
    assert (major == ZLINK_VERSION_MAJOR);
    assert (minor == ZLINK_VERSION_MINOR);
    assert (patch == ZLINK_VERSION_PATCH);

    assert (zlink_strerror (EINVAL) != NULL);
    return 0;
}
