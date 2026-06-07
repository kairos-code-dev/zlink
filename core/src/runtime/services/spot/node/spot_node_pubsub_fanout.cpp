/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"

namespace zlink
{
namespace
{

bool spot_logical_topic_matches_local (const std::shared_ptr<spot_logical_state_t> &state_,
                                       const std::string &topic_)
{
    if (!state_ || topic_.empty ())
        return false;

    scoped_lock_t lock (state_->pubsub_sync);
    if (state_->subscription_topics.count (topic_) != 0)
        return true;
    for (std::set<std::string>::const_iterator it = state_->subscription_patterns.begin ();
         it != state_->subscription_patterns.end (); ++it) {
        if (topic_.size () >= it->size ()
            && memcmp (topic_.data (), it->data (), it->size ()) == 0) {
            return true;
        }
    }
    return false;
}

int spot_copy_publish_parts_to_block_local (zlink_msg_t *parts_,
                                            size_t part_count_,
                                            spot_owned_msg_parts_t *out_)
{
    if (!out_ || (part_count_ > 0 && !parts_)) {
        errno = EINVAL;
        return -1;
    }

    spot_clear_msg_parts (out_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t frame;
        spot_init_msg_frame (&frame);
        if (zlink_msg_copy (&frame, &parts_[i]) != 0) {
            const int err = errno;
            spot_close_msg_frame (&frame);
            spot_clear_msg_parts (out_);
            errno = err;
            return -1;
        }
        out_->push_back (frame);
    }
    return 0;
}

}

int spot_node_t::fanout_local_publish (const zlink_routing_id_t *source_rid_,
                                       const char *topic_id_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_)
{
    if (!topic_id_ || topic_id_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    std::vector<std::shared_ptr<spot_logical_state_t>> states;
    snapshot_spot_states (&states);
    if (states.empty ())
        return 0;

    struct fanout_target_t
    {
        std::shared_ptr<spot_logical_state_t> state;
    };
    std::vector<fanout_target_t> targets;
    const std::string topic (topic_id_);
    for (size_t i = 0; i < states.size (); ++i) {
        if (!spot_logical_topic_matches_local (states[i], topic))
            continue;
        fanout_target_t target;
        target.state = states[i];
        targets.push_back (target);
    }
    if (targets.empty ())
        return 0;

    std::shared_ptr<spot_logical_pubsub_message_t> block (new (std::nothrow)
                                                            spot_logical_pubsub_message_t ());
    if (!block) {
        errno = ENOMEM;
        return -1;
    }

    memset (&block->source_rid, 0, sizeof (block->source_rid));
    if (source_rid_)
        block->source_rid = *source_rid_;
    block->topic_id = topic;
    if (spot_copy_publish_parts_to_block_local (parts_, part_count_, &block->parts) != 0)
        return -1;

    for (size_t i = 0; i < targets.size (); ++i) {
        bool should_signal = false;
        {
            scoped_lock_t lock (targets[i].state->pubsub_sync);
            targets[i].state->subscribe_queue.push_back (block);
            if (!targets[i].state->subscribe_signal_armed) {
                targets[i].state->subscribe_signal_armed = true;
                should_signal = true;
            }
        }
        if (should_signal && targets[i].state->subscribe_signaler.valid ())
            targets[i].state->subscribe_signaler.send ();
        {
            scoped_lock_t lock (_sync);
            for (std::set<spot_handle_t *>::const_iterator it = _handle_state.facades.begin ();
                 it != _handle_state.facades.end (); ++it) {
                if ((*it)->logical_state == targets[i].state) {
                    zlink_spot_notify_dispatch_info (*it,
                                                     ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE,
                                                     ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, *it);
                    break;
                }
            }
        }
    }
    return 0;
}

}
