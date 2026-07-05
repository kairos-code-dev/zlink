/* SPDX-License-Identifier: MPL-2.0 */

#include "../Common/workflow_logic.hpp"
#include "../Configuration/location_store.hpp"

#include <zlink/framework.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace zlink::samples::shoppingmall
{
using namespace zlink::framework;

inline std::string read_option (int argc, char **argv, const std::string &name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) return argv[i + 1];
    }
    return "";
}

inline void write_workflow_flow (const std::string &instance, const std::string &line)
{
    std::filesystem::create_directories (shoppingmall_log_dir ());
    std::ofstream output (shoppingmall_log_dir () + "/flow-" + instance + ".log", std::ios::app);
    output << "message flow role=" << instance << " " << line << "\n";
}

class workflow_handlers_t
{
  public:
    explicit workflow_handlers_t (file_state_store_t &store) : _store (store) {}

    start_order_workflow_res_t start (const start_order_workflow_req_t &request)
    {
        auto state = _store.update ([&] (nlohmann::json &json) {
            return start_workflow (json, request);
        });
        std::cerr << "shoppingmall order: started order=" << state.order_id
                  << " status=" << state.status << "\n";
        return {state};
    }

    start_order_workflow_res_t start_route (const start_order_workflow_req_t &request,
                                            const route_handler_context_t &)
    {
        try {
            return start (request);
        }
        catch (const std::exception &error) {
            std::cerr << "shoppingmall workflow handler failed: packet=StartOrderWorkflowReq"
                      << " order=" << request.order_id << " error=" << error.what () << "\n";
            throw;
        }
    }

    continue_order_workflow_res_t continue_ (const continue_order_workflow_req_t &request)
    {
        return {_store.update ([&] (nlohmann::json &json) {
            return continue_workflow (json, request.order_id);
        })};
    }

    continue_order_workflow_res_t continue_route (const continue_order_workflow_req_t &request,
                                                  const route_handler_context_t &)
    {
        try {
            return continue_ (request);
        }
        catch (const std::exception &error) {
            std::cerr << "shoppingmall workflow handler failed: packet=ContinueOrderWorkflowReq"
                      << " order=" << request.order_id << " error=" << error.what () << "\n";
            throw;
        }
    }

    start_order_workflow_res_t prepare (const start_order_workflow_req_t &request)
    {
        return {_store.update ([&] (nlohmann::json &json) {
            auto state = start_workflow (json, request);
            if (state.status == order_status_t::created) {
                append_event (json, request.order_id, "InventoryReservedEvent");
                state.status = order_status_t::inventory_reserved;
                state.reservation_id = "reservation-" + request.order_id;
                state = save_projection (json, state);
            }
            return state;
        })};
    }

    rebuild_order_projection_res_t rebuild (const rebuild_order_projection_req_t &request)
    {
        return {_store.update ([&] (nlohmann::json &json) {
            return rebuild_projection (json, request.order_id);
        })};
    }

    rebuild_order_projection_res_t rebuild_route (const rebuild_order_projection_req_t &request,
                                                  const route_handler_context_t &)
    {
        rebuild_order_projection_res_t response;
        try {
            response = rebuild (request);
        }
        catch (const std::exception &error) {
            std::cerr << "shoppingmall workflow handler failed: packet=RebuildOrderProjectionReq"
                      << " order=" << request.order_id << " error=" << error.what () << "\n";
            throw;
        }
        std::cerr << "shoppingmall order: projection rebuilt order=" << response.state.order_id
                  << " status=" << response.state.status << "\n";
        return response;
    }

  private:
    file_state_store_t &_store;
};

#define SHOPPINGMALL_WORKFLOW_HANDLER(name, req, res, method) \
class name { public: using request_type = req; using reply_type = res; \
using dependency_types = dependency_list_t<workflow_handlers_t>; static constexpr const char *topic_name = #req; \
explicit name (workflow_handlers_t &h): _h(h) {} reply_type handle (const request_type &r) { return _h.method(r); } \
private: workflow_handlers_t &_h; };

SHOPPINGMALL_WORKFLOW_HANDLER (workflow_start_handler_t, start_order_workflow_req_t, start_order_workflow_res_t, start)
SHOPPINGMALL_WORKFLOW_HANDLER (workflow_continue_handler_t, continue_order_workflow_req_t, continue_order_workflow_res_t, continue_)
SHOPPINGMALL_WORKFLOW_HANDLER (workflow_prepare_handler_t, start_order_workflow_req_t, start_order_workflow_res_t, prepare)
SHOPPINGMALL_WORKFLOW_HANDLER (workflow_rebuild_handler_t, rebuild_order_projection_req_t, rebuild_order_projection_res_t, rebuild)

} // namespace zlink::samples::shoppingmall

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::shoppingmall;

    const sample_topology_t topology;
    auto instance_id = read_option (argc, argv, "--instance");
    if (instance_id.empty ())
        instance_id = env_or ("SHOPPINGMALL_WORKFLOW_INSTANCE", "workflow-a");
    auto instance = topology.for_workflow_instance (instance_id);
    file_state_store_t store;
    store.seed_defaults ();
    write_workflow_flow (instance.instance_id,
                         "action=location-store-claim redis=" + topology.redis_endpoint
                           + " prefix=" + topology.redis_key_prefix);

    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.services ()
          .add_singleton<file_state_store_t> ()
          .add_singleton<workflow_handlers_t, file_state_store_t> ();
        add_shoppingmall_location_store (options, topology);
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (shoppingmall_log_dir () + "/flow-" + instance.instance_id + ".log")
          .trace_label (instance.instance_id);
        options.add_route_mesh_channel (sample_names_t::order_workflow_route_channel)
          .enable_server (instance.route_endpoint)
          .set_routing_id (instance.route_rid)
          .add_request_handler<workflow_handlers_t,
                               start_order_workflow_req_t,
                               start_order_workflow_res_t> (
            "StartOrderWorkflowReq", &workflow_handlers_t::start_route)
          .add_request_handler<workflow_handlers_t,
                               continue_order_workflow_req_t,
                               continue_order_workflow_res_t> (
            "ContinueOrderWorkflowReq", &workflow_handlers_t::continue_route)
          .add_request_handler<workflow_handlers_t,
                               rebuild_order_projection_req_t,
                               rebuild_order_projection_res_t> (
            "RebuildOrderProjectionReq", &workflow_handlers_t::rebuild_route);
        options.add_spot_mesh (sample_names_t::order_spot_discovery)
          .enable_router (instance.spot_router_endpoint)
          .set_routing_id (instance.spot_rid)
          .enable_pub_sub (instance.spot_endpoint);
        options.http ()
          .listen (instance.http_url)
          .map_health ("/health")
          .map_post<workflow_start_handler_t> ("/workflow/start")
          .map_post<workflow_continue_handler_t> ("/workflow/continue")
          .map_post<workflow_prepare_handler_t> ("/workflow/prepare")
          .map_post<workflow_rebuild_handler_t> ("/workflow/rebuild");
    });
    return app.run (argc, argv);
}
