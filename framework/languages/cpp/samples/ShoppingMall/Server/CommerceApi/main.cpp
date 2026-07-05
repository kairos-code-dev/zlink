/* SPDX-License-Identifier: MPL-2.0 */

#include "../Common/store.hpp"
#include "../Configuration/location_store.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

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

inline void write_flow (const std::string &instance, const std::string &line)
{
    std::filesystem::create_directories (shoppingmall_log_dir ());
    std::ofstream output (shoppingmall_log_dir () + "/flow-" + instance + ".log", std::ios::app);
    output << "message flow role=" << instance << " " << line << "\n";
}

class commerce_api_handlers_t
{
  public:
    commerce_api_handlers_t (sample_topology_t &topology,
                             api_instance_topology_t &instance,
                             route_client_t &routes,
                             file_state_store_t &store) :
        _topology (topology), _instance (instance), _routes (routes), _store (store)
    {
    }

    start_order_res_t start_order (const start_order_req_t &request)
    {
        auto command = _store.update ([&] (nlohmann::json &state) {
            auto &mappings = state["idempotency"];
            if (mappings.contains (request.idempotency_key)
                && mappings[request.idempotency_key].value ("started", false)) {
                const auto order_id =
                  mappings[request.idempotency_key].value ("orderId", std::string{});
                const auto current = state["readModels"][order_id].get<order_state_t> ();
                return start_order_workflow_req_t{current.order_id,
                                                  request.cart_id,
                                                  request.shipping_address_id,
                                                  request.payment_method_id,
                                                  request.idempotency_key,
                                                  {},
                                                  current.amount,
                                                  current.currency};
            }
            if (!mappings.contains (request.idempotency_key)) {
                const auto next = state.value ("nextOrderSequence", 0) + 1;
                state["nextOrderSequence"] = next;
                mappings[request.idempotency_key] =
                  nlohmann::json{{"orderId", "order-" + std::string (4 - std::to_string (next).size (), '0')
                                               + std::to_string (next)},
                                 {"ownerInstanceId", _instance.instance_id},
                                 {"started", false}};
            }
            const auto order_id =
              mappings[request.idempotency_key].value ("orderId", std::string{});
            state["orderPaymentMethods"][order_id] = request.payment_method_id;
            const auto inventory_fail = request.cart_id == "cart-inventory-fail";
            return start_order_workflow_req_t{
              order_id,
              request.cart_id,
              request.shipping_address_id,
              request.payment_method_id,
              request.idempotency_key,
              inventory_fail ? std::vector<order_line_input_t>{{"sku-rare", 1}}
                             : std::vector<order_line_input_t>{{"sku-ok", 1}},
              inventory_fail ? 1200.0 : 120.0,
              "USD"};
        });

        const auto owner = _topology.for_order_id (command.order_id);
        auto state = request_workflow<start_order_workflow_res_t> (owner.route_rid, command).state;
        (void) request_workflow<continue_order_workflow_res_t> (
          owner.route_rid, continue_order_workflow_req_t{state.order_id});
        state = _store.read ([&] (const nlohmann::json &saved) {
            return saved["readModels"][command.order_id].get<order_state_t> ();
        });
        write_flow (_instance.instance_id,
                    "action=start-order order=" + state.order_id + " owner=" + owner.instance_id);
        std::cerr << "shoppingmall api: start order=" << state.order_id
                  << " status=" << state.status << "\n";
        return {state.order_id, state.status};
    }

    get_order_state_res_t get_order (const get_order_state_req_t &request)
    {
        return _store.read ([&] (const nlohmann::json &state) {
            return get_order_state_res_t{state["readModels"][request.order_id].get<order_state_t> ()};
        });
    }

    ok_res_t create_pending (const pending_mapping_req_t &request)
    {
        _store.update ([&] (nlohmann::json &state) {
            state["idempotency"][request.idempotency_key] =
              nlohmann::json{{"orderId", request.order_id},
                             {"ownerInstanceId", request.owner_instance_id},
                             {"started", false}};
            return true;
        });
        return {};
    }

