#include <zlink.hpp>

#include <cassert>

int main ()
{
    const int has_ipc = zlink_has ("ipc");
    const int has_tls = zlink_has ("tls");
    const int has_ws = zlink_has ("ws");
    const int has_wss = zlink_has ("wss");
    assert (has_ipc == 0 || has_ipc == 1);
    assert (has_tls == 0 || has_tls == 1);
    assert (has_ws == 0 || has_ws == 1);
    assert (has_wss == 0 || has_wss == 1);

    assert (!zlink_has ("pgm"));
    assert (!zlink_has ("epgm"));
    assert (!zlink_has ("tipc"));
    assert (!zlink_has ("norm"));
    assert (!zlink_has ("curve"));
    assert (!zlink_has ("gssapi"));
    assert (!zlink_has ("vmci"));
    assert (!zlink_has ("draft"));

    return 0;
}
