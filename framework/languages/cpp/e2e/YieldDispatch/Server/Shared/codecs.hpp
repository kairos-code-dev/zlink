/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::yield_dispatch::server
{

inline void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    namespace yd = zlink::framework::e2e::yield_dispatch;
    codecs.add_json ();
    codecs.add_json<yd::ensure_spot_req_t,
                    yd::ensure_spot_reply_t,
                    yd::yield_evidence_req_t,
                    yd::yield_evidence_reply_t,
                    yd::yield_evidence_wait_req_t,
                    yd::delay_req_t,
                    yd::delay_reply_t,
                    yd::yield_actor_binding_t,
                    yd::bind_yield_actors_req_t,
                    yd::bind_yield_actors_reply_t,
                    yd::hold_req_t,
                    yd::hold_command_t,
                    yd::yield_req_t,
                    yd::yield_command_t,
                    yd::worker_yield_req_t,
                    yd::worker_yield_command_t,
                    yd::remote_spot_yield_req_t,
                    yd::timer_start_command_t,
                    yd::timer_stop_command_t,
                    yd::probe_req_t,
                    yd::probe_command_t,
                    yd::actor_yield_req_t,
                    yd::actor_fast_req_t,
                    yd::actor_join_yield_req_t,
                    yd::actor_yield_reply_t,
                    yd::yield_dispatch_reply_t> ();
}

} // namespace zlink::framework::e2e::yield_dispatch::server
