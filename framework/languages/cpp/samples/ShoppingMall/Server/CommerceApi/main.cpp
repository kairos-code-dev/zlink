/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Common/store.hpp"
#include "../Configuration/location_store.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

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

class commerce_api_handlers_t
{
  public:
    commerce_api_handlers_t (sample_topology_t &topology,
                             api_instance_topology_t &instance,
                             route_client_t &routes,
                             redis_state_store_t &store,
                             spot_handle_resolver_t &spot_handles) :
        _topology (topology), _instance (instance), _routes (routes), _store (store),
        _spot_handles (spot_handles)
    {
    }

    /* 공통 sample spec §16: CommerceApi는 HTTP API·검증·멱등 키 조회·조회 모델 조회만 맡고
     * 도메인 이벤트를 기록하지 않는다. 새 주문의 응답은 `Created`이며, 나머지 단계는 owner가
     * 배경에서 진행한다(§9.3). 클라이언트는 GetOrderState 폴링으로 종료를 확인한다. */
    task_t<start_order_res_t> start_order (const start_order_req_t &request)
    {
        auto command = _store.update ([&] (nlohmann::json &state) {
            auto &mappings = state["idempotency"];
            if (!mappings.contains (request.idempotency_key)) {
                const auto next = state.value ("nextOrderSequence", 0) + 1;
                state["nextOrderSequence"] = next;
                mappings[request.idempotency_key] = nlohmann::json{
                  {"orderId", "order-" + std::string (4 - std::to_string (next).size (), '0')
                                + std::to_string (next)},
                  {"ownerInstanceId", _instance.instance_id},
                  {"started", false}};
            }
            const auto order_id =
              mappings[request.idempotency_key].value ("orderId", std::string{});

            /* 장바구니는 CommerceStateStore의 시드에서 읽어 검증한다 — 금액 범위나 cart id
             * 문자열 비교로 성공·실패를 흉내내지 않는다. */
            if (!state["carts"].contains (request.cart_id)) {
                throw std::runtime_error ("Unknown cart: " + request.cart_id);
            }
            const auto cart = state["carts"][request.cart_id].get<cart_seed_t> ();
            mappings[request.idempotency_key]["started"] = true;
            return start_order_workflow_req_t{order_id,
                                              request.cart_id,
                                              request.shipping_address_id,
                                              request.payment_method_id,
                                              request.idempotency_key,
                                              cart.lines,
                                              cart.amount,
                                              cart.currency};
        });

        const auto owner = _topology.for_order_id (command.order_id);
        auto state =
          (co_await request_workflow<start_order_workflow_res_t> (owner.instance_id, command)).state;
        /* 나머지 단계를 진행할 재개 호출을 기다리지 않고 예약한다(§9.3). 결제 지연이 HTTP 응답
         * 지연이 되지 않도록, 응답은 여기서 바로 돌려주고 진행은 배경에서 이어진다. */
        if (state.status != order_status_t::confirmed && state.status != order_status_t::failed) {
            co_await schedule_continue (state.order_id);
        }
        std::cerr << "shoppingmall api: start order=" << state.order_id
                  << " status=" << state.status << "\n";
        co_return start_order_res_t{state.order_id, state.status};
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

    /* self-check hook(§15 "죽은 뒤 재개"): 배경 재개를 InventoryReserved에서 멈춰 중간 상태를
     * 만든다. API가 이벤트 스트림을 잘라내는 게 아니라, owner의 루프가 이 hook을 보고 멈춘다. */
    task_t<start_order_res_t> prepare_inventory_reserved (const start_order_req_t &request)
    {
        const auto order_id = _store.update ([&] (nlohmann::json &state) {
            auto &mappings = state["idempotency"];
            if (!mappings.contains (request.idempotency_key)) {
                const auto next = state.value ("nextOrderSequence", 0) + 1;
                state["nextOrderSequence"] = next;
                mappings[request.idempotency_key] = nlohmann::json{
                  {"orderId", "order-" + std::string (4 - std::to_string (next).size (), '0')
                                + std::to_string (next)},
                  {"ownerInstanceId", _instance.instance_id},
                  {"started", false}};
            }
            const auto id = mappings[request.idempotency_key].value ("orderId", std::string{});
            state["testHooks"]["stopAt"][id] = order_status_t::inventory_reserved;
            return id;
        });
        (void) order_id;

        auto response = co_await start_order (request);
        auto state = co_await wait_for_status (response.order_id,
                                               order_status_t::inventory_reserved);
        _store.update ([&] (nlohmann::json &saved) {
            saved["testHooks"]["stopAt"].erase (response.order_id);
            return true;
        });
        co_return start_order_res_t{response.order_id, state.status};
    }

    task_t<continue_order_workflow_res_t> continue_order (const continue_order_workflow_req_t &request)
    {
        const auto owner = _topology.for_order_id (request.order_id);
        co_return co_await request_workflow<continue_order_workflow_res_t> (owner.instance_id,
                                                                            request);
    }

    ok_res_t delete_projection (const delete_projection_req_t &request)
    {
        _store.update ([&] (nlohmann::json &state) {
            state["readModels"].erase (request.order_id);
            return true;
        });
        return {};
    }

    task_t<rebuild_order_projection_res_t>
    rebuild_projection_req (const rebuild_order_projection_req_t &request)
    {
        const auto owner = _topology.for_order_id (request.order_id);
        co_return co_await request_workflow<rebuild_order_projection_res_t> (owner.instance_id,
                                                                             request);
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
    /* 조회 모델을 폴링해 원하는 상태에 도달할 때까지 기다린다(self-check hook 전용). */
    task_t<order_state_t> wait_for_status (const std::string &order_id, const std::string &status)
    {
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto current = _store.read ([&] (const nlohmann::json &state) {
                return state["readModels"].contains (order_id)
                         ? state["readModels"][order_id].get<order_state_t> ()
                         : order_state_t{};
            });
            if (current.status == status) {
                co_return current;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
        throw framework_exception_t (framework_error_kind_t::request_failed,
                                     "Order '" + order_id + "' did not reach status " + status);
    }

    task_t<void> schedule_continue (const std::string &order_id)
    {
        auto target =
          co_await _spot_handles.resolve_spot_handle (spot_rid_t::from_string (order_id));
        if (!target) {
            throw framework_exception_t (framework_error_kind_t::spot_route_not_found,
                                         "Order workflow spot '" + order_id
                                           + "' has no live location row");
        }
        _routes.send_to_spot (*target, continue_order_workflow_msg_t{order_id}).submit ();
        co_return;
    }

    template <typename TReply, typename TRequest>
    task_t<TReply> request_workflow (const std::string &owner_instance_id, const TRequest &request)
    {
        const auto owner = _topology.for_workflow_instance (owner_instance_id);
        auto ensured =
          co_await _routes
            .request_to_node (order_workflow_channel_for (owner.instance_id),
                              owner.route_rid,
                              ensure_order_workflow_spot_req_t{request.order_id})
            .timeout (std::chrono::milliseconds (5000))
            .template async<ok_res_t> ();
        if (!ensured.ok) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "ShoppingMall workflow spot ensure failed");
        }

        auto target = co_await _spot_handles.resolve_spot_handle (
          spot_rid_t::from_string (request.order_id));
        if (!target) {
            throw framework_exception_t (framework_error_kind_t::spot_route_not_found,
                                         "Order workflow spot '" + request.order_id
                                           + "' has no live location row");
        }
        co_return co_await _routes.request_to_spot (*target, request)
          .timeout (std::chrono::milliseconds (5000))
          .template async<TReply> ();
    }

    sample_topology_t &_topology;
    api_instance_topology_t &_instance;
    route_client_t &_routes;
    redis_state_store_t &_store;
    spot_handle_resolver_t &_spot_handles;
};

/* 공통 sample spec: 샘플 handler는 framework가 처리하는 dispatch 오류를 다시 잡아
 * 로그만 남기고 되던지지 않는다. 실패는 dispatch 경계(error reply/observer/기본 로그)가
 * 소유한다. */
#define SHOPPINGMALL_HANDLER(name, req, res, method)                                               \
    class name                                                                                     \
    {                                                                                              \
      public:                                                                                      \
        using request_type = req;                                                                  \
        using reply_type = res;                                                                    \
        using dependency_types = dependency_list_t<commerce_api_handlers_t>;                       \
        static constexpr const char *topic_name = req::packet_name;                                \
        explicit name (commerce_api_handlers_t &handlers) : _handlers (handlers) {}                \
        auto handle (const request_type &request) { return _handlers.method (request); }           \
                                                                                                   \
      private:                                                                                     \
        commerce_api_handlers_t &_handlers;                                                        \
    };

SHOPPINGMALL_HANDLER (start_order_handler_t, start_order_req_t, start_order_res_t, start_order)
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
    redis_state_store_t store{topology};
    store.seed_defaults ();
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (topology));
        options.services ().add_singleton<api_instance_topology_t> (
          std::make_unique<api_instance_topology_t> (instance));
        options.services ()
          .add_singleton<redis_state_store_t, sample_topology_t> ()
          .add_singleton<commerce_api_handlers_t,
                         sample_topology_t,
                         api_instance_topology_t,
                         route_client_t,
                         redis_state_store_t,
                         spot_handle_resolver_t> ();
        add_shoppingmall_location_store (options, topology);
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (shoppingmall_log_dir () + "/flow-" + instance.instance_id + ".log")
          .trace_label (instance.instance_id);
        options.add_route_mesh (order_workflow_channel_for ("workflow-a"))
          .enable_client (topology.workflow_a_route_endpoint);
        options.add_route_mesh (order_workflow_channel_for ("workflow-b"))
          .enable_client (topology.workflow_b_route_endpoint);
        auto order_spot_route = options.add_route_mesh (sample_names_t::order_spot_route);
        order_spot_route.enable_client (topology.workflow_a_spot_route_endpoint);
        order_spot_route.enable_client (topology.workflow_b_spot_route_endpoint);
        options.configure_locations ().spot_router_channels[sample_names_t::order_spot_discovery] =
          sample_names_t::order_spot_route;
        options.add_spot_mesh (std::string (sample_names_t::order_spot_discovery) + "."
                               + instance.instance_id)
          .set_routing_id (instance.spot_rid)
          .enable_router (instance.spot_router_endpoint)
          .connect_router (topology.for_workflow_instance ("workflow-a").spot_rid,
                           topology.workflow_a_spot_router_endpoint)
          .connect_router (topology.for_workflow_instance ("workflow-b").spot_rid,
                           topology.workflow_b_spot_router_endpoint)
          .accept_route_mesh (sample_names_t::order_spot_route);
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
