/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

namespace zlink
{
int spot_node_t::apply_pub_defaults (spot_pub_t *pub_, const pub_defaults_t &defaults_)
{
    return zlink::apply_spot_pub_defaults (pub_, defaults_);
}

int spot_node_t::apply_sub_defaults (spot_sub_t *sub_, const sub_defaults_t &defaults_)
{
    return zlink::apply_spot_sub_defaults (sub_, defaults_);
}

spot_pub_t *spot_node_t::create_spot_pub_with_defaults (const pub_defaults_t &defaults_,
                                                        bool node_owned_default_)
{
    LIBZLINK_UNUSED (node_owned_default_);
    if (!pubsub_enabled ()) {
        errno = ENOTSUP;
        return NULL;
    }
    if (ensure_healthy () != 0)
        return NULL;
    if (!_runtime)
        return NULL;

    spot_pub_t *pub = new (std::nothrow) spot_pub_t (this, NULL, 0, false);
    if (!pub) {
        errno = ENOMEM;
        return NULL;
    }

    if (apply_pub_defaults (pub, defaults_) != 0) {
        const int err = errno;
        pub->abort_create ();
        delete pub;
        errno = err;
        return NULL;
    }

    bool bound = false;
    {
        scoped_lock_t lock (_sync);
        _handle_state.pubs.insert (pub);
        bound = !_endpoint_state.bound_endpoint.empty ();
    }

    pub->emit_ready_event ();
    if (bound)
        submit_pub_summary (pub, ZLINK_TOPOLOGY_STATE_READY, 0);
    return pub;
}

spot_sub_t *spot_node_t::create_spot_sub_with_defaults (const sub_defaults_t &defaults_,
                                                        bool node_owned_default_)
{
    if (!pubsub_enabled ()) {
        errno = ENOTSUP;
        return NULL;
    }
    if (ensure_healthy () != 0)
        return NULL;
    uint64_t attachment_id = 0;
    socket_base_t *attachment_socket = NULL;
    if (!_runtime
        || _runtime->create_attachment (spot_attachment_sub, sub_fanout_endpoint ().c_str (),
                                        &attachment_id)
             != 0)
        return NULL;
    attachment_socket = _runtime->attachment_socket (attachment_id);
    if (!attachment_socket || wait_facade_peer (attachment_socket) != 0) {
        const int err = errno != 0 ? errno : ETIMEDOUT;
        (void) _runtime->destroy_attachment (attachment_id);
        errno = err;
        return NULL;
    }

    spot_sub_t *sub =
      new (std::nothrow) spot_sub_t (this, attachment_socket, attachment_id, node_owned_default_);
    if (!sub) {
        (void) _runtime->destroy_attachment (attachment_id);
        errno = ENOMEM;
        return NULL;
    }

    if (apply_sub_defaults (sub, defaults_) != 0) {
        const int err = errno;
        sub->abort_create ();
        delete sub;
        errno = err;
        return NULL;
    }

    {
        scoped_lock_t lock (_sync);
        _handle_state.subs.insert (sub);
        _summary_state.mark_subject_snapshot_changed ();
    }
    if (node_owned_default_)
        _handle_state.handle_defaults.publish_default_sub (sub);
    sub->emit_ready_event ();
    submit_sub_summary (sub, ZLINK_TOPOLOGY_STATE_CONNECTING, 0);
    return sub;
}

spot_sub_t *spot_node_t::ensure_default_sub ()
{
    spot_sub_t *sub = _handle_state.handle_defaults.fast_default_sub ();
    if (sub)
        return sub;

    scoped_lock_t init_lock (_handle_state.handle_defaults.default_sub_init_lock ());
    sub = _handle_state.handle_defaults.default_sub ();
    if (sub)
        return sub;

    sub_defaults_t defaults = _handle_state.handle_defaults.load_sub_defaults ();
    sub = _handle_state.handle_defaults.default_sub ();
    if (sub)
        return sub;
    return create_spot_sub_with_defaults (defaults, true);
}
}
