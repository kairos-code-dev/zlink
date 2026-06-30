/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Shared/shopping_mall_state.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::shoppingmall
{

using namespace framework;

class start_order_workflow_route_handler_t
{
  public:
    using dependency_types = dependency_list_t<shopping_mall_state_t>;

    explicit start_order_workflow_route_handler_t (shopping_mall_state_t &state) :
        _state (state)
    {}

    start_order_workflow_res_t handle (const start_order_workflow_req_t &request,
                                       const route_handler_context_t &)
    {
        return _state.start_workflow (request);
    }

  private:
    shopping_mall_state_t &_state;
};

class continue_order_workflow_route_handler_t
{
  public:
    using dependency_types = dependency_list_t<shopping_mall_state_t>;

    explicit continue_order_workflow_route_handler_t (shopping_mall_state_t &state) :
        _state (state)
    {}

    continue_order_workflow_res_t handle (const continue_order_workflow_req_t &request,
                                          const route_handler_context_t &)
    {
        return _state.continue_workflow (request);
    }

  private:
    shopping_mall_state_t &_state;
};

class get_order_route_handler_t
{
  public:
    using dependency_types = dependency_list_t<shopping_mall_state_t>;

    explicit get_order_route_handler_t (shopping_mall_state_t &state) : _state (state) {}

    get_order_state_res_t handle (const get_order_state_req_t &request,
                                  const route_handler_context_t &)
    {
        return _state.get_order (request.order_id);
    }

  private:
    shopping_mall_state_t &_state;
};

class delete_projection_route_handler_t
{
  public:
    using dependency_types = dependency_list_t<shopping_mall_state_t>;

    explicit delete_projection_route_handler_t (shopping_mall_state_t &state) :
        _state (state)
    {}

    get_order_state_res_t handle (const delete_order_projection_req_t &request,
                                  const route_handler_context_t &)
    {
        return _state.delete_projection (request.order_id);
    }

  private:
    shopping_mall_state_t &_state;
};

class rebuild_projection_route_handler_t
{
  public:
    using dependency_types = dependency_list_t<shopping_mall_state_t>;

    explicit rebuild_projection_route_handler_t (shopping_mall_state_t &state) :
        _state (state)
    {}

    rebuild_order_projection_res_t handle (const rebuild_order_projection_req_t &request,
                                           const route_handler_context_t &)
    {
        return _state.rebuild_projection (request.order_id);
    }

  private:
    shopping_mall_state_t &_state;
};

} // namespace zlink::samples::shoppingmall

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::shoppingmall;

    const sample_topology_t topology;
    const auto instance = shoppingmall_env_or ("SHOPPINGMALL_WORKFLOW_INSTANCE", "workflow-a");
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (shoppingmall_log_dir () + "/flow-" + instance + ".log")
          .trace_label ("shoppingmall-" + instance);
        options.services ().add_singleton<shopping_mall_state_t> ();
        options.services ()
          .add_transient<start_order_workflow_route_handler_t, shopping_mall_state_t> ()
          .add_transient<continue_order_workflow_route_handler_t, shopping_mall_state_t> ()
          .add_transient<get_order_route_handler_t, shopping_mall_state_t> ()
          .add_transient<delete_projection_route_handler_t, shopping_mall_state_t> ()
          .add_transient<rebuild_projection_route_handler_t, shopping_mall_state_t> ();
        add_shoppingmall_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router);
        options.add_route_mesh (sample_names_t::workflow_route_channel)
          .enable_server (topology.route_endpoint_for_workflow (instance))
          .set_routing_id (
            zlink::routing_id_t::from (topology.route_rid_for_workflow (instance)))
          .enable_client ()
          .add_request_handler<start_order_workflow_route_handler_t, start_order_workflow_req_t,
                               start_order_workflow_res_t> (
            start_order_workflow_req_t::packet_name, &start_order_workflow_route_handler_t::handle)
          .add_request_handler<continue_order_workflow_route_handler_t,
                               continue_order_workflow_req_t,
                               continue_order_workflow_res_t> (
            continue_order_workflow_req_t::packet_name,
            &continue_order_workflow_route_handler_t::handle)
          .add_request_handler<get_order_route_handler_t, get_order_state_req_t,
                               get_order_state_res_t> (get_order_state_req_t::packet_name,
                                                       &get_order_route_handler_t::handle)
          .add_request_handler<delete_projection_route_handler_t,
                               delete_order_projection_req_t, get_order_state_res_t> (
            delete_order_projection_req_t::packet_name,
            &delete_projection_route_handler_t::handle)
          .add_request_handler<rebuild_projection_route_handler_t,
                               rebuild_order_projection_req_t,
                               rebuild_order_projection_res_t> (
            rebuild_order_projection_req_t::packet_name,
            &rebuild_projection_route_handler_t::handle);
        options.http ()
          .listen (instance == "workflow-b" ? topology.workflow_b_http : topology.workflow_a_http)
          .map_health ("/health");
    });
    return app.run (argc, argv);
}
