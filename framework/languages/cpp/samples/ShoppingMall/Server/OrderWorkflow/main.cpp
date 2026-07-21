/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Common/workflow_logic.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_configuration.hpp"

#include <zlink/framework.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace zlink::samples::shoppingmall
{
using namespace zlink::framework;

class order_workflow_spot_t;

struct order_workflow_continue_timer_handler_t
{
    void handle (order_workflow_spot_t &spot, const timer_tick_t &) const;
};

struct order_workflow_spot_create_req_t
{
    static constexpr const char *packet_name = "OrderWorkflowSpotCreateReq";
    std::string order_id;
};

inline void to_json (nlohmann::json &json, const order_workflow_spot_create_req_t &value)
{
    json = {{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, order_workflow_spot_create_req_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
}

class order_workflow_spot_t : public spot_t
{
  public:
    explicit order_workflow_spot_t (sample_topology_t topology) :
        _store (std::move (topology))
    {
    }

    void configure (spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_handler<&order_workflow_spot_t::start> (start_order_workflow_req_t::packet_name)
          .add_handler<&order_workflow_spot_t::continue_> (
            continue_order_workflow_req_t::packet_name)
          .add_handler<&order_workflow_spot_t::continue_scheduled> (
            continue_order_workflow_msg_t::packet_name)
          .add_handler<&order_workflow_spot_t::rebuild> (
            rebuild_order_projection_req_t::packet_name);
    }

    spot_create_response_t on_create (const zlink::framework::message_t &request)
    {
        auto create = request.decode<order_workflow_spot_create_req_t> ();
        _order_id = create.order_id;
        return spot_create_response_t::accept ();
    }

    void on_closing () { _continue_timer.cancel (); }

    /* 공통 sample spec §9.3: 시작은 루프를 Created까지만 돌리고 즉시 응답하며, 나머지 단계를
     * 진행할 재개 호출을 기다리지 않고 예약한다. 결제 지연을 HTTP 응답에 묶지 않기 위해서다. */
    start_order_workflow_res_t start (const start_order_workflow_req_t &request)
    {
        auto state = _store.update ([&] (nlohmann::json &json) {
            return run_workflow (json, request.order_id, source_command_id (request), &request,
                                 /*max_steps=*/1);
        });
        std::cerr << "shoppingmall order: started order=" << state.order_id
                  << " status=" << state.status << " spot=" << _order_id << "\n";
        if (state.status != order_status_t::confirmed && state.status != order_status_t::failed) {
            schedule_continue (state.order_id);
        }
        return {state};
    }

    /* 재개는 다음 단계가 없을 때까지 같은 루프를 돌린다. 시작이 예약한 호출이든, 복구용 외부
     * 호출이든 코드는 같다. */
    continue_order_workflow_res_t continue_ (const continue_order_workflow_req_t &request)
    {
        return {run_to_completion (request.order_id)};
    }

    void continue_scheduled (const continue_order_workflow_msg_t &message)
    {
        (void) run_to_completion (message.order_id);
    }

    rebuild_order_projection_res_t rebuild (const rebuild_order_projection_req_t &request)
    {
        auto state = _store.update ([&] (nlohmann::json &json) {
            return rebuild_projection (json, request.order_id);
        });
        std::cerr << "shoppingmall order: projection rebuilt order=" << state.order_id
                  << " status=" << state.status << "\n";
        return {state};
    }

    void run_scheduled_continue ()
    {
        _continue_timer.cancel ();
        auto order_id = std::move (_scheduled_order_id);
        _scheduled_order_id.clear ();
        if (!order_id.empty ()) {
            continue_scheduled (continue_order_workflow_msg_t{std::move (order_id)});
        }
    }

  private:
    static std::string source_command_id (const start_order_workflow_req_t &request)
    {
        /* 같은 IdempotencyKey의 시작 요청은 같은 SourceCommandId를 만든다(§9.4). */
        return "start:" + request.idempotency_key;
    }

    order_state_t run_to_completion (const std::string &order_id)
    {
        return _store.update ([&] (nlohmann::json &json) {
            return run_workflow (json, order_id, /*source_command_id=*/{}, nullptr,
                                 /*max_steps=*/16);
        });
    }

    void schedule_continue (const std::string &order_id)
    {
        _continue_timer.cancel ();
        _scheduled_order_id = order_id;
        _continue_timer = _context.add_timer<order_workflow_continue_timer_handler_t> (
          "order-workflow-continue", std::chrono::milliseconds (1));
    }

    redis_state_store_t _store;
    spot_context_t _context;
    zlink::framework::timer_t _continue_timer;
    std::string _scheduled_order_id;
    std::string _order_id;
};

void order_workflow_continue_timer_handler_t::handle (order_workflow_spot_t &spot,
                                                       const timer_tick_t &) const
{
    spot.run_scheduled_continue ();
}

class workflow_route_handlers_t
{
  public:
    using dependency_types = dependency_list_t<spot_node_manager_t>;

    explicit workflow_route_handlers_t (spot_node_manager_t &spots) :
        _spots (spots)
    {
    }

    ok_res_t handle (const ensure_order_workflow_spot_req_t &request,
                     const route_handler_context_t &)
    {
        ensure_spot (request.order_id);
        return {};
    }

  private:
    static spot_rid_t spot_rid_for (const std::string &order_id)
    {
        return spot_rid_t::from_string (order_id);
    }

    void ensure_spot (const std::string &order_id)
    {
        (void) _spots.get_or_create_spot (
          sample_names_t::order_workflow_spot,
          spot_rid_for (order_id),
          order_workflow_spot_create_req_t{order_id});
    }

    spot_node_manager_t &_spots;
};

} // namespace zlink::samples::shoppingmall

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::shoppingmall;

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    auto instance = topology.for_workflow_instance (configuration.role.name);
    redis_state_store_t store{topology};
    store.seed_defaults ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.services ()
          .add_singleton<sample_topology_t> (std::make_unique<sample_topology_t> (topology))
          .add_singleton<workflow_instance_topology_t> (
            std::make_unique<workflow_instance_topology_t> (instance))
          .add_singleton<redis_state_store_t, sample_topology_t> ()
          .add_singleton<workflow_route_handlers_t, spot_node_manager_t> ();
        add_shoppingmall_location_store (options, topology);
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label (instance.instance_id);
        const auto workflow_channel = order_workflow_channel_for (instance.instance_id);
        auto workflow_route = options.add_route_mesh (workflow_channel);
        workflow_route.listen (instance.route_endpoint)
          .set_routing_id (instance.route_rid)
          .channel_name (workflow_channel);
        workflow_route
          .add_route_request_handler<workflow_route_handlers_t,
                                      ensure_order_workflow_spot_req_t,
                                      ok_res_t> (
            ensure_order_workflow_spot_req_t::packet_name);
        auto spot_route = options.add_route_mesh (sample_names_t::order_spot_route);
        spot_route.listen (instance.spot_route_endpoint)
          .set_routing_id (instance.spot_rid)
          .channel_name (sample_names_t::order_spot_route);
        auto order_spot = options.add_route_mesh (sample_names_t::order_spot_discovery);
        order_spot.channel_name (sample_names_t::order_spot_route);
        order_spot.listen (instance.spot_router_endpoint)
          .set_routing_id (instance.spot_rid)
          .add_spot<order_workflow_spot_t> (
            sample_names_t::order_workflow_spot,
            [topology] { return std::make_shared<order_workflow_spot_t> (topology); });
        options.http ()
          .listen (instance.http_url)
          .map_health ("/health");
    });
    return app.run (argc, argv);
}
