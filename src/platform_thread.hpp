/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_PLATFORM_THREAD_HPP_INCLUDED__
#define __ZMQ_PLATFORM_THREAD_HPP_INCLUDED__

#include <set>

namespace zmq
{
typedef void *(*thread_entry_t) (void *arg_);

struct platform_thread_t;

platform_thread_t *platform_thread_start (thread_entry_t entry_, void *arg_);
void platform_thread_stop (platform_thread_t *thread_);
bool platform_thread_is_current (const platform_thread_t *thread_);
void platform_thread_destroy (platform_thread_t *thread_);

void platform_thread_apply_scheduling (int priority_,
                                       int scheduling_policy_,
                                       const std::set<int> &affinity_cpus_);
void platform_thread_apply_name (const char *name_);
}

#endif
