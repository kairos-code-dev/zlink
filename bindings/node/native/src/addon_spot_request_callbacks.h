/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "addon_common_api.h"

#include <condition_variable>
#include <mutex>

struct request_js_state_t;

struct sync_request_state_t
{
    sync_request_state_t () : result (ZLINK_REQUEST_INTERNAL_ERROR), done (false) {}

    std::mutex mu;
    std::condition_variable cv;
    zlink_request_result_t result;
    bool done;
};

request_js_state_t *create_request_js_state (napi_env env, napi_value handler);
request_js_state_t *create_core_request_js_state (napi_env env, napi_value handler);
void abort_request_js_state (request_js_state_t *state);

void request_reply_callback_trampoline (zlink_request_result_t errnum,
                                        zlink_msg_t *parts,
                                        size_t part_count,
                                        void *userdata);

void sync_request_callback (zlink_request_result_t result,
                            zlink_msg_t *parts,
                            size_t part_count,
                            void *userdata);

zlink_request_result_t wait_sync_request (sync_request_state_t *state);
