/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_NATIVE_SEND_RESULT_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_NATIVE_SEND_RESULT_HPP_INCLUDED

#include <zlink/Contracts/Sockets/results.hpp>

#include <cerrno>

namespace zlink
{
namespace detail
{

inline send_result_t to_send_result (int result_) noexcept
{
    switch (result_) {
    case ZLINK_SUBMIT_OK:
        return send_result_t::sent;
    case ZLINK_SUBMIT_BACKPRESSURED:
        return send_result_t::backpressured;
    case ZLINK_SUBMIT_NOT_CONNECTED:
        return send_result_t::not_ready;
    default:
        return send_result_t::sent;
    }
}

inline bool classify_nonblocking_send_errno (int err_,
                                             send_result_t &result_) noexcept
{
    switch (err_) {
    case EAGAIN:
        result_ = send_result_t::backpressured;
        return true;
    case ENOTCONN:
    case EHOSTUNREACH:
        result_ = send_result_t::not_ready;
        return true;
    default:
        return false;
    }
}

} // namespace detail
} // namespace zlink

#endif
