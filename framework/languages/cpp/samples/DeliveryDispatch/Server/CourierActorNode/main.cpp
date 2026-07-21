/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_timings.hpp"
#include "../Configuration/sample_configuration.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

static std::string g_node_rid;

class courier_actor_t
{
  public:
    explicit courier_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (actor_context_t value) { context = std::move (value); }

    std::string actor_id;
    zlink::framework::actor_ref_t actor_ref;
    actor_context_t context;
    /* 진행 중인 제안: delivery id -> attempt. 배송원의 결정이 오면 이 값을 실어 배차 쪽으로
     * 돌려준다(공통 sample spec §7.4). */
    std::map<std::string, int> offered_attempts;
};

struct courier_actor_factory_t
{
    courier_actor_t create (std::string actor_id) const
    {
        return courier_actor_t (std::move (actor_id));
    }
};

/* actor 조회·생성은 이 노드의 service scope에서 얻은 actor manager가 맡는다. */
class courier_actor_runtime_t
{
  public:
    explicit courier_actor_runtime_t (service_provider_t services) :
        _services (std::move (services))
    {
    }

    service_scope_t create_scope () { return _services.create_scope (); }

  private:
    service_provider_t _services;
};

class courier_entry_spot_t : public entry_spot_t
{
  public:
    explicit courier_entry_spot_t (courier_actor_runtime_t &runtime) : _runtime (runtime) {}

    void configure (entry_spot_context_t &context)
    {
        _context = context;
        /* 공통 sample spec §7.2: CourierSession과 DispatchWorker는 이 노드의 entry spot으로
         * Find/Ensure/Offer를 route 요청한다. 별도 gateway나 session registry는 두지 않는다. */
        context.handlers ()
          .add_handler<&courier_entry_spot_t::find_courier_actor> (
            find_courier_actor_req_t::packet_name)
          .add_handler<&courier_entry_spot_t::ensure_courier_actor> (
            ensure_courier_actor_req_t::packet_name)
          .add_handler<&courier_entry_spot_t::offer_delivery_route> (
            offer_delivery_msg_t::packet_name)
          .add_actor_request<&courier_entry_spot_t::bind_courier_session> (
            bind_courier_session_req_t::packet_name)
          .add_actor_send<&courier_entry_spot_t::offer_delivery> (
            offer_delivery_msg_t::packet_name)
          .add_actor_send<&courier_entry_spot_t::courier_decision> (
            courier_decision_msg_t::packet_name);
    }

    void configure (spot_context_t &context)
    {
        entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    spot_actor_join_response_t on_actor_join (std::string_view, const zlink::message_t &)
    {
        return spot_actor_join_response_t::accept ();
    }

    find_courier_actor_res_t find_courier_actor (const find_courier_actor_req_t &request)
    {
        auto scope = _runtime.create_scope ();
        auto &actors = scope.get_required<session_actor_manager_t> ();
        auto actor = actors.find (request.courier_id);
        if (!actor) {
            return {request.courier_id, std::nullopt};
        }
        return {request.courier_id, actor_ref_snapshot_t::from (actor->ref ())};
    }

    task_t<ensure_courier_actor_res_t>
    ensure_courier_actor (const ensure_courier_actor_req_t &request)
    {
        auto scope = _runtime.create_scope ();
        auto &actors = scope.get_required<session_actor_manager_t> ();
        auto actor =
          actors.get_or_create (sample_names_t::courier_actor_type, request.courier_id, request);
        if (!actor) {
            throw framework_exception_t (actor.error_kind (),
                                         actor.error () ? actor.error ()->what ()
                                                        : "courier actor create failed");
        }
        auto bound = co_await actors.bind_or_get (actor.value ().ref ()).async ();
        auto joined = co_await bound.context ()
                        .join_entry_spot (node_rid_t::from_string (g_node_rid), request)
                        .async ();
        std::cerr << "deliverydispatch courier-route: ensured courier=" << request.courier_id
                  << " node=" << g_node_rid << "\n";
        co_return ensure_courier_actor_res_t{
          request.courier_id,
          actor_ref_snapshot_t::from (
            std::get<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (joined)
              .actor)};
    }

    /* 제안은 응답 없는 one-way다. actor에게 넘기고 즉시 리턴한다 — spot의 직렬 줄을 배송원의
     * 반응 시간 동안 붙잡지 않는다(공통 sample spec §7.4). */
    task_t<void> offer_delivery_route (const offer_delivery_msg_t &message)
    {
        auto scope = _runtime.create_scope ();
        auto &actors = scope.get_required<session_actor_manager_t> ();
        auto actor = actors.find (message.courier_id);
        if (!actor) {
            throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                         "courier actor is not bound: " + message.courier_id);
        }
        co_await actor->relay (offer_delivery_msg_t::packet_name,
                               zlink::message_t::from_json (message));
        co_return;
    }

