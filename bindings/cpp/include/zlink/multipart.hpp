/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MULTIPART_HPP_INCLUDED
#define ZLINK_CPP_MULTIPART_HPP_INCLUDED

#include "common.hpp"

#include <cstdlib>

namespace zlink
{

class multipart_t
{
  public:
    multipart_t () : _parts (NULL), _count (0) {}
    ~multipart_t () { reset (); }

    multipart_t (multipart_t &&other) noexcept
        : _parts (other._parts), _count (other._count)
    {
        other._parts = NULL;
        other._count = 0;
    }

    multipart_t &operator= (multipart_t &&other) noexcept
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

    multipart_t (const multipart_t &) = delete;
    multipart_t &operator= (const multipart_t &) = delete;

    void reset ()
    {
        if (_parts) {
            for (size_t i = 0; i < _count; ++i)
                zlink_msg_close (&_parts[i]);
            std::free (_parts);
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
