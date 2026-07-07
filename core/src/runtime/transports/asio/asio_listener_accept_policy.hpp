/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_ASIO_LISTENER_ACCEPT_POLICY_HPP_INCLUDED
#define ZLINK_ASIO_LISTENER_ACCEPT_POLICY_HPP_INCLUDED

#include "core/options.hpp"
#include "utils/env.hpp"

#include <boost/asio/io_context.hpp>

namespace zlink
{
inline size_t asio_stream_accept_target (const options_t &options_)
{
    if (options_.type != ZLINK_CORE_SOCKET_STREAM)
        return 1;
    return env::asio_stream_accept_concurrency ();
}

inline void drain_asio_listener_pending_accepts (boost::asio::io_context &io_context_,
                                                 const size_t *accepting_count_)
{
    for (size_t i = 0; i < 4096 && accepting_count_ && *accepting_count_ > 0; ++i)
        io_context_.poll_one ();
}
}

#endif