    bind_courier_session_res_t bind_courier_session (courier_actor_t &,
                                                    spot_actor_request_context_t &,
                                                    const bind_courier_session_req_t &request)
    {
        return {request.courier_id, request.actor, request.session_route};
    }

    void offer_delivery (courier_actor_t &actor,
                         spot_actor_send_context_t &,
                         const offer_delivery_msg_t &message)
    {
        actor.offered_attempts[message.delivery_id] = message.attempt;
        actor.context.bound_session ()
          .send (offer_delivery_notify_t{message.courier_id, message.delivery_id,
                                         message.pickup_address, message.dropoff_address})
          .submit ();
    }

    /* 배송원의 결정은 배차 쪽으로 one-way로 돌려준다. 노드는 시한을 세지 않는다 — 제안 시한은
     * DispatchWorker가 소유한다(공통 sample spec §7.4). */
    void courier_decision (courier_actor_t &actor,
                           spot_actor_send_context_t &,
                           const courier_decision_msg_t &decision)
    {
        const auto offered = actor.offered_attempts.find (decision.delivery_id);
        if (offered == actor.offered_attempts.end ()) {
            std::cerr << "deliverydispatch courier-actor: decision for an unknown offer delivery="
                      << decision.delivery_id << "\n";
            return;
        }
        const auto attempt = offered->second;
        actor.offered_attempts.erase (offered);

        auto scope = _runtime.create_scope ();
        scope.get_required<channel_client_t> ()
          .send (sample_names_t::dispatch_route_channel,
                 offer_delivery_result_msg_t{decision.delivery_id, decision.courier_id, attempt,
                                             decision.accepted, decision.reason})
          .submit ();
    }

  private:
    courier_actor_runtime_t &_runtime;
    entry_spot_context_t _context;
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    const std::string node_rid = configuration.role.node_rid;
    if (node_rid.empty ()) {
        throw std::runtime_error ("sample.role.nodeRid is required for the courier actor node");
    }
    g_node_rid = node_rid;
    const auto spot_router_endpoint = node_rid == sample_names_t::courier_actor_node_1
                                        ? topology.courier_actor_node_1_router_endpoint
                                        : topology.courier_actor_node_2_router_endpoint;
    const auto spot_endpoint = node_rid == sample_names_t::courier_actor_node_1
                                 ? topology.courier_actor_node_1_endpoint
                                 : topology.courier_actor_node_2_endpoint;

    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label ("deliverydispatch-" + node_rid);
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        auto runtime =
          std::make_unique<courier_actor_runtime_t> (options.services ().build_provider ());
        auto *runtime_ptr = runtime.get ();
        options.services ().add_singleton<courier_actor_runtime_t> (std::move (runtime));
        /* 배송원의 결정을 배차 쪽으로 돌려보내는 통로. */
        options.add_client_server_channel (sample_names_t::dispatch_route_channel)
          .enable_client ();
        auto actor_mesh = options.add_route_mesh (sample_names_t::courier_actor_discovery);
        actor_mesh.set_routing_id (zlink::routing_id_t::from (node_rid))
          .listen (spot_router_endpoint);
        actor_mesh.channel_name (sample_names_t::courier_actor_discovery);
        actor_mesh.add_entry_spot<courier_entry_spot_t> (
            [runtime_ptr] { return std::make_shared<courier_entry_spot_t> (*runtime_ptr); })
          .add_actor_factory<courier_actor_factory_t> (sample_names_t::courier_actor_type);
    });
    return app.run (argc, argv);
}
