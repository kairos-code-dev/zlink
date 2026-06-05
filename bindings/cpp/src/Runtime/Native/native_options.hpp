/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_OPTIONS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_OPTIONS_HPP_INCLUDED

#include <cerrno>
#include <string>
#include <vector>

namespace zlink
{
namespace detail
{

template <typename Reader>
inline int read_growing_string (Reader reader_, size_t initial_capacity_, std::string &value_)
{
    size_t cap = initial_capacity_ > 0 ? initial_capacity_ : 1u;
    const size_t max_cap = 64u * 1024u;

    while (cap <= max_cap) {
        std::vector<char> buffer (cap);
        size_t size = cap;
        const int rc = reader_ (buffer.data (), buffer.size (), &size);
        if (rc == 0) {
            const size_t bounded = size <= buffer.size () ? size : buffer.size ();
            size_t out_size = bounded;
            if (out_size > 0 && buffer[out_size - 1] == '\0')
                --out_size;
            value_.assign (buffer.data (), out_size);
            return 0;
        }

        if ((errno != EINVAL && errno != EMSGSIZE) || cap == max_cap)
            return rc;

        cap = size > cap ? size : cap * 2u;
        if (cap > max_cap)
            cap = max_cap;
    }

    errno = EINVAL;
    return -1;
}

template <typename Getter, typename Option>
inline int
get_string_option (Getter getter_, void *handle_, Option option_, size_t initial_capacity_, std::string &value_)
{
    return read_growing_string ([&] (char *buffer_, size_t capacity_,
                                     size_t *size_out_) { return getter_ (handle_, option_, buffer_, size_out_); },
                                initial_capacity_, value_);
}

} // namespace detail
} // namespace zlink

#endif
