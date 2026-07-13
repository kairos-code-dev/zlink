/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_timings.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <condition_variable>
#include <iostream>
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
};

struct courier_actor_factory_t
{
    courier_actor_t create (std::string actor_id) const
    {
        return courier_actor_t (std::move (actor_id));
    }
};

/* 배송원이 stream session으로 내려주는 결정을 offer 요청과 만나게 하는 랑데부. offer 핸들러는
 * 결정이 오거나 배차 결정 시한이 지날 때까지 기다린다. */
class courier_decision_directory_t
{
  public:
    void begin (const offer_delivery_req_t &offer)
    {
        const std::lock_guard lock (_mutex);
        _pending[key (offer.courier_id, offer.delivery_id)] = std::nullopt;
    }

    offer_delivery_res_t wait (const offer_delivery_req_t &offer)
    {
        std::unique_lock lock (_mutex);
        const auto pending_key = key (offer.courier_id, offer.delivery_id);
        const auto ready =
          _condition.wait_for (lock, sample_timings_t::courier_decision_timeout, [&] {
              const auto found = _pending.find (pending_key);
              return found != _pending.end () && found->second.has_value ();
          });
        if (!ready) {
            _pending.erase (pending_key);
            return {offer.delivery_id, offer.courier_id, false,
                    "courier did not respond before dispatch timeout"};
        }
        auto decision = std::move (*_pending[pending_key]);
        _pending.erase (pending_key);
        return {decision.delivery_id, decision.courier_id, decision.accepted, decision.reason};
    }

    void complete (courier_decision_msg_t decision)
    {
        {
            const std::lock_guard lock (_mutex);
            const auto pending_key = key (decision.courier_id, decision.delivery_id);
            const auto found = _pending.find (pending_key);
            if (found == _pending.end ()) {
                return;
            }
            found->second = std::move (decision);
        }
        _condition.notify_all ();
    }

  private:
    static std::string key (const std::string &courier_id, const std::string &delivery_id)
    {
        return courier_id + "|" + delivery_id;
    }

    std::mutex _mutex;
    std::condition_variable _condition;
    std::map<std::string, std::optional<courier_decision_msg_t>> _pending;
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
    courier_entry_spot_t (courier_decision_directory_t &decisions,
                          courier_actor_runtime_t &runtime) :
        _decisions (decisions), _runtime (runtime)
    {
    }

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
            offer_delivery_req_t::packet_name)
          .add_actor_request<&courier_entry_spot_t::bind_courier_session> (
            bind_courier_session_req_t::packet_name)
          .add_actor_request<&courier_entry_spot_t::offer_delivery> (
            offer_delivery_req_t::packet_name)
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

    task_t<offer_delivery_res_t> offer_delivery_route (const offer_delivery_req_t &request)
    {
        auto scope = _runtime.create_scope ();
        auto &actors = scope.get_required<session_actor_manager_t> ();
        auto actor = actors.find (request.courier_id);
        if (!actor) {
            throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                         "courier actor is not bound: " + request.courier_id);
        }
        _decisions.begin (request);
        (void) co_await actor
          ->relay_request (offer_delivery_req_t::packet_name,
                           zlink::message_t::from_json (request))
          .async ();
        co_return _decisions.wait (request);
    }

    bind_courier_session_res_t bind_courier_session (courier_actor_t &,
                                                    spot_actor_request_context_t &,
                                                    const bind_courier_session_req_t &request)
    {
        return {request.courier_id, request.actor, request.session_route};
    }

    task_t<offer_delivery_res_t> offer_delivery (courier_actor_t &actor,
                                                 spot_actor_request_context_t &,
                                                 const offer_delivery_req_t &request)
    {
        actor.context.bound_session ()
          .send (offer_delivery_notify_t{request.courier_id, request.delivery_id,
                                         request.pickup_address, request.dropoff_address})
          .submit ();
        co_return offer_delivery_res_t{request.delivery_id, request.courier_id, true,
                                       "offer pushed to bound courier session"};
    }

    void courier_decision (courier_actor_t &,
                           spot_actor_send_context_t &,
                           const courier_decision_msg_t &decision)
    {
        _decisions.complete (decision);
    }

  private:
    courier_decision_directory_t &_decisions;
    courier_actor_runtime_t &_runtime;
    entry_spot_context_t _context;
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <node-rid>\n";
        return 2;
    }

    const std::string node_rid = argv[1];
    g_node_rid = node_rid;
    const sample_topology_t topology;
    const auto spot_router_endpoint = node_rid == sample_names_t::courier_actor_node_1
                                        ? topology.courier_actor_node_1_router_endpoint
                                        : topology.courier_actor_node_2_router_endpoint;
    const auto spot_endpoint = node_rid == sample_names_t::courier_actor_node_1
                                 ? topology.courier_actor_node_1_endpoint
                                 : topology.courier_actor_node_2_endpoint;

    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-" + node_rid + ".log")
          .trace_label ("deliverydispatch-" + node_rid);
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        auto decisions = std::make_unique<courier_decision_directory_t> ();
        auto *decisions_ptr = decisions.get ();
        auto runtime =
          std::make_unique<courier_actor_runtime_t> (options.services ().build_provider ());
        auto *runtime_ptr = runtime.get ();
        options.services ()
          .add_singleton<courier_decision_directory_t> (std::move (decisions))
          .add_singleton<courier_actor_runtime_t> (std::move (runtime));
        options.add_spot_mesh (sample_names_t::courier_actor_discovery)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (spot_endpoint)
          .add_entry_spot<courier_entry_spot_t> ([decisions_ptr, runtime_ptr] {
              return std::make_shared<courier_entry_spot_t> (*decisions_ptr, *runtime_ptr);
          })
          .add_actor_factory<courier_actor_factory_t> (sample_names_t::courier_actor_type);
    });
    return app.run (argc, argv);
}
