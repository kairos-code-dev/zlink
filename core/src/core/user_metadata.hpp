/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CORE_USER_METADATA_HPP_INCLUDED__
#define __ZLINK_CORE_USER_METADATA_HPP_INCLUDED__

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace zlink
{
class user_metadata_t
{
  public:
    struct entry_t
    {
        uint16_t key;
        std::vector<unsigned char> value;
    };

    user_metadata_t ();
    user_metadata_t (const user_metadata_t &other_);

    bool empty () const;
    int set (uint16_t key_, const void *value_, size_t value_size_);
    const void *get (uint16_t key_, size_t *size_out_) const;
    size_t encoded_size () const;
    int encode (unsigned char *out_, size_t out_size_) const;
    int decode (const unsigned char *data_, size_t size_);

  private:
    entry_t *find_mutable (uint16_t key_);
    const entry_t *find (uint16_t key_) const;

    std::vector<entry_t> _entries;
};
}

#endif
