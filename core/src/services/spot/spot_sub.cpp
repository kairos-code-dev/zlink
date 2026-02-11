/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"
#include "services/spot/spot_node.hpp"

#include "utils/err.hpp"

#include <string.h>

namespace zlink
{
static const uint32_t spot_sub_tag_value = 0x1e6700da;
static const size_t spot_queue_hwm_default = 100000;

static void close_parts_local (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

static bool copy_parts_from_vec (const std::vector<msg_t> &src_,
                                 std::vector<msg_t> *out_)
{
    out_->clear ();
    if (src_.empty ())
        return true;
    out_->resize (src_.size ());
    for (size_t i = 0; i < src_.size (); ++i) {
        msg_t &dst = (*out_)[i];
        if (dst.init () != 0) {
            close_parts_local (out_);
            return false;
        }
        msg_t &src = const_cast<msg_t &> (src_[i]);
        if (dst.copy (src) != 0) {
            close_parts_local (out_);
            return false;
        }
    }
    return true;
}

static void retain_shared_message (spot_shared_message_t *shared_)
{
    if (shared_)
        shared_->refs.add (1);
}

static void release_shared_message (spot_shared_message_t *shared_)
{
    if (!shared_)
        return;
    if (shared_->refs.sub (1))
        return;
    close_parts_local (&shared_->parts);
    delete shared_;
}

spot_sub_t::spot_sub_t (spot_node_t *node_) :
    _node (node_),
    _tag (spot_sub_tag_value),
    _queue_hwm (spot_queue_hwm_default),
    _handler (NULL),
    _handler_userdata (NULL),
    _handler_state (handler_none),
    _callback_inflight (0)
{
}

spot_sub_t::~spot_sub_t ()
{
    while (!_queue.empty ()) {
        queue_entry_t &entry = _queue.front ();
        release_shared_message (entry.shared);
        _queue.pop_front ();
    }
    _tag = 0xdeadbeef;
}

bool spot_sub_t::check_tag () const
{
    return _tag == spot_sub_tag_value;
}

int spot_sub_t::subscribe (const char *topic_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->subscribe (this, topic_);
}

int spot_sub_t::subscribe_pattern (const char *pattern_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->subscribe_pattern (this, pattern_);
}

int spot_sub_t::unsubscribe (const char *topic_or_pattern_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->unsubscribe (this, topic_or_pattern_);
}

int spot_sub_t::set_handler (zlink_spot_sub_handler_fn handler_,
                             void *userdata_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }

    bool wait_quiesce = false;
    {
        scoped_lock_t lock (_node->_sync);
        if (handler_) {
            _handler = handler_;
            _handler_userdata = userdata_;
            _handler_state = handler_active;
            return 0;
        }

        _handler_state = handler_clearing;
        _handler = NULL;
        _handler_userdata = NULL;

        if (_callback_inflight.get () == 0) {
            _handler_state = handler_none;
            _callback_cv.broadcast ();
            return 0;
        }

        wait_quiesce = !_node->_worker.is_current_thread ();
    }

    if (!wait_quiesce)
        return 0;

    scoped_lock_t lock (_node->_sync);
    while (_callback_inflight.get () > 0)
        _callback_cv.wait (&_node->_sync, -1);
    _handler_state = handler_none;
    return 0;
}

bool spot_sub_t::matches (const std::string &topic_) const
{
    if (_topics.count (topic_))
        return true;

    for (std::set<std::string>::const_iterator it = _patterns.begin ();
         it != _patterns.end (); ++it) {
        const std::string &prefix = *it;
        if (topic_.compare (0, prefix.size (), prefix) == 0)
            return true;
    }
    return false;
}

bool spot_sub_t::enqueue_message (const std::string &topic_,
                                  const std::vector<msg_t> &payload_)
{
    spot_shared_message_t *shared =
      new (std::nothrow) spot_shared_message_t ();
    if (!shared) {
        errno = ENOMEM;
        return false;
    }
    shared->topic = topic_;
    if (!copy_parts_from_vec (payload_, &shared->parts)) {
        delete shared;
        return false;
    }
    const bool ok = enqueue_shared_message (shared);
    release_shared_message (shared);
    return ok;
}

bool spot_sub_t::enqueue_shared_message (spot_shared_message_t *shared_)
{
    if (!shared_)
        return false;
    if (_queue.size () >= _queue_hwm)
        return false;
    _queue.push_back (queue_entry_t ());
    queue_entry_t &entry = _queue.back ();
    retain_shared_message (shared_);
    entry.shared = shared_;
    _cv.broadcast ();
    return true;
}

bool spot_sub_t::dequeue_message (spot_shared_message_t **out_)
{
    if (!out_)
        return false;
    if (_queue.empty ())
        return false;
    queue_entry_t &front = _queue.front ();
    *out_ = front.shared;
    front.shared = NULL;
    _queue.pop_front ();
    return true;
}

bool spot_sub_t::callback_enabled () const
{
    return _handler_state == handler_active && _handler != NULL;
}

zlink_msg_t *spot_sub_t::alloc_msgv_from_parts (std::vector<msg_t> *parts_,
                                                size_t *count_)
{
    if (count_)
        *count_ = 0;
    if (!parts_ || parts_->empty ())
        return NULL;

    const size_t count = parts_->size ();
    zlink_msg_t *out =
      static_cast<zlink_msg_t *> (malloc (count * sizeof (zlink_msg_t)));
    if (!out) {
        errno = ENOMEM;
        close_parts (parts_);
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&out[i]);
        if (dst->init () != 0 || dst->move ((*parts_)[i]) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&out[j]);
            free (out);
            close_parts (parts_);
            errno = EFAULT;
            return NULL;
        }
    }
    parts_->clear ();
    if (count_)
        *count_ = count;
    return out;
}

