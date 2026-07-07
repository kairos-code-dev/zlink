/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/runtime/spot_runtime_internal.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

namespace zlink
{
namespace
{
static void spot_internal_receiver_fanout_handler (const zlink_routing_id_t *source_rid_,
                                                   const char *topic_,
                                                   size_t topic_len_,
                                                   zlink_msg_t *parts_,
                                                   size_t part_count_,
                                                   void *userdata_)
{
    spot_node_t *node = static_cast<spot_node_t *> (userdata_);
    if (!node || !node->check_tag ()) {
        zlink_multipart_close (parts_, part_count_);
        return;
    }

    const std::string topic (topic_ ? topic_ : "", topic_len_);
    (void) node->fanout_local_publish (source_rid_, topic.c_str (), parts_, part_count_);
    zlink_multipart_close (parts_, part_count_);
}
}

spot_node_default_handles_t::spot_node_default_handles_t () :
    _default_sub (NULL),
    _internal_receiver (NULL),
    _default_sub_fast (NULL),
    _internal_receiver_fast (NULL)
{
}

int spot_node_default_handles_t::validate_pub_option (int option_,
                                                      const void *optval_,
                                                      size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::is_spot_pub_default_option (option_))
        return 0;
    errno = EINVAL;
    return -1;
}

int spot_node_default_handles_t::validate_sub_option (int option_,
                                                      const void *optval_,
                                                      size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::is_spot_sub_default_option (option_))
        return 0;
    errno = EINVAL;
    return -1;
}

void spot_node_default_handles_t::copy_option_setting (option_setting_t *dst_,
                                                       const void *optval_,
                                                       size_t optvallen_)
{
    zlink::copy_spot_option_setting (dst_, optval_, optvallen_);
}

void spot_node_default_handles_t::store_pub_option (int option_,
                                                    const void *optval_,
                                                    size_t optvallen_)
{
    (void) zlink::store_spot_pub_default (&_pub_defaults, option_, optval_, optvallen_);
}

void spot_node_default_handles_t::store_sub_option (int option_,
                                                    const void *optval_,
                                                    size_t optvallen_)
{
    (void) zlink::store_spot_sub_default (&_sub_defaults, option_, optval_, optvallen_);
}

int spot_node_default_handles_t::set_pub_option (int option_,
                                                 const void *optval_,
                                                 size_t optvallen_)
{
    if (validate_pub_option (option_, optval_, optvallen_) != 0)
        return -1;

    scoped_lock_t lock (_sync);
    store_pub_option (option_, optval_, optvallen_);
    return 0;
}

int spot_node_default_handles_t::set_sub_option (int option_,
                                                 const void *optval_,
                                                 size_t optvallen_)
{
    if (validate_sub_option (option_, optval_, optvallen_) != 0)
        return -1;

    scoped_lock_t init_lock (_default_sub_sync);
    spot_sub_t *default_sub = NULL;
    spot_internal_receiver_t *internal_receiver = NULL;
    {
        scoped_lock_t lock (_sync);
        default_sub = _default_sub;
        internal_receiver = _internal_receiver;
    }

    if (default_sub && default_sub->set_option (option_, optval_, optvallen_) != 0)
        return -1;
    if (internal_receiver && internal_receiver->impl () != default_sub
        && internal_receiver->set_option (option_, optval_, optvallen_) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        store_sub_option (option_, optval_, optvallen_);
    }
    return 0;
}

spot_node_default_handles_t::pub_defaults_t spot_node_default_handles_t::load_pub_defaults () const
{
    scoped_lock_t lock (_sync);
    return _pub_defaults;
}

spot_node_default_handles_t::sub_defaults_t spot_node_default_handles_t::load_sub_defaults () const
{
    scoped_lock_t lock (_sync);
    return _sub_defaults;
}

spot_sub_t *spot_node_default_handles_t::default_sub () const
{
    scoped_lock_t lock (_sync);
    return _default_sub;
}

spot_internal_receiver_t *spot_node_default_handles_t::internal_receiver () const
{
    scoped_lock_t lock (_sync);
    return _internal_receiver;
}

spot_sub_t *spot_node_default_handles_t::fast_default_sub () const
{
    return _default_sub_fast.load (std::memory_order_acquire);
}

