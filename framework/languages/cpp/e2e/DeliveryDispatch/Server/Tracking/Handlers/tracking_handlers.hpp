/* SPDX-License-Identifier: FSL-1.1-ALv2 */
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

class subscribe_customer_to_delivery_handler_t
{
  public:
    using request_type = subscribe_customer_to_delivery_req_t;
    using reply_type = subscribe_customer_to_delivery_res_t;
    using dependency_types = zlink::framework::dependency_list_t<delivery_spot_directory_t>;
    static constexpr const char *topic_name = "SubscribeCustomerToDeliveryReq";

    explicit subscribe_customer_to_delivery_handler_t (delivery_spot_directory_t &directory) :
        _directory (directory)
    {
    }

    subscribe_customer_to_delivery_res_t handle (const subscribe_customer_to_delivery_req_t &request)
    {
        auto &spot = _directory.get_or_create (request.delivery_id);
        customer_actor_t actor{request.customer_id};
        if (!spot.join (actor, delivery_spot_join_req_t{request.delivery_id, request.customer_id})) {
            throw std::runtime_error ("delivery spot join rejected");
        }
        return {request.customer_id, request.delivery_id};
    }

  private:
    delivery_spot_directory_t &_directory;
};

class delivery_status_changed_handler_t
{
  public:
    using request_type = delivery_status_req_t;
    using reply_type = delivery_status_res_t;
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t,
                                                                zlink::framework::actor_client_t,
                                                                zlink::framework::actor_directory_t,
                                                                zlink::framework::publisher_t,
                                                                delivery_spot_directory_t>;
    static constexpr const char *topic_name = "DeliveryStatusReq";

    delivery_status_changed_handler_t (evidence_store_t &evidence,
                                       zlink::framework::actor_client_t &actors,
                                       zlink::framework::actor_directory_t &actor_directory,
                                       zlink::framework::publisher_t &fanout,
                                       delivery_spot_directory_t &directory) :
        _evidence (evidence),
        _actors (actors),
        _actor_directory (actor_directory),
        _fanout (fanout),
        _directory (directory)
    {
    }

    zlink::framework::task_t<delivery_status_res_t> handle (
      const delivery_status_req_t &request)
    {
        _evidence.append (request);
        _directory.get_or_create (request.delivery_id).record (request);
        delivery_status_updated_msg_t updated{request.delivery_id, request.customer_id,
                                              request.status, request.courier_id,
                                              request.occurred_at};
        auto actor_ref = co_await _actor_directory.find (request.customer_id);
        if (!actor_ref) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "customer actor route was not found");
        }
        _actors.send_to_actor (*actor_ref, updated)
          .submit ();
        delivery_status_notify_t notify{
          request.delivery_id, request.status, request.courier_id, request.occurred_at};
        _fanout.publish (sample_names_t::status_fanout_channel, sample_names_t::status_topic, notify)
          .submit ();
        std::cerr << "deliverydispatch tracking: status delivery=" << request.delivery_id
                  << " status=" << request.status << " courier=" << request.courier_id << "\n";
        co_return delivery_status_res_t{request.delivery_id, request.status};
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::actor_client_t &_actors;
    zlink::framework::actor_directory_t &_actor_directory;
    zlink::framework::publisher_t &_fanout;
    delivery_spot_directory_t &_directory;
};

} // namespace zlink::samples::deliverydispatch
