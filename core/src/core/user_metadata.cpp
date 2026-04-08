/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/user_metadata.hpp"

#include <algorithm>
#include <new>
#include <string.h>

namespace
{
void encode_u16_be_local (uint16_t value_, unsigned char *out_)
{
    out_[0] = static_cast<unsigned char> ((value_ >> 8) & 0xffu);
    out_[1] = static_cast<unsigned char> (value_ & 0xffu);
}

uint16_t decode_u16_be_local (const unsigned char *in_)
{
    return (static_cast<uint16_t> (in_[0]) << 8)
           | static_cast<uint16_t> (in_[1]);
}
}

zlink::user_metadata_t::user_metadata_t ()
{
}

zlink::user_metadata_t::user_metadata_t (const user_metadata_t &other_) :
    _entries (other_._entries)
{
}

bool zlink::user_metadata_t::empty () const
{
    return _entries.empty ();
}

int zlink::user_metadata_t::set (uint16_t key_,
                                 const void *value_,
                                 size_t value_size_)
{
    const unsigned char *begin = static_cast<const unsigned char *> (value_);
    entry_t *entry = find_mutable (key_);
    if (!entry) {
        entry_t created;
        created.key = key_;
        try {
            if (begin && value_size_ > 0)
                created.value.assign (begin, begin + value_size_);
            _entries.push_back (created);
        } catch (const std::bad_alloc &) {
            errno = ENOMEM;
            return -1;
        }
        std::sort (_entries.begin (), _entries.end (),
                   [](const entry_t &lhs_, const entry_t &rhs_) {
                       return lhs_.key < rhs_.key;
                   });
        return 0;
    }

    try {
        if (begin && value_size_ > 0)
            entry->value.assign (begin, begin + value_size_);
        else
            entry->value.clear ();
    } catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

const void *zlink::user_metadata_t::get (uint16_t key_,
                                         size_t *size_out_) const
{
    const entry_t *entry = find (key_);
    if (!entry)
        return NULL;
    if (size_out_)
        *size_out_ = entry->value.size ();
    return entry->value.empty () ? static_cast<const void *> ("")
                                 : static_cast<const void *> (&entry->value[0]);
}

size_t zlink::user_metadata_t::encoded_size () const
{
    size_t total = sizeof (uint16_t);
    for (std::vector<entry_t>::const_iterator it = _entries.begin (),
                                              end = _entries.end ();
         it != end; ++it)
        total += sizeof (uint16_t) + sizeof (uint16_t) + it->value.size ();
    return total;
}

int zlink::user_metadata_t::encode (unsigned char *out_, size_t out_size_) const
{
    if (!out_ || out_size_ < encoded_size ()) {
        errno = EINVAL;
        return -1;
    }

    encode_u16_be_local (static_cast<uint16_t> (_entries.size ()), out_);
    out_ += sizeof (uint16_t);
    for (std::vector<entry_t>::const_iterator it = _entries.begin (),
                                              end = _entries.end ();
         it != end; ++it) {
        encode_u16_be_local (it->key, out_);
        encode_u16_be_local (static_cast<uint16_t> (it->value.size ()),
                             out_ + sizeof (uint16_t));
        out_ += sizeof (uint16_t) * 2;
        if (!it->value.empty ()) {
            memcpy (out_, &it->value[0], it->value.size ());
            out_ += it->value.size ();
        }
    }

    return 0;
}

int zlink::user_metadata_t::decode (const unsigned char *data_, size_t size_)
{
    if (!data_ || size_ < sizeof (uint16_t)) {
        errno = EINVAL;
        return -1;
    }

    const uint16_t count = decode_u16_be_local (data_);
    size_t remaining = size_ - sizeof (uint16_t);
    const unsigned char *cursor = data_ + sizeof (uint16_t);

    user_metadata_t decoded;
    for (uint16_t i = 0; i < count; ++i) {
        if (remaining < sizeof (uint16_t) * 2) {
            errno = EINVAL;
            return -1;
        }

        const uint16_t key = decode_u16_be_local (cursor);
        const uint16_t value_size =
          decode_u16_be_local (cursor + sizeof (uint16_t));
        cursor += sizeof (uint16_t) * 2;
        remaining -= sizeof (uint16_t) * 2;
        if (remaining < value_size) {
            errno = EINVAL;
            return -1;
        }

        if (decoded.set (key, cursor, value_size) != 0)
            return -1;
        cursor += value_size;
        remaining -= value_size;
    }

    if (remaining != 0) {
        errno = EINVAL;
        return -1;
    }

    _entries.swap (decoded._entries);
    return 0;
}

zlink::user_metadata_t::entry_t *
zlink::user_metadata_t::find_mutable (uint16_t key_)
{
    for (std::vector<entry_t>::iterator it = _entries.begin (),
                                        end = _entries.end ();
         it != end; ++it) {
        if (it->key == key_)
            return &(*it);
    }
    return NULL;
}

const zlink::user_metadata_t::entry_t *
zlink::user_metadata_t::find (uint16_t key_) const
{
    for (std::vector<entry_t>::const_iterator it = _entries.begin (),
                                              end = _entries.end ();
         it != end; ++it) {
        if (it->key == key_)
            return &(*it);
    }
    return NULL;
}
