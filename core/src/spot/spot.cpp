/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "spot/spot.hpp"
#include "spot/spot_node.hpp"

#include "utils/err.hpp"

#include <string.h>

namespace zlink
{
static const uint32_t spot_tag_value = 0x1e6700da;
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

spot_t::spot_t (spot_node_t *node_) :
    _node (node_),
    _tag (spot_tag_value),
    _queue_hwm (spot_queue_hwm_default)
{
}

spot_t::~spot_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_t::check_tag () const
{
    return _tag == spot_tag_value;
}

int spot_t::publish (const char *topic_,
                     zlink_msg_t *parts_,
                     size_t part_count_,
                     int flags_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->publish (this, topic_, parts_, part_count_, flags_);
}

int spot_t::subscribe (const char *topic_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->subscribe (this, topic_);
}

int spot_t::subscribe_pattern (const char *pattern_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->subscribe_pattern (this, pattern_);
}

int spot_t::unsubscribe (const char *topic_or_pattern_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->unsubscribe (this, topic_or_pattern_);
}

int spot_t::topic_create (const char *topic_, int mode_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->topic_create (topic_, mode_);
}

int spot_t::topic_destroy (const char *topic_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->topic_destroy (topic_);
}

bool spot_t::matches (const std::string &topic_) const
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

bool spot_t::enqueue_message (const std::string &topic_,
                              const std::vector<msg_t> &payload_)
{
    if (_queue.size () >= _queue_hwm)
        return false;

    std::vector<msg_t> parts;
    if (!copy_parts_from_vec (payload_, &parts))
        return false;

    _queue.push_back (spot_message_t ());
    spot_message_t &msg = _queue.back ();
    msg.topic = topic_;
    msg.parts.swap (parts);
    _cv.broadcast ();
    return true;
}

bool spot_t::dequeue_message (spot_message_t *out_)
{
    if (!out_)
        return false;
    if (_queue.empty ())
        return false;
    spot_message_t &front = _queue.front ();
    out_->topic = front.topic;
    out_->parts.swap (front.parts);
    _queue.pop_front ();
    return true;
}

bool spot_t::fetch_ring_message (spot_message_t *out_)
{
    if (!out_ || !_node)
        return false;

    for (std::map<std::string, uint64_t>::iterator it = _ring_cursors.begin ();
         it != _ring_cursors.end (); ++it) {
        const std::string &topic = it->first;
        std::map<std::string, spot_node_t::topic_state_t>::iterator tit =
          _node->_topics.find (topic);
        if (tit == _node->_topics.end ())
            continue;
        if (tit->second.mode != ZLINK_SPOT_TOPIC_RINGBUFFER)
            continue;

        spot_node_t::ringbuffer_t &ring = tit->second.ring;
        if (it->second < ring.start_seq)
            it->second = ring.start_seq;
        const uint64_t end_seq = ring.start_seq + ring.entries.size ();
        if (it->second >= end_seq)
            continue;

        const size_t index = static_cast<size_t> (it->second - ring.start_seq);
        std::vector<msg_t> parts;
        if (!copy_parts_from_vec (ring.entries[index], &parts))
            return false;

        out_->topic = topic;
        out_->parts.swap (parts);
        it->second++;
        return true;
    }

    return false;
}

zlink_msg_t *spot_t::alloc_msgv_from_parts (std::vector<msg_t> *parts_,
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

void spot_t::close_parts (std::vector<msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (*parts_)[i].close ();
    parts_->clear ();
}

int spot_t::recv (zlink_msg_t **parts_,
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

    spot_message_t msg;
    {
        scoped_lock_t lock (_node->_sync);
        while (true) {
            if (dequeue_message (&msg))
                break;
            if (fetch_ring_message (&msg))
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
        strncpy (topic_out_, msg.topic.c_str (), 255);
    }
    if (topic_len_)
        *topic_len_ = msg.topic.size ();

    zlink_msg_t *out_parts = NULL;
    size_t out_count = 0;
    if (!msg.parts.empty ()) {
        out_parts = alloc_msgv_from_parts (&msg.parts, &out_count);
        if (!out_parts) {
            if (parts_)
                *parts_ = NULL;
            if (part_count_)
                *part_count_ = 0;
            return -1;
        }
    }

    if (parts_)
        *parts_ = out_parts;
    if (part_count_)
        *part_count_ = out_count;

    return 0;
}

socket_base_t *spot_t::pub_socket () const
{
    return _node ? _node->pub_socket () : NULL;
}

socket_base_t *spot_t::sub_socket () const
{
    return _node ? _node->sub_socket () : NULL;
}

int spot_t::destroy ()
{
    if (_node)
        _node->remove_spot (this);
    _node = NULL;
    return 0;
}
}
