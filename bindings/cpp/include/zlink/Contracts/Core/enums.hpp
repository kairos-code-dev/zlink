/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_CORE_ENUMS_HPP_INCLUDED
#define ZLINK_CPP_CORE_ENUMS_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

enum class socket_type : int
{
    any = ZLINK_SOCKET_ANY,
    pair = ZLINK_SOCKET_PAIR,
    pub = ZLINK_SOCKET_PUB,
    sub = ZLINK_SOCKET_SUB,
    dealer = ZLINK_SOCKET_DEALER,
    router = ZLINK_SOCKET_ROUTER,
    xpub = ZLINK_SOCKET_XPUB,
    xsub = ZLINK_SOCKET_XSUB,
    stream = ZLINK_SOCKET_STREAM
};

enum class context_option : int
{
    io_threads = ZLINK_IO_THREADS,
    max_sockets = ZLINK_MAX_SOCKETS,
    socket_limit = ZLINK_SOCKET_LIMIT,
    thread_priority = ZLINK_THREAD_PRIORITY,
    thread_sched_policy = ZLINK_THREAD_SCHED_POLICY,
    max_msgsz = ZLINK_MAX_MSGSZ,
    msg_t_size = ZLINK_MSG_T_SIZE,
    thread_affinity_cpu_add = ZLINK_THREAD_AFFINITY_CPU_ADD,
    thread_affinity_cpu_remove = ZLINK_THREAD_AFFINITY_CPU_REMOVE,
    thread_name_prefix = ZLINK_THREAD_NAME_PREFIX,
    blocky = ZLINK_CTX_OPT_BLOCKY,
    auto_hwm_enable = ZLINK_CTX_OPT_AUTO_HWM_ENABLE,
    auto_hwm_recalc_debounce_ms =
      ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS,
    auto_hwm_profile = ZLINK_CTX_OPT_AUTO_HWM_PROFILE
};

enum class auto_hwm_profile : int
{
    compact = ZLINK_AUTO_HWM_PROFILE_COMPACT,
    low_latency = ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY,
    balanced = ZLINK_AUTO_HWM_PROFILE_BALANCED,
    throughput = ZLINK_AUTO_HWM_PROFILE_THROUGHPUT
};

enum class thread_scheduling_policy_t : int
{
    default_policy = ZLINK_THREAD_SCHED_POLICY_DFLT,
    other = 0,
    fifo = 1,
    round_robin = 2
};

enum class rid_duplicate_policy_t : int
{
    reject = ZLINK_RID_DUPLICATE_REJECT,
    handover = ZLINK_RID_DUPLICATE_HANDOVER,
    replace = ZLINK_RID_DUPLICATE_HANDOVER
};

} // namespace zlink

#endif
