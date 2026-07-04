/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

struct courier_binding_t
{
    std::string courier_id;
    actor_ref_snapshot_t actor;
    std::string session_route;
};

class courier_directory_t
{
  public:
    std::string choose_node (const std::string &courier_id) const
    {
        return sample_names_t::courier_actor_node (courier_id);
    }

    courier_binding_t remember (const ensure_courier_actor_res_t &ensured,
                                const std::string &session_route)
    {
        courier_binding_t binding{ensured.courier_id, ensured.actor, session_route};
        const std::lock_guard lock (_mutex);
        _bindings[ensured.courier_id] = binding;
        return binding;
    }

    courier_binding_t require (const std::string &courier_id) const
    {
        const std::lock_guard lock (_mutex);
        const auto found = _bindings.find (courier_id);
        if (found == _bindings.end ()) {
            throw std::runtime_error ("courier actor is not bound: " + courier_id);
        }
        return found->second;
    }

  private:
    mutable std::mutex _mutex;
    std::map<std::string, courier_binding_t> _bindings;
};

class bind_courier_handler_t
{
  public:
    using dependency_types = dependency_list_t<courier_directory_t, route_client_t>;
    using request_type = bind_courier_req_t;
    using reply_type = bind_courier_res_t;
    static constexpr const char *topic_name = bind_courier_req_t::packet_name;

    bind_courier_handler_t (courier_directory_t &directory, route_client_t &routes) :
        _directory (directory), _routes (routes)
    {
    }

    task_t<bind_courier_res_t> handle (const bind_courier_req_t &request)
    {
        const auto node = _directory.choose_node (request.courier_id);
        auto ensured =
          co_await _routes.request_to_node (sample_names_t::courier_actor_node_route_channel,
                                    zlink::routing_id_t::from (node),
                                    ensure_courier_actor_req_t{request.courier_id})
            .packet_name (ensure_courier_actor_req_t::packet_name)
            .template async<ensure_courier_actor_res_t> ();
        auto binding = _directory.remember (ensured, request.session_route);
        co_return bind_courier_res_t{binding.courier_id, binding.actor, binding.session_route};
    }

  private:
    courier_directory_t &_directory;
    route_client_t &_routes;
};

class gateway_offer_delivery_handler_t
{
  public:
    using dependency_types = dependency_list_t<courier_directory_t, route_client_t>;
    using request_type = offer_delivery_req_t;
    using reply_type = offer_delivery_res_t;
    static constexpr const char *topic_name = offer_delivery_req_t::packet_name;

    gateway_offer_delivery_handler_t (courier_directory_t &directory, route_client_t &routes) :
        _directory (directory), _routes (routes)
    {
    }

    task_t<offer_delivery_res_t> handle (const offer_delivery_req_t &request)
    {
        auto binding = _directory.require (request.courier_id);
        auto reply =
          co_await _routes.request_to_node (sample_names_t::courier_actor_node_route_channel,
                                    zlink::routing_id_t::from (
                                      std::string (binding.actor.node_rid.value ())), request)
            .packet_name (offer_delivery_req_t::packet_name)
            .template async<offer_delivery_res_t> ();
        co_return reply;
    }

  private:
    courier_directory_t &_directory;
    route_client_t &_routes;
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
          .trace_log_file (deliverydispatch_log_dir () + "/flow-courier-gateway.log")
          .trace_label ("deliverydispatch-courier-gateway");
        options.services ().add_singleton<courier_directory_t> ();
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.add_client_server_channel (sample_names_t::courier_route_channel)
          .enable_server (topology.courier_route_endpoint)
          .use_handler_group ("courier-gateway");
        options.add_route_mesh_channel (sample_names_t::courier_actor_node_route_channel)
          .enable_client (topology.courier_actor_node_1_route_endpoint)
          .enable_client (topology.courier_actor_node_2_route_endpoint)
          .set_routing_id (zlink::routing_id_t::from ("delivery-courier-gateway"));
        options.handlers ()
          .group ("courier-gateway")
          .add<bind_courier_handler_t> ()
          .add<gateway_offer_delivery_handler_t> ();
    });
    return app.run (argc, argv);
}
