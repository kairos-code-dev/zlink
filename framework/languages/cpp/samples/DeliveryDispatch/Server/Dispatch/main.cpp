/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/evidence_store.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_timings.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

inline std::string now_text ()
{
    return std::to_string (static_cast<long long> (std::time (nullptr)));
}

/* HTTP edge는 배차 요청을 큐에 넣고 접수만 확인한다. 큐를 비우는 것은 DispatchWorker다
 * (공통 sample spec §5: 배차 큐·선택 정책·timeout 재시도는 worker가 소유한다). */
class dispatch_work_queue_t
{
  public:
    void enqueue (assign_delivery_msg_t request)
    {
        const std::lock_guard lock (_mutex);
        _queue.push_back (std::move (request));
    }

    std::optional<assign_delivery_msg_t> take ()
    {
        const std::lock_guard lock (_mutex);
        if (_queue.empty ()) {
            return std::nullopt;
        }
        auto request = std::move (_queue.front ());
        _queue.pop_front ();
        return request;
    }

  private:
    std::mutex _mutex;
    std::deque<assign_delivery_msg_t> _queue;
};

/* 배송원 후보 순서는 worker의 선택 정책이다. */
class courier_selection_policy_t
{
  public:
    const std::vector<std::string> &candidates () const noexcept { return _candidates; }

  private:
    std::vector<std::string> _candidates{"courier-a", "courier-b"};
};

struct dispatch_offer_attempt_t
{
    offer_delivery_res_t response;
    bool delivered = false;

    static dispatch_offer_attempt_t not_delivered (const std::string &delivery_id,
                                                   const std::string &courier_id,
                                                   std::string reason)
    {
        return {offer_delivery_res_t{delivery_id, courier_id, false, std::move (reason)}, false};
    }
};

/* worker가 배송원 actor에 닿는 유일한 통로. 배차 노드의 entry spot으로 route 요청한다. */
class courier_offer_port_t
{
  public:
    courier_offer_port_t (route_client_t &routes, spot_handle_resolver_t &spot_handles) :
        _routes (routes), _spot_handles (spot_handles)
    {
    }

    task_t<dispatch_offer_attempt_t> offer (const assign_delivery_msg_t &delivery,
                                            const std::string &courier_id)
    {
        const auto node = sample_names_t::courier_actor_node (courier_id);
        auto entry_spot =
          co_await _spot_handles.resolve_spot_handle (spot_rid_t::from_string (node));
        if (!entry_spot) {
            throw framework_exception_t (framework_error_kind_t::spot_route_not_found,
                                         "courier entry spot has no live location row: " + node);
        }
        auto found = co_await _routes
                       .request_to_spot (*entry_spot, find_courier_actor_req_t{courier_id})
                       .async<find_courier_actor_res_t> ();
        if (!found.actor) {
            co_return dispatch_offer_attempt_t::not_delivered (delivery.delivery_id, courier_id,
                                                               "courier is not bound");
        }
        auto actor_spot = co_await _spot_handles.resolve_spot_handle (
          spot_rid_t::from_string (std::string (found.actor->node_rid.value ())));
        if (!actor_spot) {
            throw framework_exception_t (
              framework_error_kind_t::spot_route_not_found,
              "courier actor spot has no live location row: "
                + std::string (found.actor->node_rid.value ()));
        }
        auto response =
          co_await _routes
            .request_to_spot (*actor_spot,
                              offer_delivery_req_t{courier_id, delivery.delivery_id,
                                                   delivery.pickup_address,
                                                   delivery.dropoff_address})
            .timeout (sample_timings_t::offer_request_timeout)
            .async<offer_delivery_res_t> ();
        co_return dispatch_offer_attempt_t{response, true};
    }

  private:
    route_client_t &_routes;
    spot_handle_resolver_t &_spot_handles;
};

class delivery_status_publisher_t
{
  public:
    explicit delivery_status_publisher_t (channel_client_t &channels) : _channels (channels) {}

    task_t<void> publish (const assign_delivery_msg_t &delivery,
                          const std::string &status,
                          const std::string &courier_id)
    {
        delivery_status_changed_req_t changed{delivery.delivery_id, status, courier_id,
                                              now_text ()};
        (void) co_await _channels.request (sample_names_t::tracking_route_channel, changed)
          .async<delivery_status_changed_res_t> ();
    }

  private:
    channel_client_t &_channels;
};

/* 배차 큐를 비우는 worker. 배송원 선택 정책과 offer 요청 timeout(재배차 판정)을 소유한다. */
class dispatch_worker_t
{
  public:
    dispatch_worker_t (dispatch_work_queue_t &queue,
                       courier_selection_policy_t &couriers,
                       courier_offer_port_t offers,
                       delivery_status_publisher_t statuses) :
        _queue (queue), _couriers (couriers), _offers (offers), _statuses (statuses)
    {
    }

    task_t<void> drain ()
    {
        while (auto request = _queue.take ()) {
            co_await dispatch (*request);
        }
        co_return;
    }

  private:
    task_t<void> dispatch (assign_delivery_msg_t request)
    {
        std::cerr << "deliverydispatch dispatch: assign delivery=" << request.delivery_id
                  << " customer=" << request.customer_id << "\n";

        const auto &candidates = _couriers.candidates ();
        bool first_candidate = true;
        for (const auto &courier_id : candidates) {
            if (!first_candidate) {
                co_await _statuses.publish (request, delivery_status_t::reassigned, courier_id);
            }
            auto attempt = co_await _offers.offer (request, courier_id);
            if (!attempt.delivered) {
                throw std::runtime_error ("delivery '" + request.delivery_id
                                          + "' could not be offered to " + courier_id);
            }
            if (first_candidate) {
                co_await _statuses.publish (request, delivery_status_t::assigned,
                                            attempt.response.courier_id);
            }
            if (attempt.response.accepted) {
                co_await _statuses.publish (request, delivery_status_t::accepted,
                                            attempt.response.courier_id);
                co_await _statuses.publish (request, delivery_status_t::picked_up,
                                            attempt.response.courier_id);
                co_await _statuses.publish (request, delivery_status_t::delivered,
                                            attempt.response.courier_id);
                co_return;
            }
            first_candidate = false;
        }

        co_await _statuses.publish (request, delivery_status_t::failed, candidates.back ());
        throw std::runtime_error ("delivery '" + request.delivery_id
                                  + "' was rejected by all couriers");
    }

