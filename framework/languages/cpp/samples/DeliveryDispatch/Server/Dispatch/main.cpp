/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/evidence_store.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_timings.hpp"
#include "../Configuration/sample_configuration.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <atomic>
#include <chrono>
#include <ctime>
#include <map>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <algorithm>
#include <thread>
#include <vector>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

inline std::string now_text ()
{
    return std::to_string (static_cast<long long> (std::time (nullptr)));
}

/* 진행 중인 제안 상태. 배차는 배송원의 결정을 기다리지 않고 이 기록으로 다음 단계를 정한다
 * (공통 sample spec §7.4). */
struct delivery_offer_t
{
    assign_delivery_msg_t request;
    std::size_t candidate_index = 0;
    int attempt = 0;
    std::chrono::steady_clock::time_point deadline;
    bool settled = false;
};

class dispatch_state_t
{
  public:
    /* 새 제안을 기록하고 그 attempt 번호를 돌려준다. */
    int offer (const assign_delivery_msg_t &request,
               std::size_t candidate_index,
               std::chrono::milliseconds timeout)
    {
        const std::lock_guard lock (_mutex);
        auto &offer = _offers[request.delivery_id];
        offer.request = request;
        offer.candidate_index = candidate_index;
        offer.attempt += 1;
        offer.deadline = std::chrono::steady_clock::now () + timeout;
        offer.settled = false;
        return offer.attempt;
    }

    /* 결정이 현재 제안의 것이면 그 제안을 닫고 돌려준다. 늦게 도착한 결정(attempt 불일치)이나
     * 이미 처리된 제안은 무시한다. */
    std::optional<delivery_offer_t> settle (const std::string &delivery_id, int attempt)
    {
        const std::lock_guard lock (_mutex);
        const auto found = _offers.find (delivery_id);
        if (found == _offers.end () || found->second.settled
            || found->second.attempt != attempt) {
            return std::nullopt;
        }
        found->second.settled = true;
        return found->second;
    }

    /* 시한이 지난 제안을 거둔다(sweeper). */
    std::vector<delivery_offer_t> expired ()
    {
        const auto now = std::chrono::steady_clock::now ();
        const std::lock_guard lock (_mutex);
        std::vector<delivery_offer_t> expired_offers;
        for (auto &[delivery_id, offer] : _offers) {
            if (!offer.settled && offer.deadline <= now) {
                offer.settled = true;
                expired_offers.push_back (offer);
            }
        }
        return expired_offers;
    }

    void close (const std::string &delivery_id)
    {
        const std::lock_guard lock (_mutex);
        _offers.erase (delivery_id);
    }

  private:
    std::mutex _mutex;
    std::map<std::string, delivery_offer_t> _offers;
};

/* 배송원 후보 순서는 worker의 선택 정책이다. */
class courier_selection_policy_t
{
  public:
    const std::vector<std::string> &candidates () const noexcept { return _candidates; }

  private:
    std::vector<std::string> _candidates{"courier-a", "courier-b"};
};

/* worker가 배송원 actor에 닿는 유일한 통로. 제안은 응답 없는 one-way다. */
class courier_offer_port_t
{
  public:
    courier_offer_port_t (route_client_t &routes, spot_handle_resolver_t &spot_handles) :
        _routes (routes), _spot_handles (spot_handles)
    {
    }

