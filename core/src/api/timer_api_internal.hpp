/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_TIMER_API_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_TIMER_API_INTERNAL_HPP_INCLUDED__

#include "utils/precompiled.hpp"

struct timer_handle_t;

timer_handle_t *as_timer_handle (void *timer_);
int timer_handle_signaler_fd (timer_handle_t *timer_, zlink_fd_t *fd_out_);
int timer_handle_acquire_poller_ref (timer_handle_t *timer_);
void timer_handle_release_poller_ref (timer_handle_t *timer_);

#endif
