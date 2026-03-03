/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MSGV_HPP_INCLUDED
#define ZLINK_CPP_MSGV_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

class msgv_t
{
  public:
    msgv_t () : _parts (NULL), _count (0) {}
    ~msgv_t () { reset (); }

    msgv_t (msgv_t &&other) noexcept
        : _parts (other._parts), _count (other._count)
    {
        other._parts = NULL;
        other._count = 0;
    }

    msgv_t &operator= (msgv_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        reset ();
        _parts = other._parts;
        _count = other._count;
        other._parts = NULL;
        other._count = 0;
        return *this;
    }

    msgv_t (const msgv_t &) = delete;
    msgv_t &operator= (const msgv_t &) = delete;

    void reset ()
    {
        if (_parts) {
            zlink_msgv_close (_parts, _count);
            _parts = NULL;
            _count = 0;
        }
    }

    zlink_msg_t *data () { return _parts; }
    const zlink_msg_t *data () const { return _parts; }
    size_t size () const { return _count; }

    zlink_msg_t &operator[] (size_t idx_) { return _parts[idx_]; }
    const zlink_msg_t &operator[] (size_t idx_) const { return _parts[idx_]; }

    void adopt (zlink_msg_t *parts_, size_t count_)
    {
        reset ();
        _parts = parts_;
        _count = count_;
    }

  private:
    zlink_msg_t *_parts;
    size_t _count;
};

} // namespace zlink

#endif