spot_internal_receiver_t *spot_node_default_handles_t::fast_internal_receiver () const
{
    return _internal_receiver_fast.load (std::memory_order_acquire);
}

void spot_node_default_handles_t::publish_default_sub (spot_sub_t *sub_)
{
    scoped_lock_t lock (_sync);
    _default_sub = sub_;
    _default_sub_fast.store (sub_, std::memory_order_release);
}

spot_internal_receiver_t *
spot_node_default_handles_t::publish_internal_receiver (spot_internal_receiver_t *receiver_,
                                                        spot_sub_t *created_sub_,
                                                        spot_sub_t *previous_default_sub_,
                                                        bool *installed_out_)
{
    if (installed_out_)
        *installed_out_ = false;

    scoped_lock_t lock (_sync);
    if (_default_sub == created_sub_)
        _default_sub = previous_default_sub_;

    if (_internal_receiver && _internal_receiver != receiver_) {
        _default_sub_fast.store (_default_sub, std::memory_order_release);
        return _internal_receiver;
    }

    _internal_receiver = receiver_;
    _internal_receiver_fast.store (receiver_, std::memory_order_release);
    _default_sub_fast.store (_default_sub, std::memory_order_release);
    if (installed_out_)
        *installed_out_ = true;
    return receiver_;
}

void spot_node_default_handles_t::remove_spot_pub (spot_pub_t *pub_)
{
    LIBZLINK_UNUSED (pub_);
}

bool spot_node_default_handles_t::remove_spot_sub (spot_sub_t *sub_)
{
    bool had_filters = false;
    scoped_lock_t lock (_sync);
    if (_default_sub == sub_) {
        _default_sub = NULL;
        _default_sub_fast.store (NULL, std::memory_order_release);
    }
    if (_internal_receiver && _internal_receiver->impl () == sub_) {
        _internal_receiver = NULL;
        _internal_receiver_fast.store (NULL, std::memory_order_release);
    }
    had_filters = sub_ && sub_->has_filters ();
    return had_filters;
}

void spot_node_default_handles_t::snapshot_destroy_handles (const std::set<spot_pub_t *> &pubs_,
                                                            const std::set<spot_sub_t *> &subs_,
                                                            std::vector<spot_pub_t *> *pubs_out_,
                                                            std::vector<spot_sub_t *> *subs_out_)
{
    if (!pubs_out_ || !subs_out_)
        return;

    pubs_out_->clear ();
    subs_out_->clear ();

    scoped_lock_t lock (_sync);
    pubs_out_->assign (pubs_.begin (), pubs_.end ());
    for (std::set<spot_sub_t *>::const_iterator it = subs_.begin (); it != subs_.end (); ++it) {
        if (_internal_receiver && *it == _internal_receiver->impl ())
            continue;
        subs_out_->push_back (*it);
    }

    _default_sub = NULL;
    _default_sub_fast.store (NULL, std::memory_order_release);
    _internal_receiver_fast.store (NULL, std::memory_order_release);
}

spot_internal_receiver_t *spot_node_default_handles_t::detach_internal_receiver ()
{
    scoped_lock_t lock (_sync);
    spot_internal_receiver_t *receiver = _internal_receiver;
    _internal_receiver = NULL;
    _internal_receiver_fast.store (NULL, std::memory_order_release);
    return receiver;
}

spot_node_t::pub_defaults_t spot_node_t::load_pub_defaults () const
{
    return _handle_state.handle_defaults.load_pub_defaults ();
}

spot_node_t::sub_defaults_t spot_node_t::load_sub_defaults () const
{
    return _handle_state.handle_defaults.load_sub_defaults ();
}


spot_pub_t *spot_node_t::create_spot_pub ()
{
    pub_defaults_t defaults;
    memset (&defaults, 0, sizeof (defaults));
    return create_spot_pub_with_defaults (defaults, false);
}

spot_sub_t *spot_node_t::create_spot_sub ()
{
    sub_defaults_t defaults;
    memset (&defaults, 0, sizeof (defaults));
    return create_spot_sub_with_defaults (defaults, false);
}