zlink_msg_t *spot_sub_t::alloc_msgv_from_parts_ref (
  const std::vector<msg_t> &parts_,
  size_t *count_)
{
    if (count_)
        *count_ = 0;
    if (parts_.empty ())
        return NULL;

    const size_t count = parts_.size ();
    zlink_msg_t *out =
      static_cast<zlink_msg_t *> (malloc (count * sizeof (zlink_msg_t)));
    if (!out) {
        errno = ENOMEM;
        return NULL;
    }

    for (size_t i = 0; i < count; ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&out[i]);
        if (dst->init () != 0) {
            for (size_t j = 0; j < i; ++j)
                zlink_msg_close (&out[j]);
            free (out);
            return NULL;
        }

        msg_t &src = const_cast<msg_t &> (parts_[i]);
        if (dst->copy (src) != 0) {
            zlink_msg_close (&out[i]);
            for (size_t j = 0; j < i; ++j)
                zlink_msg_close (&out[j]);
            free (out);
            return NULL;
        }
    }
    if (count_)
        *count_ = count;
    return out;
}

void spot_sub_t::close_parts (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

int spot_sub_t::recv (zlink_msg_t **parts_,
                      size_t *part_count_,
                      int flags_,
                      char *topic_out_,
                      size_t *topic_len_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }

    spot_shared_message_t *shared = NULL;
    {
        scoped_lock_t lock (_node->_sync);
        if (_handler_state != handler_none) {
            errno = EINVAL;
            return -1;
        }
        while (true) {
            if (dequeue_message (&shared))
                break;
            if (flags_ == ZLINK_DONTWAIT) {
                errno = EAGAIN;
                return -1;
            }
            _cv.wait (&_node->_sync, -1);
        }
    }

    if (topic_out_) {
        memset (topic_out_, 0, 256);
        strncpy (topic_out_, shared->topic.c_str (), 255);
    }
    if (topic_len_)
        *topic_len_ = shared->topic.size ();

    zlink_msg_t *out_parts = NULL;
    size_t out_count = 0;
    if (!shared->parts.empty ()) {
        out_parts = alloc_msgv_from_parts_ref (shared->parts, &out_count);
        if (!out_parts) {
            if (parts_)
                *parts_ = NULL;
            if (part_count_)
                *part_count_ = 0;
            release_shared_message (shared);
            return -1;
        }
    }

    if (parts_)
        *parts_ = out_parts;
    if (part_count_)
        *part_count_ = out_count;
    release_shared_message (shared);

    return 0;
}

int spot_sub_t::set_socket_option (int option_,
                                   const void *optval_,
                                   size_t optvallen_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->set_socket_option (ZLINK_SPOT_NODE_SOCKET_SUB, option_,
                                     optval_, optvallen_);
}

socket_base_t *spot_sub_t::sub_socket () const
{
    return _node ? _node->sub_socket () : NULL;
}

int spot_sub_t::destroy ()
{
    if (_node) {
        if (set_handler (NULL, NULL) != 0)
            return -1;

        {
            scoped_lock_t lock (_node->_sync);
            if (_handler_state != handler_none) {
                errno = EBUSY;
                return -1;
            }
        }
        _node->remove_spot_sub (this);
    }
    _node = NULL;
    return 0;
}
}
