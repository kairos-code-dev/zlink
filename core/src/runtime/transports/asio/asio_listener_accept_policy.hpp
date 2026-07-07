/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_ASIO_LISTENER_ACCEPT_POLICY_HPP_INCLUDED
#define ZLINK_ASIO_LISTENER_ACCEPT_POLICY_HPP_INCLUDED

#include "core/options.hpp"
#include "utils/env.hpp"

namespace zlink
{
inline size_t asio_stream_accept_target (const options_t &options_)
{
    if (options_.type != ZLINK_CORE_SOCKET_STREAM)
        return 1;
    return env::asio_stream_accept_concurrency ();
}
}

#endif