    dispatch_work_queue_t &_queue;
    courier_selection_policy_t &_couriers;
    courier_offer_port_t _offers;
    delivery_status_publisher_t _statuses;
};

/* HTTP edge가 넣은 배차 요청을 worker가 큐 순서대로 비운다. */
class assign_delivery_handler_t
{
  public:
    using message_type = assign_delivery_msg_t;
    using dependency_types = dependency_list_t<dispatch_work_queue_t,
                                               courier_selection_policy_t,
                                               route_client_t,
                                               spot_handle_resolver_t,
                                               channel_client_t>;
    static constexpr const char *topic_name = "AssignDeliveryMsg";

    assign_delivery_handler_t (dispatch_work_queue_t &queue,
                               courier_selection_policy_t &couriers,
                               route_client_t &routes,
                               spot_handle_resolver_t &spot_handles,
                               channel_client_t &channels) :
        _queue (queue), _couriers (couriers), _routes (routes), _spot_handles (spot_handles),
        _channels (channels)
    {
    }

    task_t<void> handle (const assign_delivery_msg_t &request)
    {
        _queue.enqueue (request);
        dispatch_worker_t worker (_queue, _couriers, courier_offer_port_t (_routes, _spot_handles),
                                  delivery_status_publisher_t (_channels));
        co_await worker.drain ();
    }

  private:
    dispatch_work_queue_t &_queue;
    courier_selection_policy_t &_couriers;
    route_client_t &_routes;
    spot_handle_resolver_t &_spot_handles;
    channel_client_t &_channels;
};

class create_delivery_http_handler_t
{
  public:
    using request_type = create_delivery_req_t;
    using reply_type = create_delivery_res_t;
    using dependency_types = dependency_list_t<channel_client_t>;
    static constexpr const char *topic_name = "CreateDeliveryReq";

    explicit create_delivery_http_handler_t (channel_client_t &channels) : _channels (channels) {}

    create_delivery_res_t handle (const create_delivery_req_t &request)
    {
        /* 배차 투입은 응답 없는 one-way send(`AssignDeliveryMsg`)다. HTTP edge는 접수만
         * 확인하고, 진행 상태는 Tracking 기록과 고객 stream push로 전달된다. */
        _channels
          .send (sample_names_t::dispatch_route_channel,
                 assign_delivery_msg_t{request.delivery_id, request.customer_id,
                                       request.pickup_address, request.dropoff_address})
          .submit ();
        std::cerr << "deliverydispatch api: created delivery=" << request.delivery_id << "\n";
        return create_delivery_res_t{request.delivery_id};
    }

  private:
    channel_client_t &_channels;
};

class server_assertion_http_handler_t
{
  public:
    using request_type = server_assertion_req_t;
    using reply_type = server_assertion_res_t;
    using dependency_types = dependency_list_t<evidence_store_t>;
    static constexpr const char *topic_name = "ServerAssertionReq";

    explicit server_assertion_http_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    server_assertion_res_t handle (const server_assertion_req_t &request)
    {
        std::cerr << "deliverydispatch api: assert successful=" << request.successful_delivery_id
                  << " reassigned=" << request.reassigned_delivery_id << "\n";
        const auto success = _evidence.has_sequence (
          request.successful_delivery_id,
          {delivery_status_t::assigned, delivery_status_t::accepted, delivery_status_t::picked_up,
           delivery_status_t::delivered});
        const auto reassigned = _evidence.has_sequence (
          request.reassigned_delivery_id,
          {delivery_status_t::assigned, delivery_status_t::reassigned, delivery_status_t::accepted,
           delivery_status_t::picked_up, delivery_status_t::delivered});
        return {success && reassigned, _evidence.read_lines ()};
    }

  private:
    evidence_store_t &_evidence;
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    const sample_topology_t topology;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-dispatch.log")
          .trace_label ("deliverydispatch-dispatch");
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.services ()
          .add_singleton<evidence_store_t> ()
          .add_singleton<dispatch_work_queue_t> ()
          .add_singleton<courier_selection_policy_t> ();
        options.add_client_server_channel (sample_names_t::dispatch_route_channel)
          .enable_server (topology.dispatch_route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (sample_names_t::dispatch_route_node))
          .enable_client ()
          .use_handler_group ("dispatch");
        options.add_client_server_channel (sample_names_t::tracking_route_channel)
          .enable_client ();
        options.add_spot_mesh (sample_names_t::courier_actor_discovery)
          .set_routing_id (zlink::routing_id_t::from (sample_names_t::dispatch_spot_node))
          .enable_router (topology.dispatch_spot_router_endpoint)
          .enable_pub_sub (topology.dispatch_spot_endpoint);
        options.handlers ().group ("dispatch").add_send<assign_delivery_handler_t> ();
        options.http ()
          .listen (topology.dispatch_api_http_url)
          .map_health ("/health")
          .map_post<create_delivery_http_handler_t> ("/deliveries")
          .map_post<server_assertion_http_handler_t> ("/self-check/assert");
    });
    return app.run (argc, argv);
}