    task_t<void> offer (const assign_delivery_msg_t &delivery,
                        const std::string &courier_id,
                        int attempt)
    {
        const auto node = sample_names_t::courier_actor_node (courier_id);
        auto entry_spot =
          co_await _spot_handles.resolve_spot_handle (spot_rid_t::from_string (node));
        if (!entry_spot) {
            throw framework_exception_t (framework_error_kind_t::spot_route_not_found,
                                         "courier entry spot has no live location row: " + node);
        }
        _routes
          .send_to_spot (*entry_spot,
                         offer_delivery_msg_t{courier_id, delivery.delivery_id, attempt,
                                              delivery.pickup_address, delivery.dropoff_address})
          .submit ();
        co_return;
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

/* 배차 진행. 어느 단계에서도 배송원의 결정을 기다리지 않는다 — 상태 기록이 다음 단계를 정한다. */
class dispatch_worker_t
{
  public:
    dispatch_worker_t (dispatch_state_t &state,
                       courier_selection_policy_t &couriers,
                       courier_offer_port_t offers,
                       delivery_status_publisher_t statuses) :
        _state (state), _couriers (couriers), _offers (offers), _statuses (statuses)
    {
    }

    /* 첫 제안. Assigned를 기록하고 제안을 보낸 뒤 이 턴은 끝난다. */
    task_t<void> start (const assign_delivery_msg_t &request)
    {
        std::cerr << "deliverydispatch dispatch: assign delivery=" << request.delivery_id
                  << " customer=" << request.customer_id << "\n";
        const auto &courier_id = _couriers.candidates ().front ();
        const auto attempt =
          _state.offer (request, 0, sample_timings_t::courier_decision_timeout);
        co_await _statuses.publish (request, delivery_status_t::assigned, courier_id);
        co_await _offers.offer (request, courier_id, attempt);
    }

    /* 배송원의 결정이 도착했다. 수락이면 진행, 거절이면 다음 후보로 재제안. */
    task_t<void> settle (const delivery_offer_t &offer, bool accepted, const std::string &reason)
    {
        const auto &courier_id = _couriers.candidates ()[offer.candidate_index];
        if (accepted) {
            co_await _statuses.publish (offer.request, delivery_status_t::accepted, courier_id);
            co_await _statuses.publish (offer.request, delivery_status_t::picked_up, courier_id);
            co_await _statuses.publish (offer.request, delivery_status_t::delivered, courier_id);
            _state.close (offer.request.delivery_id);
            co_return;
        }
        std::cerr << "deliverydispatch dispatch: courier=" << courier_id
                  << " did not take delivery=" << offer.request.delivery_id << " (" << reason
                  << ")\n";
        co_await reassign (offer);
    }

    /* 시한이 지난 제안. 다음 후보로 재제안한다 — 제안 시한은 worker가 소유한다. */
    task_t<void> reassign (const delivery_offer_t &offer)
    {
        const auto next_index = offer.candidate_index + 1;
        if (next_index >= _couriers.candidates ().size ()) {
            co_await _statuses.publish (offer.request, delivery_status_t::failed,
                                        _couriers.candidates ().back ());
            _state.close (offer.request.delivery_id);
            std::cerr << "deliverydispatch dispatch: delivery=" << offer.request.delivery_id
                      << " was rejected by all couriers\n";
            co_return;
        }
        const auto &courier_id = _couriers.candidates ()[next_index];
        const auto attempt =
          _state.offer (offer.request, next_index, sample_timings_t::courier_decision_timeout);
        co_await _statuses.publish (offer.request, delivery_status_t::reassigned, courier_id);
        co_await _offers.offer (offer.request, courier_id, attempt);
    }

  private:
    dispatch_state_t &_state;
    courier_selection_policy_t &_couriers;
    courier_offer_port_t _offers;
    delivery_status_publisher_t _statuses;
};

inline dispatch_worker_t make_worker (dispatch_state_t &state,
                                      courier_selection_policy_t &couriers,
                                      route_client_t &routes,
                                      spot_handle_resolver_t &spot_handles,
                                      channel_client_t &channels)
{
    return dispatch_worker_t (state, couriers, courier_offer_port_t (routes, spot_handles),
                              delivery_status_publisher_t (channels));
}

/* HTTP edge가 넣은 배차 요청을 받아 첫 제안을 보낸다. */
class assign_delivery_handler_t
{
  public:
    using message_type = assign_delivery_msg_t;
    using dependency_types = dependency_list_t<dispatch_state_t,
                                               courier_selection_policy_t,
                                               route_client_t,
                                               spot_handle_resolver_t,
                                               channel_client_t>;
    static constexpr const char *topic_name = "AssignDeliveryMsg";

    assign_delivery_handler_t (dispatch_state_t &state,
                               courier_selection_policy_t &couriers,
                               route_client_t &routes,
                               spot_handle_resolver_t &spot_handles,
                               channel_client_t &channels) :
        _state (state), _couriers (couriers), _routes (routes), _spot_handles (spot_handles),
        _channels (channels)
    {
    }

    task_t<void> handle (const assign_delivery_msg_t &request)
    {
        auto worker = make_worker (_state, _couriers, _routes, _spot_handles, _channels);
        co_await worker.start (request);
    }

  private:
    dispatch_state_t &_state;
    courier_selection_policy_t &_couriers;
    route_client_t &_routes;
    spot_handle_resolver_t &_spot_handles;
    channel_client_t &_channels;
};

/* 배송원의 결정. 늦게 도착한 결정은 attempt 불일치로 버려진다. */
class offer_delivery_result_handler_t
{
  public:
    using message_type = offer_delivery_result_msg_t;
    using dependency_types = dependency_list_t<dispatch_state_t,
                                               courier_selection_policy_t,
                                               route_client_t,
                                               spot_handle_resolver_t,
                                               channel_client_t>;
    static constexpr const char *topic_name = "OfferDeliveryResultMsg";

    offer_delivery_result_handler_t (dispatch_state_t &state,
                                     courier_selection_policy_t &couriers,
                                     route_client_t &routes,
                                     spot_handle_resolver_t &spot_handles,
                                     channel_client_t &channels) :
        _state (state), _couriers (couriers), _routes (routes), _spot_handles (spot_handles),
        _channels (channels)
    {
    }

    task_t<void> handle (const offer_delivery_result_msg_t &result)
    {
        auto offer = _state.settle (result.delivery_id, result.attempt);
        if (!offer) {
            std::cerr << "deliverydispatch dispatch: stale decision delivery="
                      << result.delivery_id << " attempt=" << result.attempt << "\n";
            co_return;
        }
        auto worker = make_worker (_state, _couriers, _routes, _spot_handles, _channels);
        co_await worker.settle (*offer, result.accepted, result.reason);
    }

  private:
    dispatch_state_t &_state;
    courier_selection_policy_t &_couriers;
    route_client_t &_routes;
    spot_handle_resolver_t &_spot_handles;
    channel_client_t &_channels;
};

/* 제안 시한을 세는 것은 worker다. 시한이 지난 제안을 훑어 다음 후보로 재제안한다. 재제안 자체도
 * 기다리지 않는다 — 시작한 task는 스스로 돌고, sweeper는 끝난 것만 걷어낸다. */
class offer_deadline_sweeper_t final : public hosted_service_t
{
  public:
    void start (service_provider_t &services) override
    {
        _services = services;
        _running.store (true);
        _thread = std::thread ([this] { run (); });
    }

    void stop () noexcept override
    {
        _running.store (false);
        if (_thread.joinable ()) {
            _thread.join ();
        }
    }

  private:
    void run ()
    {
        /* 재제안 task가 이 scope의 client들을 쓴다. scope는 sweeper와 수명을 같이한다. */
        auto scope = _services->create_scope ();
        auto worker = make_worker (scope.get_required<dispatch_state_t> (),
                                   scope.get_required<courier_selection_policy_t> (),
                                   scope.get_required<route_client_t> (),
                                   scope.get_required<spot_handle_resolver_t> (),
                                   scope.get_required<channel_client_t> ());
        auto &state = scope.get_required<dispatch_state_t> ();
        while (_running.load ()) {
            std::this_thread::sleep_for (sample_timings_t::offer_sweep_interval);
            try {
                sweep (state, worker);
            }
            catch (const std::exception &error) {
                std::cerr << "deliverydispatch dispatch: sweep failed: " << error.what () << "\n";
            }
        }
    }

    void sweep (dispatch_state_t &state, dispatch_worker_t &worker)
    {
        reap ();
        for (const auto &offer : state.expired ()) {
            std::cerr << "deliverydispatch dispatch: offer expired delivery="
                      << offer.request.delivery_id << " attempt=" << offer.attempt << "\n";
            _reassignments.push_back (reassign (worker, offer));
        }
    }

    /* 실패는 여기서 삼킨다 — sweeper는 다음 주기에 다시 온다. */
    task_t<void> reassign (dispatch_worker_t &worker, delivery_offer_t offer)
    {
        try {
            co_await worker.reassign (offer);
        }
        catch (const std::exception &error) {
            std::cerr << "deliverydispatch dispatch: reassign failed delivery="
                      << offer.request.delivery_id << ": " << error.what () << "\n";
        }
    }

    /* 끝난 재제안만 걷어낸다. 기다리지 않는다. */
    void reap ()
    {
        std::erase_if (_reassignments, [] (const task_t<void> &reassignment) {
            return reassignment.await_ready ();
        });
    }

    std::optional<service_provider_t> _services;
    std::atomic_bool _running{false};
    std::thread _thread;
    std::vector<task_t<void>> _reassignments;
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

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label ("deliverydispatch-dispatch");
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.services ()
          .add_singleton<evidence_store_t> (std::make_unique<evidence_store_t> (configuration.evidence_path ()))
          .add_singleton<dispatch_state_t> ()
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
        options.handlers ()
          .group ("dispatch")
          .add_send<assign_delivery_handler_t> ()
          .add_send<offer_delivery_result_handler_t> ();
        options.http ()
          .listen (topology.dispatch_api_http_url)
          .map_health ("/health")
          .map_post<create_delivery_http_handler_t> ("/deliveries")
          .map_post<server_assertion_http_handler_t> ("/self-check/assert");
    });
    app.add_hosted_service (std::make_unique<offer_deadline_sweeper_t> ());
    return app.run (argc, argv);
}
