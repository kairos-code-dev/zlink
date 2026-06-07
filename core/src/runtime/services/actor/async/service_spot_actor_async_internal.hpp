/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_ACTOR_ASYNC_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_ACTOR_ASYNC_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

namespace zlink
{
namespace spot_actor_async
{
typedef zlink_request_result_t (*reply_operation_run_fn) (void *arg_);
typedef void (*lookup_operation_run_fn) (void *arg_, zlink_actor_lookup_result_t *out_);
typedef void (*operation_cleanup_fn) (void *arg_);

zlink_submit_result_t schedule_reply_operation (zlink_reply_handler_fn handler_,
                                                void *userdata_,
                                                uint32_t timeout_ms_,
                                                reply_operation_run_fn run_,
                                                void *arg_,
                                                operation_cleanup_fn cleanup_);

zlink_submit_result_t schedule_lookup_operation (zlink_actor_lookup_handler_fn handler_,
                                                 void *userdata_,
                                                 uint32_t timeout_ms_,
                                                 lookup_operation_run_fn run_,
                                                 void *arg_,
                                                 operation_cleanup_fn cleanup_);
}
}

#endif
