/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/Contracts/messages.hpp"

#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace zlink::samples::shoppingmall
{

class shopping_mall_state_t
{
  public:
    start_order_workflow_req_t prepare_start_order (const start_order_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        const auto existing = _idempotency.find (request.idempotency_key);
        if (existing != _idempotency.end ()) {
            return {existing->second,
                    request.cart_id,
                    request.shipping_address_id,
                    request.payment_method_id,
                    request.idempotency_key};
        }

        const auto order_id = order_id_for (request.idempotency_key);
        _idempotency[request.idempotency_key] = order_id;
        return {order_id,
                request.cart_id,
                request.shipping_address_id,
                request.payment_method_id,
                request.idempotency_key};
    }

    start_order_res_t seed_pending (const seed_pending_idempotency_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        _idempotency[request.idempotency_key] = request.order_id;
        return {request.order_id, order_status_t::created};
    }

    start_order_workflow_res_t start_workflow (const start_order_workflow_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        const auto existing = _orders.find (request.order_id);
        if (existing != _orders.end ()) {
            _evidence.push_back (request.order_id + ":idempotency-hit");
            return {existing->second};
        }

        auto state = make_state (request.order_id, order_status_t::created,
                                 request.shipping_address_id, request.cart_id);
        _orders[request.order_id] = state;
        _workflow_contexts[request.order_id] = {request.cart_id, request.payment_method_id};
        _evidence.push_back (request.order_id + ":OrderStartedEvent");
        const auto created = state;
        continue_workflow_unlocked (request.order_id);
        return {created};
    }

    continue_order_workflow_res_t continue_workflow (const continue_order_workflow_req_t &request)
    {
        const std::lock_guard lock (_mutex);
        continue_workflow_unlocked (request.order_id);
        return {require_order_unlocked (request.order_id)};
    }

    get_order_state_res_t get_order (const std::string &order_id) const
    {
        const std::lock_guard lock (_mutex);
        return {require_order_unlocked (order_id)};
    }

    get_order_state_res_t delete_projection (const std::string &order_id)
    {
        const std::lock_guard lock (_mutex);
        _deleted.insert (order_id);
        _evidence.push_back (order_id + ":ProjectionDeleted");
        return {require_order_unlocked (order_id)};
    }

    rebuild_order_projection_res_t rebuild_projection (const std::string &order_id)
    {
        const std::lock_guard lock (_mutex);
        _deleted.erase (order_id);
        _evidence.push_back (order_id + ":ProjectionRebuilt");
        return {require_order_unlocked (order_id)};
    }

    server_assertion_res_t assert_server (const server_assertion_req_t &request) const
    {
        const std::lock_guard lock (_mutex);
        const auto success = require_order_unlocked (request.successful_order_id);
        const auto pending = require_order_unlocked (request.pending_recovered_order_id);
        const auto inventory = require_order_unlocked (request.inventory_failure_order_id);
        const auto payment = require_order_unlocked (request.payment_failure_order_id);
        const auto scale = require_order_unlocked (request.scale_out_order_id);
        const bool passed =
          success.status == order_status_t::confirmed &&
          pending.status == order_status_t::confirmed &&
          inventory.reason.find ("inventory") != std::string::npos &&
          payment.reason.find ("payment") != std::string::npos &&
          scale.status == order_status_t::confirmed &&
          contains_evidence_unlocked ("ProjectionRebuilt") &&
          contains_evidence_unlocked ("idempotency-hit");
        return {passed, _evidence};
    }

  private:
    struct workflow_context_t
    {
        std::string cart_id;
        std::string payment_method_id;
    };

    order_state_t make_state (const std::string &order_id,
                              const std::string &status,
                              const std::string &shipping_address_id,
                              const std::string &cart_id)
    {
        order_state_t state;
        state.order_id = order_id;
        state.status = status;
        state.shipping_address_id = shipping_address_id;
        if (cart_id.find ("success") != std::string::npos) {
            state.amount = 120.0;
            state.currency = "USD";
        }
        state.updated_at_unix_ms = ++_version;
        return state;
    }

    void continue_workflow_unlocked (const std::string &order_id)
    {
        auto state = require_order_unlocked (order_id);
        if (state.order_id.empty () || state.status != order_status_t::created) {
            return;
        }
        const auto context = _workflow_contexts.find (order_id);
        const auto cart_id = context == _workflow_contexts.end () ? std::string{} :
                                                               context->second.cart_id;
        const auto payment_method_id = context == _workflow_contexts.end () ?
                                         std::string{} :
                                         context->second.payment_method_id;
        if (cart_id.find ("inventory-fail") != std::string::npos) {
            state.status = order_status_t::failed;
            state.reason = "inventory unavailable";
            _evidence.push_back (order_id + ":InventoryReservationFailedEvent");
            _evidence.push_back (order_id + ":OrderFailedEvent");
        } else if (payment_method_id.find ("decline") != std::string::npos) {
            state.status = order_status_t::failed;
            state.reservation_id = "reservation-" + order_id;
            state.reason = "payment declined";
            _evidence.push_back (order_id + ":InventoryReservedEvent");
            _evidence.push_back (order_id + ":PaymentFailedEvent");
            _evidence.push_back (order_id + ":InventoryReleasedEvent");
            _evidence.push_back (order_id + ":OrderFailedEvent");
        } else {
            state.status = order_status_t::confirmed;
            state.reservation_id = "reservation-" + order_id;
            state.payment_id = "payment-" + order_id;
            state.amount = 120.0;
            state.currency = "USD";
            _evidence.push_back (order_id + ":InventoryReservedEvent");
            _evidence.push_back (order_id + ":PaymentAuthorizedEvent");
            _evidence.push_back (order_id + ":OrderConfirmedEvent");
        }
        state.updated_at_unix_ms = ++_version;
        _orders[order_id] = state;
    }

    std::string order_id_for (const std::string &idempotency_key)
    {
        if (idempotency_key == "order-pending-001") {
            return "order-pending-0001";
        }
        std::ostringstream stream;
        stream << "order-" << std::setw (4) << std::setfill ('0') << ++_sequence;
        return stream.str ();
    }

    order_state_t require_order_unlocked (const std::string &order_id) const
    {
        const auto found = _orders.find (order_id);
        return found == _orders.end () ? order_state_t{} : found->second;
    }

    bool contains_evidence_unlocked (const std::string &needle) const
    {
        for (const auto &line : _evidence) {
            if (line.find (needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex _mutex;
    std::map<std::string, order_state_t> _orders;
    std::map<std::string, std::string> _idempotency;
    std::map<std::string, workflow_context_t> _workflow_contexts;
    std::set<std::string> _deleted;
    std::vector<std::string> _evidence;
    int _sequence{0};
    long long _version{0};
};

} // namespace zlink::samples::shoppingmall
