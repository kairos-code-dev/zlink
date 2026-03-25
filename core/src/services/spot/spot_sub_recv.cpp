/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"

#include <stdlib.h>
#include <string.h>

namespace zlink
{
namespace
{
static bool spot_frame_has_more (const zlink_msg_t &msg_)
{
    return (reinterpret_cast<const msg_t *> (&msg_)->flags () & msg_t::more)
           != 0;
}

static void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static void close_msgv (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

static bool may_be_ready_probe_topic (const char *topic_, size_t topic_len_)
{
    static const char spot_ready_probe_prefix[] = "__zlink.ready__/";

    return topic_ && topic_len_ >= sizeof (spot_ready_probe_prefix) - 1
           && memcmp (topic_, spot_ready_probe_prefix,
                      sizeof (spot_ready_probe_prefix) - 1)
                == 0;
}

static bool is_ready_probe_message (const char *topic_,
                                    size_t topic_len_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    std::string *raw_filter_out_,
                                    std::string *peer_endpoint_out_)
{
    static const char spot_ready_probe_marker[] =
      "\x00zlink.ready.probe.v1\x00";

    if (!topic_ || topic_len_ == 0 || !parts_ || part_count_ != 2)
        return false;
    if (!may_be_ready_probe_topic (topic_, topic_len_))
        return false;
    if (zlink_msg_size (&parts_[0]) != sizeof (spot_ready_probe_marker) - 1)
        return false;
    if (memcmp (zlink_msg_data (&parts_[0]), spot_ready_probe_marker,
                sizeof (spot_ready_probe_marker) - 1)
        != 0) {
        return false;
    }
    if (zlink_msg_size (&parts_[1]) == 0)
        return false;

    if (raw_filter_out_)
        raw_filter_out_->assign (topic_, topic_len_);
    if (peer_endpoint_out_) {
        peer_endpoint_out_->assign (
          static_cast<const char *> (zlink_msg_data (&parts_[1])),
          zlink_msg_size (&parts_[1]));
    }
    return true;
}

}

int spot_sub_t::set_direct_handler (spot_sub_direct_handler_fn handler_,
                                    void *userdata_)
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (_node && _node->ensure_healthy () != 0)
        return -1;
    {
        scoped_lock_t lock (_sync);
        if (_handler_state.load (std::memory_order_acquire) == handler_active) {
            const unsigned int next_binding =
              (_direct_handler_binding_index + 1) % 2;
            _direct_handler_bindings[next_binding].handler = handler_;
            _direct_handler_bindings[next_binding].userdata = userdata_;
            _active_direct_handler.store (&_direct_handler_bindings[next_binding],
                                          std::memory_order_release);
            _direct_handler_binding_index = next_binding;
            return 0;
        }
        if (_handler_state.load (std::memory_order_acquire) != handler_none) {
            errno = EBUSY;
            return -1;
        }
        _direct_handler_bindings[0].handler = handler_;
        _direct_handler_bindings[0].userdata = userdata_;
        _direct_handler_binding_index = 0;
        _active_direct_handler.store (&_direct_handler_bindings[0],
                                      std::memory_order_release);
        _handler_state.store (handler_active, std::memory_order_release);
    }

    if (socket->sub_dispatch_start (&spot_sub_t::dispatch_from_io, this) == 0)
        return 0;