spot_internal_receiver_t *spot_node_t::ensure_internal_receiver ()
{
    spot_internal_receiver_t *receiver = _handle_state.handle_defaults.fast_internal_receiver ();
    if (receiver)
        return receiver;

    scoped_lock_t init_lock (_handle_state.handle_defaults.default_sub_init_lock ());

    spot_sub_t *previous_default_sub = NULL;
    receiver = _handle_state.handle_defaults.internal_receiver ();
    if (receiver)
        return receiver;
    previous_default_sub = _handle_state.handle_defaults.default_sub ();

    sub_defaults_t defaults = _handle_state.handle_defaults.load_sub_defaults ();
    receiver = _handle_state.handle_defaults.internal_receiver ();
    if (receiver)
        return receiver;

    spot_sub_t *sub = create_spot_sub_with_defaults (defaults, true);
    if (!sub)
        return NULL;
    receiver = new (std::nothrow) spot_internal_receiver_t (sub);
    if (!receiver) {
        (void) sub->abort_create ();
        delete sub;
        errno = ENOMEM;
        return NULL;
    }
    if (receiver->set_direct_handler (&spot_internal_receiver_fanout_handler, this) != 0) {
        const int err = errno;
        (void) receiver->abort_create ();
        delete receiver;
        delete sub;
        errno = err;
        return NULL;
    }

    bool installed = false;
    spot_internal_receiver_t *published_receiver =
      _handle_state.handle_defaults.publish_internal_receiver (receiver, sub, previous_default_sub,
                                                               &installed);
    if (!installed) {
        {
            scoped_lock_t lock (_sync);
            _handle_state.subs.erase (sub);
            _summary_state.mark_subject_snapshot_changed ();
        }
        (void) receiver->abort_create ();
        delete receiver;
        delete sub;
        return published_receiver;
    }

    return receiver;
}

spot_internal_receiver_t *spot_node_t::internal_receiver () const
{
    return _handle_state.handle_defaults.internal_receiver ();
}

spot_sub_t *spot_node_t::default_sub () const
{
    return _handle_state.handle_defaults.default_sub ();
}

void spot_node_t::remove_spot_pub (spot_pub_t *pub_)
{
    _handle_state.handle_defaults.remove_spot_pub (pub_);
    scoped_lock_t lock (_sync);
    _handle_state.pubs.erase (pub_);
}

void spot_node_t::remove_spot_sub (spot_sub_t *sub_)
{
    const bool had_filters = _handle_state.handle_defaults.remove_spot_sub (sub_);
    scoped_lock_t lock (_sync);
    _handle_state.subs.erase (sub_);
    _summary_state.mark_subject_snapshot_changed ();
    if (had_filters) {
        note_local_sub_filters_changed (true, false);
    }
}

int spot_node_t::destroy_handles ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    int first_error = 0;
    {
        scoped_lock_t lock (_sync);
        _handle_state.handle_defaults.snapshot_destroy_handles (_handle_state.pubs,
                                                                _handle_state.subs, &pubs, &subs);
        for (size_t i = 0; i < pubs.size (); ++i)
            _handle_state.pubs.erase (pubs[i]);
        for (size_t i = 0; i < subs.size (); ++i)
            _handle_state.subs.erase (subs[i]);
        if (!subs.empty ())
            _summary_state.mark_subject_snapshot_changed ();
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        preserve_first_error (pubs[i]->destroy_from_node (), &first_error);
        delete pubs[i];
    }
    for (size_t i = 0; i < subs.size (); ++i) {
        preserve_first_error (subs[i]->destroy_from_node (), &first_error);
        delete subs[i];
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

int spot_node_t::destroy_internal_receiver ()
{
    spot_internal_receiver_t *receiver = _handle_state.handle_defaults.detach_internal_receiver ();
    {
        scoped_lock_t lock (_sync);
        if (receiver)
            _handle_state.subs.erase (receiver->impl ());
        if (receiver)
            _summary_state.mark_subject_snapshot_changed ();
    }

    if (!receiver)
        return 0;

    const bool had_filters = receiver->has_filters ();
    const int rc = receiver->abort_create ();
    spot_sub_t *sub = receiver->impl ();
    delete receiver;
    delete sub;
    if (had_filters)
        note_local_sub_filters_changed (true, false);
    return rc;
}

void spot_node_t::stop_data_plane_sockets ()
{
    if (_runtime)
        _runtime->stop_sockets ();
}

void spot_node_t::close_control_sockets ()
{
    if (_runtime)
        _runtime->close_control_sockets ();
}
}
