/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_BATCH_POLICY_HPP_INCLUDED__
#define __ZLINK_STREAM_BATCH_POLICY_HPP_INCLUDED__

#include <climits>
#include <cstdlib>

namespace zlink
{
namespace stream_batch_policy
{
inline int parse_non_negative_int_env (const char *name_, int default_value_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return default_value_;

    char *end = NULL;
    const long value = std::strtol (env, &end, 10);
    if (!end || end == env || value < 0 || value > INT_MAX)
        return default_value_;
    return static_cast<int> (value);
}

inline int parse_positive_int_env (const char *name_, int default_value_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return default_value_;

    char *end = NULL;
    const long value = std::strtol (env, &end, 10);
    if (!end || end == env || value <= 0 || value > INT_MAX)
        return default_value_;
    return static_cast<int> (value);
}

inline int minimum_send_batch_size ()
{
    return parse_positive_int_env ("ZLINK_ASIO_STREAM_BATCH_SIZE", 4096);
}

inline int read_headroom_bytes ()
{
    return parse_non_negative_int_env ("ZLINK_ASIO_STREAM_BATCH_HEADROOM", 64);
}

inline int apply_read_headroom (int base_, int headroom_)
{
    if (headroom_ <= 0 || base_ > INT_MAX - headroom_)
        return base_;
    return base_ + headroom_;
}
}
}

#endif
