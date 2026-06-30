/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::deliverydispatch
{

inline void add_deliverydispatch_json_codecs (framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<create_delivery_req_t,
                    delivery_created_t,
                    actor_ref_snapshot_t,
                    ensure_customer_actor_t,
                    customer_actor_ensured_t,
                    subscribe_delivery_req_t,
                    subscribe_delivery_accepted_t,
                    subscribe_customer_to_delivery_t,
                    customer_delivery_subscribed_t,
                    assign_delivery_req_t,
                    assign_delivery_res_t,
                    offer_delivery_req_t,
                    offer_delivery_res_t,
                    courier_decision_t,
                    reassign_delivery_t,
                    delivery_status_changed_t,
                    delivery_status_ack_t,
                    delivery_spot_create_t,
                    delivery_spot_created_t,
                    delivery_spot_join_t,
                    delivery_spot_joined_t,
                    delivery_status_notify_t,
                    server_assertion_req_t,
                    server_assertion_res_t> ();
}

} // namespace zlink::samples::deliverydispatch
