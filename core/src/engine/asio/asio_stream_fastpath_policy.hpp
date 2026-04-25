/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_ASIO_STREAM_FASTPATH_POLICY_HPP_INCLUDED__
#define __ZLINK_ASIO_STREAM_FASTPATH_POLICY_HPP_INCLUDED__

#include <cerrno>
#include <cstdlib>

namespace zlink
{
namespace asio_stream_fastpath_policy
{
inline bool env_flag_enabled (const char *name_)
{
    const char *env = std::getenv (name_);
    return env && *env && *env != '0';
}

inline size_t parse_size_env (const char *name_, size_t fallback_)
{
    const char *env = std::getenv (name_);
    if (!env || !*env)
        return fallback_;
    errno = 0;
    char *end = NULL;
    const unsigned long long value = std::strtoull (env, &end, 10);
    if (errno != 0 || end == env || value == 0)
        return fallback_;
    return static_cast<size_t> (value);
}

inline bool gather_write_enabled ()
{
    return env_flag_enabled ("ZLINK_ASIO_GATHER_WRITE");
}

inline bool single_write_enabled ()
{
    return env_flag_enabled ("ZLINK_ASIO_SINGLE_WRITE");
}

inline size_t gather_threshold ()
{
    return parse_size_env ("ZLINK_ASIO_GATHER_THRESHOLD", 65536);
}

inline bool stream_gather_enabled ()
{
    return !env_flag_enabled ("ZLINK_ASIO_STREAM_DISABLE_GATHER");
}

inline size_t stream_gather_threshold ()
{
    return parse_size_env ("ZLINK_ASIO_STREAM_GATHER_THRESHOLD", 1024);
}

inline size_t stream_tiny_gather_threshold ()
{
    return parse_size_env ("ZLINK_ASIO_STREAM_TINY_GATHER_THRESHOLD", 0);
}

inline bool trace_enabled ()
{
    return env_flag_enabled ("ZLINK_ASIO_TRACE");
}

inline bool enable_handler_alloc ()
{
    return true;
}

inline bool enable_read_drain ()
{
    return true;
}

inline bool enable_speculative_write ()
{
    return true;
}

inline bool enable_rx_slab ()
{
    return true;
}

inline bool enable_non_tcp_spec_read ()
{
    return env_flag_enabled ("ZLINK_ASIO_STREAM_ENABLE_NON_TCP_SPEC_READ");
}

inline size_t spec_write_budget_bytes ()
{
    return 2097152;
}

inline size_t read_drain_max_loops ()
{
    return 64;
}

inline size_t read_drain_max_bytes ()
{
    return 1048576;
}

inline size_t default_target_size ()
{
    return 4096;
}

inline size_t initial_target_cap ()
{
    return parse_size_env ("ZLINK_ASIO_STREAM_INITIAL_TARGET_CAP", 4096);
}

inline size_t clamp_stream_target_limit (size_t target_,
                                         const zlink::options_t &options_,
                                         bool read_path_)
{
    size_t clamped = target_ > 0 ? target_ : default_target_size ();

    if (read_path_ && options_.rcvbuf > 0
        && static_cast<size_t> (options_.rcvbuf) < clamped) {
        clamped = static_cast<size_t> (options_.rcvbuf);
    }

    if (!read_path_ && options_.sndbuf > 0
        && static_cast<size_t> (options_.sndbuf) < clamped) {
        clamped = static_cast<size_t> (options_.sndbuf);
    }

    if (options_.maxmsgsize > 0
        && static_cast<size_t> (options_.maxmsgsize) < clamped) {
        clamped = static_cast<size_t> (options_.maxmsgsize);
    }

    return clamped > 0 ? clamped : static_cast<size_t> (1);
}

inline size_t decoder_initial_read_target (const zlink::options_t &options_)
{
    size_t target = options_.in_batch_size > 0
                      ? static_cast<size_t> (options_.in_batch_size)
                      : default_target_size ();
    if (target > initial_target_cap ())
        target = initial_target_cap ();
    return clamp_stream_target_limit (target, options_, true);
}

inline size_t decoder_max_read_target (const zlink::options_t &options_)
{
    const size_t initial_target = decoder_initial_read_target (options_);
    size_t max_target = initial_target;

    if (options_.rcvbuf > 0
        && static_cast<size_t> (options_.rcvbuf) > max_target)
        max_target = static_cast<size_t> (options_.rcvbuf);

    if (options_.maxmsgsize > 0
        && static_cast<size_t> (options_.maxmsgsize) < max_target)
        max_target = static_cast<size_t> (options_.maxmsgsize);

    if (max_target == 0)
        return initial_target;

    return max_target;
}

inline size_t encoder_initial_write_target (const zlink::options_t &options_)
{
    size_t target = options_.out_batch_size > 0
                      ? static_cast<size_t> (options_.out_batch_size)
                      : default_target_size ();
    if (target > initial_target_cap ())
        target = initial_target_cap ();
    return clamp_stream_target_limit (target, options_, false);
}

inline size_t encoder_max_write_target (const zlink::options_t &options_)
{
    const size_t initial_target = encoder_initial_write_target (options_);
    size_t max_target = initial_target;

    if (options_.sndbuf > 0
        && static_cast<size_t> (options_.sndbuf) > max_target)
        max_target = static_cast<size_t> (options_.sndbuf);

    if (options_.maxmsgsize > 0
        && static_cast<size_t> (options_.maxmsgsize) < max_target)
        max_target = static_cast<size_t> (options_.maxmsgsize);

    if (max_target == 0)
        return initial_target;

    return max_target;
}

inline size_t output_target_batch (const zlink::asio_engine_t &engine_,
                                   const zlink::options_t &options_)
{
    if (options_.type == ZLINK_CORE_SOCKET_STREAM) {
        const size_t stream_target = engine_.stream_encoder_write_target_size ();
        return stream_target > 0 ? stream_target
                                 : static_cast<size_t> (options_.out_batch_size);
    }

    return static_cast<size_t> (options_.out_batch_size);
}
}
}

#endif
