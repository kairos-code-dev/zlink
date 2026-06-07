/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"

int zlink::spot_reqrep_internal::process_route_combined_for_local_delivery (
  std::vector<zlink_msg_t> *combined_)
{
    return process_route_combined_for_local_delivery_impl (combined_);
}

int zlink::spot_reqrep_internal::process_parsed_route_combined_for_local_delivery (
  std::vector<zlink_msg_t> *combined_, const parsed_spot_envelope_t &spot_envelope_)
{
    return process_parsed_route_combined_for_local_delivery_impl (combined_, spot_envelope_);
}