    start_order_res_t prepare_inventory_reserved (const start_order_req_t &request)
    {
        auto response = start_order (request);
        _store.update ([&] (nlohmann::json &state) {
            auto current = state["readModels"][response.order_id].get<order_state_t> ();
            current.status = order_status_t::inventory_reserved;
            current.reservation_id = "reservation-" + response.order_id;
            state["readModels"][response.order_id] = current;
            auto &events = state["events"][response.order_id];
            while (events.size () > 2) events.erase (events.end () - 1);
            return true;
        });
        return {response.order_id, order_status_t::inventory_reserved};
    }

    continue_order_workflow_res_t continue_order (const continue_order_workflow_req_t &request)
    {
        const auto owner = _topology.for_order_id (request.order_id);
        return request_workflow<continue_order_workflow_res_t> (owner.route_rid, request);
    }

    ok_res_t delete_projection (const delete_projection_req_t &request)
    {
        _store.update ([&] (nlohmann::json &state) {
            state["readModels"].erase (request.order_id);
            return true;
        });
        return {};
    }

    rebuild_order_projection_res_t rebuild_projection_req (const rebuild_order_projection_req_t &request)
    {
        const auto owner = _topology.for_order_id (request.order_id);
        return request_workflow<rebuild_order_projection_res_t> (owner.route_rid, request);
    }

    server_assertion_res_t assert_server (const server_assertion_req_t &request)
    {
        return _store.read ([&] (const nlohmann::json &state) {
            std::vector<std::string> evidence;
            for (const auto &order_id : {request.successful_order_id,
                                         request.pending_recovered_order_id,
                                         request.concurrent_order_id,
                                         request.resumed_order_id,
                                         request.inventory_failure_order_id,
                                         request.payment_failure_order_id,
                                         request.scale_out_order_id}) {
                const auto events = event_types_for (state, order_id);
                std::string line = order_id + ":";
                for (std::size_t i = 0; i < events.size (); ++i) {
                    if (i > 0) line += ">";
                    line += events[i];
                }
                evidence.push_back (line);
            }
            evidence.push_back ("paymentFailures=" + std::to_string (state["paymentAttempts"].size ()));
            evidence.push_back (
              "releasedReservations=" + std::to_string (state["releasedReservations"].size ()));
            evidence.push_back ("startedIdempotency=" + std::to_string (state["idempotency"].size ()));
            const auto owners_differ =
              _topology.for_order_id (request.successful_order_id).instance_id
              != _topology.for_order_id (request.scale_out_order_id).instance_id;
            evidence.push_back (
              "owners=" + _topology.for_order_id (request.successful_order_id).instance_id + ","
              + _topology.for_order_id (request.scale_out_order_id).instance_id);
            const auto success = std::vector<std::string>{"OrderStartedEvent",
                                                          "InventoryReservedEvent",
                                                          "PaymentAuthorizedEvent",
                                                          "OrderConfirmedEvent"};
            const auto passed =
              has_sequence (state, request.successful_order_id, success)
              && has_prefix (state, request.pending_recovered_order_id, success)
              && has_sequence (state, request.concurrent_order_id, success)
              && has_sequence (state, request.resumed_order_id, success)
              && has_sequence (state,
                               request.inventory_failure_order_id,
                               {"OrderStartedEvent",
                                "InventoryReservationFailedEvent",
                                "OrderFailedEvent"})
              && has_sequence (state,
                               request.payment_failure_order_id,
                               {"OrderStartedEvent",
                                "InventoryReservedEvent",
                                "PaymentFailedEvent",
                                "InventoryReleasedEvent",
                                "OrderFailedEvent"})
              && has_sequence (state, request.scale_out_order_id, success)
              && state["paymentAttempts"].size () >= 1 && state["releasedReservations"].size () >= 1
              && state["idempotency"].size () == 7 && owners_differ;
            std::cerr << "shoppingmall evidence: ";
            for (const auto &line : evidence) std::cerr << line << "; ";
            std::cerr << "\n";
            return server_assertion_res_t{passed, evidence};
        });
    }

  private:
    template <typename TReply, typename TRequest>
    TReply request_workflow (const routing_id_t &owner_route_rid, const TRequest &request)
    {
        auto reply = _routes
                       .request_to_node (sample_names_t::order_workflow_route_channel,
                                         owner_route_rid,
                                         request)
                       .timeout (std::chrono::milliseconds (5000))
                       .template async<TReply> ()
                       .result ();
        if (!reply) {
            throw framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "ShoppingMall workflow route failed");
        }
        return reply.value ();
    }

    sample_topology_t &_topology;
    api_instance_topology_t &_instance;
    route_client_t &_routes;
    file_state_store_t &_store;
};

