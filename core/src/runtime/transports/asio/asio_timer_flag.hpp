/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_ASIO_TIMER_FLAG_HPP_INCLUDED
#define ZLINK_ASIO_TIMER_FLAG_HPP_INCLUDED

namespace zlink
{
template <typename cancel_fn_t>
inline void cancel_asio_timer_if_started (bool *started_, cancel_fn_t cancel_fn_)
{
    if (!started_ || !*started_)
        return;
    cancel_fn_ ();
    *started_ = false;
}
}

#endif
