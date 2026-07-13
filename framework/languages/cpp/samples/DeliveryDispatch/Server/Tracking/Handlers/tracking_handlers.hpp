/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Configuration/evidence_store.hpp"
#include "../../Configuration/sample_names.hpp"
#include "../Spots/DeliveryTrackingSpot/delivery_spot_directory.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <string>

namespace zlink::samples::deliverydispatch
{

/* 공통 sample spec §7.3: Tracking은 상태 event를 기록한 뒤 고객 actor에게
 * `DeliveryStatusUpdatedMsg`를 응답 없는 one-way로 보낸다. push는 고객 actor가 자기 bound
 * session으로 한다 — Tracking은 client session을 알지 않는다. */
class delivery_status_changed_handler_t
{
  public:
    using request_type = delivery_status_changed_req_t;
    using reply_type = delivery_status_changed_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<evidence_store_t,
                                          zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t,
                                          zlink::framework::actor_client_t,
                                          delivery_spot_directory_t>;
    static constexpr const char *topic_name = "DeliveryStatusChangedReq";

    delivery_status_changed_handler_t (evidence_store_t &evidence,
                                       zlink::framework::route_client_t &routes,
                                       zlink::framework::spot_handle_resolver_t &spot_handles,
                                       zlink::framework::actor_client_t &actors,
                                       delivery_spot_directory_t &directory) :
        _evidence (evidence), _routes (routes), _spot_handles (spot_handles), _actors (actors),
        _directory (directory)
    {
    }

    zlink::framework::task_t<delivery_status_changed_res_t>
    handle (const delivery_status_changed_req_t &request)
    {
        _evidence.append (request);
        _directory.get_or_create (request.delivery_id).record (request);

        auto customer_entry_spot = co_await _spot_handles.resolve_spot_handle (
          zlink::framework::spot_rid_t::from_string (sample_names_t::customer_spot_node));
        if (!customer_entry_spot) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::spot_route_not_found,
              "customer entry spot has no live location row");
        }
        auto found = co_await _routes
                       .request_to_spot (*customer_entry_spot,
                                         find_customer_actor_req_t{sample_names_t::customer_id})
                       .async<find_customer_actor_res_t> ();
        if (found.actor) {
            _actors
              .send_to_actor (found.actor->to_actor_ref (sample_names_t::customer_actor_type),
                              delivery_status_updated_msg_t{request.delivery_id,
                                                            sample_names_t::customer_id,
                                                            request.status, request.courier_id,
                                                            request.occurred_at})
              .submit ();
        } else {
            std::cerr << "deliverydispatch tracking: no customer actor for delivery="
                      << request.delivery_id << "\n";
        }

        std::cerr << "deliverydispatch tracking: status delivery=" << request.delivery_id
                  << " status=" << request.status << " courier=" << request.courier_id << "\n";
        co_return delivery_status_changed_res_t{request.delivery_id, request.status};
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::route_client_t &_routes;
    zlink::framework::spot_handle_resolver_t &_spot_handles;
    zlink::framework::actor_client_t &_actors;
    delivery_spot_directory_t &_directory;
};

} // namespace zlink::samples::deliverydispatch
