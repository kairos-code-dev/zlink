/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::shoppingmall
{

inline void add_shoppingmall_json_codecs (framework::codec_options_builder_t codecs)
{
    codecs.add_json<start_order_req_t,
                    start_order_res_t,
                    get_order_state_req_t,
                    get_order_state_res_t,
                    start_order_workflow_req_t,
                    start_order_workflow_res_t,
                    continue_order_workflow_req_t,
                    continue_order_workflow_res_t,
                    rebuild_order_projection_req_t,
                    rebuild_order_projection_res_t,
                    delete_order_projection_req_t,
                    seed_pending_idempotency_req_t,
                    server_assertion_req_t,
                    server_assertion_res_t,
                    order_state_t> ();
}

} // namespace zlink::samples::shoppingmall
