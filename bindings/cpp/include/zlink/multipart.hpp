/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MULTIPART_HPP_INCLUDED
#define ZLINK_CPP_MULTIPART_HPP_INCLUDED

#include "message.hpp"

#include <vector>

namespace zlink
{

class multipart_t
{
  public:
    multipart_t () : _parts () {}
    ~multipart_t () { reset (); }

    multipart_t (multipart_t &&other) noexcept
        : _parts (std::move (other._parts))
    {
    }

    multipart_t &operator= (multipart_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        reset ();
        _parts = std::move (other._parts);
        return *this;
    }

    multipart_t (const multipart_t &) = delete;
    multipart_t &operator= (const multipart_t &) = delete;

    void reset () { _parts.clear (); }

    message_t *data () { return _parts.empty () ? NULL : _parts.data (); }
    const message_t *data () const
    {
        return _parts.empty () ? NULL : _parts.data ();
    }
    size_t size () const { return _parts.size (); }

    message_t &operator[] (size_t idx_) { return _parts[idx_]; }
    const message_t &operator[] (size_t idx_) const { return _parts[idx_]; }

    void adopt (std::vector<message_t> &&parts_)
    {
        _parts = std::move (parts_);
    }

  private:
    std::vector<message_t> _parts;
};

} // namespace zlink

#endif
