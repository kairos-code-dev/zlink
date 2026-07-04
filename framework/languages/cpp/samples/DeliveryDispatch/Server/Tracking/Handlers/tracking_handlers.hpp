/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Configuration/evidence_store.hpp"
#include "../../Configuration/sample_names.hpp"
#include "../Spots/DeliveryTrackingSpot/delivery_spot_directory.hpp"
#include "../Spots/EntrySpot/customer_entry_spot.hpp"

#include <zlink/framework.hpp>

#include <iostream>

namespace zlink::samples::deliverydispatch
{

class ensure_customer_actor_handler_t
{
  public:
    using request_type = ensure_customer_actor_req_t;
    using reply_type = ensure_customer_actor_res_t;
    static constexpr const char *topic_name = "EnsureCustomerActorReq";

    ensure_customer_actor_res_t handle (const ensure_customer_actor_req_t &request)
    {
        customer_actor_t actor{request.customer_id};
        customer_entry_spot_t entry;
        if (!entry.join (actor)) {
            throw std::runtime_error ("customer entry join rejected");
        }
        return {request.customer_id, actor.snapshot ()};
    }
};

class delivery_status_changed_handler_t
{
  public:
    using request_type = delivery_status_changed_req_t;
    using reply_type = delivery_status_changed_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<evidence_store_t,
                                          zlink::framework::publisher_t,
                                          delivery_spot_directory_t>;
    static constexpr const char *topic_name = "DeliveryStatusChangedReq";

    delivery_status_changed_handler_t (evidence_store_t &evidence,
                                       zlink::framework::publisher_t &fanout,
                                       delivery_spot_directory_t &directory) :
        _evidence (evidence), _fanout (fanout), _directory (directory)
    {
    }

    zlink::framework::task_t<delivery_status_changed_res_t> handle (
      const delivery_status_changed_req_t &request)
    {
        _evidence.append (request);
        _directory.get_or_create (request.delivery_id).record (request);
        delivery_status_notify_t notify{
          request.delivery_id, request.status, request.courier_id, request.occurred_at};
        _fanout.publish (sample_names_t::status_fanout_channel, sample_names_t::status_topic, notify)
          .submit ();
        std::cerr << "deliverydispatch tracking: status delivery=" << request.delivery_id
                  << " status=" << request.status << " courier=" << request.courier_id << "\n";
        co_return delivery_status_changed_res_t{request.delivery_id, request.status};
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::publisher_t &_fanout;
    delivery_spot_directory_t &_directory;
};

} // namespace zlink::samples::deliverydispatch