    {
        scoped_lock_t lock (_sync);
        _active_direct_handler.store (NULL, std::memory_order_release);
        _handler_state.store (handler_none, std::memory_order_release);
    }
    return -1;
}

int spot_sub_t::recv (zlink_routing_id_t *source_rid_out_,
                      zlink_msg_t **parts_,
                      size_t *part_count_,
                      int flags_,
                      char *topic_out_,
                      size_t *topic_len_)
{
    socket_base_t *socket = this->socket ();
    if (!parts_ || !part_count_) {
        errno = EINVAL;
        return -1;
    }
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    if (!_runtime || _runtime->ensure_healthy () != 0)
        return -1;

    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    {
        scoped_lock_t lock (_sync);
        if (_handler_state.load (std::memory_order_acquire) != handler_none) {
            errno = EBUSY;
            return -1;
        }
        lock_routing_id ();
    }

    while (true) {
        *parts_ = NULL;
        *part_count_ = 0;

        std::vector<zlink_msg_t> frames;
        frames.reserve (2);
        int rc = 0;
        zlink_msg_t topic_frame;
        zlink_msg_init (&topic_frame);
        rc = socket->recv (reinterpret_cast<msg_t *> (&topic_frame), flags_);
        if (rc != 0) {
            zlink_msg_close (&topic_frame);
            return -1;
        }

        frames.push_back (topic_frame);

        if (spot_frame_has_more (topic_frame)) {
            zlink_msg_t first_payload_frame;
            zlink_msg_init (&first_payload_frame);
            rc = socket->recv (reinterpret_cast<msg_t *> (&first_payload_frame), 0);
            if (rc != 0) {
                zlink_msg_close (&first_payload_frame);
                zlink_msg_close (&topic_frame);
                return -1;
            }

            if (!spot_frame_has_more (first_payload_frame)) {
                const char *topic_data = static_cast<const char *> (
                  zlink_msg_data (&topic_frame));
                const size_t topic_size = zlink_msg_size (&topic_frame);

                if (topic_len_) {
                    const size_t capacity = *topic_len_;
                    *topic_len_ = topic_size;
                    if (topic_out_ && capacity < topic_size) {
                        if (source_rid_out_)
                            socket->copy_last_recv_source_rid (source_rid_out_);
                        zlink_msg_close (&first_payload_frame);
                        close_msgv (&frames);
                        *topic_len_ = topic_size;
                        errno = EMSGSIZE;
                        return -1;
                    }
                    if (topic_out_ && topic_size > 0)
                        memcpy (topic_out_, topic_data, topic_size);
                    if (topic_out_ && capacity > topic_size)
                        topic_out_[topic_size] = '\0';
                } else if (topic_out_) {
                    if (topic_size > 0)
                        memcpy (topic_out_, topic_data, topic_size);
                    topic_out_[topic_size] = '\0';
                }

                zlink_msg_t *payload =
                  static_cast<zlink_msg_t *> (malloc (sizeof (zlink_msg_t)));
                if (!payload) {
                    zlink_msg_close (&first_payload_frame);
                    close_msgv (&frames);
                    errno = ENOMEM;
                    return -1;
                }
                memset (payload, 0, sizeof (zlink_msg_t));

                msg_t *dst = reinterpret_cast<msg_t *> (payload);
                if (dst->init () != 0
                    || dst->move (
                         *reinterpret_cast<msg_t *> (&first_payload_frame))
                         != 0) {
                    zlink_msg_close (&first_payload_frame);
                    zlink_msg_close (payload);
                    free (payload);
                    close_msgv (&frames);
                    errno = EFAULT;
                    return -1;
                }

                zlink_msg_close (&frames[0]);
                zlink_msg_close (&first_payload_frame);
                if (source_rid_out_)
                    socket->copy_last_recv_source_rid (source_rid_out_);
                *parts_ = payload;
                *part_count_ = 1;
                return 0;
            }

            frames.push_back (first_payload_frame);
            while (true) {
                zlink_msg_t frame;
                zlink_msg_init (&frame);
                rc = socket->recv (reinterpret_cast<msg_t *> (&frame), 0);
                if (rc != 0) {
                    zlink_msg_close (&frame);
                    close_msgv (&frames);
                    return -1;
                }
                frames.push_back (frame);
                if (!spot_frame_has_more (frame))
                    break;
            }
        }

        if (rc != 0) {
            return -1;
        }

        const char *topic_data =
          static_cast<const char *> (zlink_msg_data (&frames[0]));
        const size_t topic_size = zlink_msg_size (&frames[0]);

        if (may_be_ready_probe_topic (topic_data, topic_size)) {
            std::string raw_filter;
            std::string peer_endpoint;
            if (is_ready_probe_message (topic_data,
                                        topic_size,
                                        frames.size () > 1 ? &frames[1] : NULL,
                                        frames.size () - 1,
                                        &raw_filter,
                                        &peer_endpoint)) {
                close_msgv (&frames);
                handle_ready_probe (raw_filter, peer_endpoint);
                continue;
            }
        }

        if (topic_len_) {
            const size_t capacity = *topic_len_;
            *topic_len_ = topic_size;
            if (topic_out_ && capacity < topic_size) {
                if (source_rid_out_)
                    socket->copy_last_recv_source_rid (source_rid_out_);
                close_msgv (&frames);
                *topic_len_ = topic_size;
                errno = EMSGSIZE;
                return -1;
            }
            if (topic_out_ && topic_size > 0)
                memcpy (topic_out_, topic_data, topic_size);
            if (topic_out_ && capacity > topic_size)
                topic_out_[topic_size] = '\0';
        } else if (topic_out_) {
            if (topic_size > 0)
                memcpy (topic_out_, topic_data, topic_size);
            topic_out_[topic_size] = '\0';
        }

        const size_t payload_count = frames.size () - 1;
        if (payload_count == 0) {
            close_msgv (&frames);
            return 0;
        }

        zlink_msg_t *payload = static_cast<zlink_msg_t *> (
          malloc (payload_count * sizeof (zlink_msg_t)));
        if (!payload) {
            close_msgv (&frames);
            errno = ENOMEM;
            return -1;
        }
        memset (payload, 0, payload_count * sizeof (zlink_msg_t));

        for (size_t i = 0; i < payload_count; ++i) {
            msg_t *dst = reinterpret_cast<msg_t *> (&payload[i]);
            if (dst->init () != 0
                || dst->move (*reinterpret_cast<msg_t *> (&frames[i + 1])) != 0) {
                for (size_t j = 0; j <= i; ++j)
                    zlink_msg_close (&payload[j]);
                free (payload);
                close_msgv (&frames);
                errno = EFAULT;
                return -1;
            }
        }

        zlink_msg_close (&frames[0]);
        if (source_rid_out_)
            socket->copy_last_recv_source_rid (source_rid_out_);
        *parts_ = payload;
        *part_count_ = payload_count;
        return 0;
    }
}

void spot_sub_t::dispatch_from_io (const zlink_routing_id_t *source_rid_,
                                   const char *topic_,
                                   size_t topic_len_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_)
{
    spot_sub_t *self = static_cast<spot_sub_t *> (userdata_);
    if (!self) {
        close_parts (parts_, part_count_);
        return;
    }

    if (may_be_ready_probe_topic (topic_, topic_len_)) {
        std::string raw_filter;
        std::string peer_endpoint;
        if (is_ready_probe_message (topic_, topic_len_, parts_, part_count_,
                                    &raw_filter, &peer_endpoint)) {
            self->handle_ready_probe (raw_filter, peer_endpoint);
            close_parts (parts_, part_count_);
            return;
        }
    }

    direct_handler_binding_t *binding =
      self->_active_direct_handler.load (std::memory_order_acquire);
    if (self->_handler_state.load (std::memory_order_acquire) != handler_active
        || !binding || !binding->handler) {
        close_parts (parts_, part_count_);
        return;
    }

    self->_callback_inflight.add (1);
    binding->handler (source_rid_, topic_, topic_len_, parts_, part_count_,
                      binding->userdata);

    const bool callbacks_remaining = self->_callback_inflight.sub (1);
    if (!callbacks_remaining
        && self->_handler_state.load (std::memory_order_acquire)
             != handler_active) {
        scoped_lock_t lock (self->_sync);
        self->_callback_cv.broadcast ();
    }
}
}
