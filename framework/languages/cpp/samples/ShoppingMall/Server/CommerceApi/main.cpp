/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Shared/shopping_mall_state.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <stdexcept>

namespace zlink::samples::shoppingmall
{

using namespace framework;

template <typename TReply, typename TRequest>
task_t<TReply> request_workflow (route_client_t &routes,
                                 const sample_topology_t &topology,
                                 const std::string &order_id,
                                 const TRequest &request)
{
    co_return co_await routes
      .request (sample_names_t::workflow_route_channel,
                zlink::routing_id_t::from (topology.workflow_rid_for_order (order_id)), request)
      .packet_name (TRequest::packet_name)
      .timeout (std::chrono::seconds (8))
      .template async<TReply> ();
}

class start_order_http_handler_t
{
  public:
    using request_type = start_order_req_t;
    using reply_type = start_order_res_t;
    using dependency_types = dependency_list_t<shopping_mall_state_t, route_client_t>;
    static constexpr const char *topic_name = "StartOrderReq";

    start_order_http_handler_t (shopping_mall_state_t &state, route_client_t &routes) :
        _state (state), _routes (routes)
    {}

    task_t<start_order_res_t> handle (const start_order_req_t &request)
    {
        const auto workflow_request = _state.prepare_start_order (request);
        const sample_topology_t topology;
        const auto workflow_response =
          co_await request_workflow<start_order_workflow_res_t> (
            _routes, topology, workflow_request.order_id, workflow_request);
        co_return start_order_res_t{workflow_response.state.order_id,
                                    workflow_response.state.status};
    }

  private:
    shopping_mall_state_t &_state;
    route_client_t &_routes;
};

class get_order_http_handler_t
{
  public:
    using request_type = get_order_state_req_t;
    using reply_type = get_order_state_res_t;
    using dependency_types = dependency_list_t<route_client_t>;
    static constexpr const char *topic_name = "GetOrderStateReq";

    explicit get_order_http_handler_t (route_client_t &routes) : _routes (routes) {}
    task_t<get_order_state_res_t> handle (const get_order_state_req_t &request)
    {
        const sample_topology_t topology;
        co_return co_await request_workflow<get_order_state_res_t> (_routes, topology,
                                                                    request.order_id, request);
    }

  private:
    route_client_t &_routes;
};

class seed_pending_http_handler_t
{
  public:
    using request_type = seed_pending_idempotency_req_t;
    using reply_type = start_order_res_t;
    using dependency_types = dependency_list_t<shopping_mall_state_t>;
    static constexpr const char *topic_name = "SeedPendingIdempotencyReq";

    explicit seed_pending_http_handler_t (shopping_mall_state_t &state) : _state (state) {}
    start_order_res_t handle (const seed_pending_idempotency_req_t &request)
    {
        return _state.seed_pending (request);
    }

  private:
    shopping_mall_state_t &_state;
};

class delete_projection_http_handler_t
{
  public:
    using request_type = delete_order_projection_req_t;
    using reply_type = get_order_state_res_t;
    using dependency_types = dependency_list_t<route_client_t>;
    static constexpr const char *topic_name = "DeleteOrderProjectionReq";

    explicit delete_projection_http_handler_t (route_client_t &routes) : _routes (routes) {}
    task_t<get_order_state_res_t> handle (const delete_order_projection_req_t &request)
    {
        const sample_topology_t topology;
        co_return co_await request_workflow<get_order_state_res_t> (_routes, topology,
                                                                    request.order_id, request);
    }

  private:
    route_client_t &_routes;
};

class rebuild_projection_http_handler_t
{
  public:
    using request_type = rebuild_order_projection_req_t;
    using reply_type = rebuild_order_projection_res_t;
    using dependency_types = dependency_list_t<route_client_t>;
    static constexpr const char *topic_name = "RebuildOrderProjectionReq";

    explicit rebuild_projection_http_handler_t (route_client_t &routes) : _routes (routes) {}
    task_t<rebuild_order_projection_res_t> handle (const rebuild_order_projection_req_t &request)
    {
        const sample_topology_t topology;
        co_return co_await request_workflow<rebuild_order_projection_res_t> (
          _routes, topology, request.order_id, request);
    }

  private:
    route_client_t &_routes;
};

class assert_http_handler_t
{
  public:
    using request_type = server_assertion_req_t;
    using reply_type = server_assertion_res_t;
    using dependency_types = dependency_list_t<route_client_t>;
    static constexpr const char *topic_name = "ServerAssertionReq";

    explicit assert_http_handler_t (route_client_t &routes) : _routes (routes) {}
    task_t<server_assertion_res_t> handle (const server_assertion_req_t &request)
    {
        const sample_topology_t topology;
        const auto success =
          co_await request_workflow<get_order_state_res_t> (
            _routes, topology, request.successful_order_id,
            get_order_state_req_t{request.successful_order_id});
        const auto pending =
          co_await request_workflow<get_order_state_res_t> (
            _routes, topology, request.pending_recovered_order_id,
            get_order_state_req_t{request.pending_recovered_order_id});
        const auto inventory =
          co_await request_workflow<get_order_state_res_t> (
            _routes, topology, request.inventory_failure_order_id,
            get_order_state_req_t{request.inventory_failure_order_id});
        const auto payment =
          co_await request_workflow<get_order_state_res_t> (
            _routes, topology, request.payment_failure_order_id,
            get_order_state_req_t{request.payment_failure_order_id});
        const auto scale =
          co_await request_workflow<get_order_state_res_t> (
            _routes, topology, request.scale_out_order_id,
            get_order_state_req_t{request.scale_out_order_id});
        const bool passed =
          success.state.status == order_status_t::confirmed &&
          pending.state.status == order_status_t::confirmed &&
          inventory.state.reason.find ("inventory") != std::string::npos &&
          payment.state.reason.find ("payment") != std::string::npos &&
          scale.state.status == order_status_t::confirmed;
        co_return server_assertion_res_t{
          passed,
          {success.state.order_id + ":" + success.state.status,
           pending.state.order_id + ":" + pending.state.status,
           inventory.state.order_id + ":" + inventory.state.reason,
           payment.state.order_id + ":" + payment.state.reason,
           scale.state.order_id + ":" + scale.state.status}};
    }

  private:
    route_client_t &_routes;
};

} // namespace zlink::samples::shoppingmall

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::shoppingmall;

    const sample_topology_t topology;
    const auto instance = shoppingmall_env_or ("SHOPPINGMALL_INSTANCE", "api-a");
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (shoppingmall_log_dir () + "/flow-" + instance + ".log")
          .trace_label ("shoppingmall-" + instance);
        options.services ().add_singleton<shopping_mall_state_t> ();
        add_shoppingmall_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router);
        options.add_route_mesh (sample_names_t::workflow_route_channel)
          .enable_server (topology.route_endpoint_for_api (instance))
          .set_routing_id (zlink::routing_id_t::from (topology.route_rid_for_api (instance)))
          .enable_client ();
        options.http ()
          .listen (instance == "api-b" ? topology.api_b_http : topology.api_a_http)
          .map_health ("/health")
          .map_post<start_order_http_handler_t> ("/orders/start")
          .map_post<get_order_http_handler_t> ("/orders/get")
          .map_post<seed_pending_http_handler_t> ("/self-check/idempotency/pending")
          .map_post<delete_projection_http_handler_t> ("/self-check/projection/delete")
          .map_post<rebuild_projection_http_handler_t> ("/self-check/projection/rebuild")
          .map_post<assert_http_handler_t> ("/self-check/assert");
    });
    return app.run (argc, argv);
}
