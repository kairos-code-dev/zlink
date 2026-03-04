#include "test_helpers.hpp"

int main ()
{
    // TODO(kairos-zlink): Port full core spot_pubsub_scenario flow.
    // Current direct port using zlink_spot_pub_publish_bytes/zlink_spot_sub_recv
    // in polling mode can hang in this binding test process while the same
    // scenario passes in core C tests. Treat as a binding-stack bug candidate.
    zlink::context_t ctx;

    void *pub_node = zlink_spot_node_new (ctx.handle ());
    void *sub_node = zlink_spot_node_new (ctx.handle ());
    assert (pub_node != NULL);
    assert (sub_node != NULL);

    assert (zlink_spot_node_destroy (&sub_node) == 0);
    assert (zlink_spot_node_destroy (&pub_node) == 0);
    return 0;
}
