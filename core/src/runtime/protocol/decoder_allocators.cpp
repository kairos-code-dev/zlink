/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "protocol/decoder_allocators.hpp"

#include "core/msg.hpp"

namespace
{
std::size_t clamp_allocation_size (std::size_t requested_, std::size_t max_)
{
    if (max_ == 0)
        return 1;
    if (requested_ == 0)
        return 1;
    return requested_ > max_ ? max_ : requested_;
}
}

zlink::shared_message_memory_allocator::shared_message_memory_allocator (std::size_t bufsize_) :
    _buf (NULL),
    _buf_size (0),
    _allocation_size (clamp_allocation_size (bufsize_, bufsize_)),
    _max_size (bufsize_),
    _allocated_size (0),
    _msg_content (NULL),
    _max_counters ((_max_size + msg_t::max_vsm_size - 1) / msg_t::max_vsm_size)
{
}

zlink::shared_message_memory_allocator::shared_message_memory_allocator (
  std::size_t bufsize_, std::size_t max_messages_) :
    _buf (NULL),
    _buf_size (0),
    _allocation_size (clamp_allocation_size (bufsize_, bufsize_)),
    _max_size (bufsize_),
    _allocated_size (0),
    _msg_content (NULL),
    _max_counters (max_messages_)
{
}

zlink::shared_message_memory_allocator::shared_message_memory_allocator (std::size_t bufsize_,
                                                                         std::size_t max_messages_,
                                                                         std::size_t max_size_) :
    _buf (NULL),
    _buf_size (0),
    _allocation_size (
      clamp_allocation_size (bufsize_, max_size_ >= bufsize_ ? max_size_ : bufsize_)),
    _max_size (max_size_ >= bufsize_ ? max_size_ : bufsize_),
    _allocated_size (0),
    _msg_content (NULL),
    _max_counters (max_messages_)
{
}

zlink::shared_message_memory_allocator::~shared_message_memory_allocator ()
{
    deallocate ();
}

unsigned char *zlink::shared_message_memory_allocator::allocate ()
{
    const std::size_t target_size = clamp_allocation_size (_allocation_size, _max_size);
    _allocation_size = target_size;

    if (_buf) {
        // release reference count to couple lifetime to messages
        zlink::atomic_counter_t *c = reinterpret_cast<zlink::atomic_counter_t *> (_buf);

        // if refcnt drops to 0, there are no message using the buffer
        // because either all messages have been closed or only vsm-messages
        // were created
        if (c->sub (1)) {
            // buffer is still in use as message data. "Release" it and create a new one
            // release pointer because we are going to create a new buffer
            release ();
        } else if (_allocated_size != target_size) {
            c->~atomic_counter_t ();
            std::free (_buf);
            clear ();
        }
    }

    // if buf != NULL it is not used by any message so we can re-use it for the next run
    if (!_buf) {
        // allocate memory for reference counters together with reception buffer
        std::size_t const allocationsize = target_size + sizeof (zlink::atomic_counter_t)
                                           + _max_counters * sizeof (zlink::msg_t::content_t);

        _buf = static_cast<unsigned char *> (std::malloc (allocationsize));
        alloc_assert (_buf);

        new (_buf) atomic_counter_t (1);
        _allocated_size = target_size;
    } else {
        // release reference count to couple lifetime to messages
        zlink::atomic_counter_t *c = reinterpret_cast<zlink::atomic_counter_t *> (_buf);
        c->set (1);
    }

    _buf_size = target_size;
    _msg_content = reinterpret_cast<zlink::msg_t::content_t *> (_buf + sizeof (atomic_counter_t)
                                                                + _allocated_size);
    return _buf + sizeof (zlink::atomic_counter_t);
}

void zlink::shared_message_memory_allocator::deallocate ()
{
    zlink::atomic_counter_t *c = reinterpret_cast<zlink::atomic_counter_t *> (_buf);
    if (_buf && !c->sub (1)) {
        c->~atomic_counter_t ();
        std::free (_buf);
    }
    clear ();
}

unsigned char *zlink::shared_message_memory_allocator::release ()
{
    unsigned char *b = _buf;
    clear ();
    return b;
}

void zlink::shared_message_memory_allocator::clear ()
{
    _buf = NULL;
    _buf_size = 0;
    _allocated_size = 0;
    _msg_content = NULL;
}

void zlink::shared_message_memory_allocator::inc_ref ()
{
    (reinterpret_cast<zlink::atomic_counter_t *> (_buf))->add (1);
}

void zlink::shared_message_memory_allocator::call_dec_ref (void *, void *hint_)
{
    zlink_assert (hint_);
    unsigned char *buf = static_cast<unsigned char *> (hint_);
    zlink::atomic_counter_t *c = reinterpret_cast<zlink::atomic_counter_t *> (buf);

    if (!c->sub (1)) {
        c->~atomic_counter_t ();
        std::free (buf);
        buf = NULL;
    }
}


std::size_t zlink::shared_message_memory_allocator::size () const
{
    return _buf_size;
}

unsigned char *zlink::shared_message_memory_allocator::data ()
{
    return _buf + sizeof (zlink::atomic_counter_t);
}

void zlink::shared_message_memory_allocator::resize (std::size_t new_size_)
{
    const std::size_t clamped = clamp_allocation_size (new_size_, _allocation_size);
    if (clamped >= _buf_size) {
        _buf_size = clamped;
        return;
    }

    // Avoid frequent shrink/grow oscillation on bursty read sizes.
    if (_buf_size == 0 || clamped * 2 <= _buf_size)
        _buf_size = clamped;
}

void zlink::shared_message_memory_allocator::set_allocation_size (std::size_t new_size_)
{
    _allocation_size = clamp_allocation_size (new_size_, _max_size);
    if (_buf_size > _allocation_size || _buf_size == 0)
        _buf_size = _allocation_size;
}