class start_order_handler_t
{
  public:
    using request_type = start_order_req_t;
    using reply_type = start_order_res_t;
    using dependency_types = dependency_list_t<commerce_api_handlers_t>;
    static constexpr const char *topic_name = "StartOrderReq";
    explicit start_order_handler_t (commerce_api_handlers_t &handlers) : _handlers (handlers) {}
    reply_type handle (const request_type &request)
    {
        try {
            return _handlers.start_order (request);
        }
        catch (const std::exception &error) {
            std::cerr << "shoppingmall api handler failed: endpoint=/orders/start error="
                      << error.what () << "\n";
            throw;
        }
    }
  private:
    commerce_api_handlers_t &_handlers;
};

#define SHOPPINGMALL_HANDLER(name, req, res, method) \
class name { public: using request_type = req; using reply_type = res; \
using dependency_types = dependency_list_t<commerce_api_handlers_t>; \
static constexpr const char *topic_name = #req; explicit name (commerce_api_handlers_t &h): _h(h) {} \
reply_type handle (const request_type &r) { try { return _h.method (r); } \
catch (const std::exception &error) { std::cerr << "shoppingmall api handler failed: packet=" #req \
<< " error=" << error.what () << "\n"; throw; } } private: commerce_api_handlers_t &_h; };

SHOPPINGMALL_HANDLER (get_order_handler_t, get_order_state_req_t, get_order_state_res_t, get_order)
SHOPPINGMALL_HANDLER (pending_handler_t, pending_mapping_req_t, ok_res_t, create_pending)
SHOPPINGMALL_HANDLER (prepare_handler_t, start_order_req_t, start_order_res_t, prepare_inventory_reserved)
SHOPPINGMALL_HANDLER (continue_handler_t, continue_order_workflow_req_t, continue_order_workflow_res_t, continue_order)
SHOPPINGMALL_HANDLER (delete_projection_handler_t, delete_projection_req_t, ok_res_t, delete_projection)
SHOPPINGMALL_HANDLER (rebuild_projection_handler_t, rebuild_order_projection_req_t, rebuild_order_projection_res_t, rebuild_projection_req)
SHOPPINGMALL_HANDLER (assert_handler_t, server_assertion_req_t, server_assertion_res_t, assert_server)

} // namespace zlink::samples::shoppingmall

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::shoppingmall;

    const sample_topology_t topology;
    auto instance_id = read_option (argc, argv, "--instance");
    if (instance_id.empty ()) instance_id = env_or ("SHOPPINGMALL_INSTANCE", "api-a");
    auto instance = topology.for_api_instance (instance_id);
    file_state_store_t store;
    store.seed_defaults ();
    write_flow (instance.instance_id,
                "action=location-store-claim redis=" + topology.redis_endpoint
                  + " prefix=" + topology.redis_key_prefix);

    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (topology));
        options.services ().add_singleton<api_instance_topology_t> (
          std::make_unique<api_instance_topology_t> (instance));
        options.services ()
          .add_singleton<file_state_store_t> ()
          .add_singleton<commerce_api_handlers_t,
                         sample_topology_t,
                         api_instance_topology_t,
                         route_client_t,
                         file_state_store_t> ();
        add_shoppingmall_location_store (options, topology);
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (shoppingmall_log_dir () + "/flow-" + instance.instance_id + ".log")
          .trace_label (instance.instance_id);
        options.add_route_mesh_channel (sample_names_t::order_workflow_route_channel)
          .enable_server (instance.route_endpoint)
          .set_routing_id (instance.route_rid);
        options.http ()
          .listen (instance.http_url)
          .map_health ("/health")
          .map_post<start_order_handler_t> ("/orders/start")
          .map_post<get_order_handler_t> ("/orders/get")
          .map_post<pending_handler_t> ("/self-check/idempotency/pending")
          .map_post<prepare_handler_t> ("/self-check/workflow/inventory-reserved")
          .map_post<continue_handler_t> ("/self-check/workflow/continue")
          .map_post<delete_projection_handler_t> ("/self-check/projection/delete")
          .map_post<rebuild_projection_handler_t> ("/self-check/projection/rebuild")
          .map_post<assert_handler_t> ("/self-check/assert");
    });
    return app.run (argc, argv);
}
