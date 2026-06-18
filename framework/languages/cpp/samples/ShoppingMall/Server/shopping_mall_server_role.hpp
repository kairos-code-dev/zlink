/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::shoppingmall
{

class shopping_mall_server_role_t
{
  public:
    start_order_res_t start_order (const start_order_req_t &request)
    {
        const auto existing = _idempotency.find (request.idempotency_key);
        if (existing != _idempotency.end ()) {
            auto state = require_order (existing->second);
            if (state.shipping_address_id != request.shipping_address_id) {
                state.shipping_address_id = request.shipping_address_id;
                state.updated_at_unix_ms = next_version ();
                _orders[state.order_id] = state;
            }
            _evidence.push_back (state.order_id + ":idempotency-hit:" +
                                 request.idempotency_key);
            if (state.status == order_status_t::created) {
                continue_workflow (state.order_id, request.payment_method_id, request.cart_id);
            }
            return {state.order_id, require_order (state.order_id).status};
        }

        const auto order_id = order_id_for (request.idempotency_key);
        _idempotency.emplace (request.idempotency_key, order_id);
        auto state = make_state (order_id, order_status_t::created,
                                 request.shipping_address_id, request.cart_id);
        _orders.emplace (order_id, state);
        _evidence.push_back (order_id + ":created:" + request.cart_id);
        continue_workflow (order_id, request.payment_method_id, request.cart_id);
        return {order_id, state.status};
    }

    start_order_res_t seed_pending (const seed_pending_idempotency_req_t &request)
    {
        _idempotency[request.idempotency_key] = request.order_id;
        _orders[request.order_id] =
          make_state (request.order_id, order_status_t::created, "", "pending");
        _evidence.push_back (request.order_id + ":pending-seeded:" +
                             request.owner_instance_id);
        return {request.order_id, order_status_t::created};
    }

    get_order_state_res_t get_order (const std::string &order_id) const
    {
        if (_deleted_projections.find (order_id) != _deleted_projections.end ()) {
            throw std::logic_error ("projection deleted");
        }
        return {require_order (order_id)};
    }

    continue_order_workflow_res_t continue_workflow (const std::string &order_id,
                                                     const std::string &payment_method_id = "pm-ok",
                                                     const std::string &cart_id = "cart-success")
    {
        auto state = require_order (order_id);
        if (cart_id.find ("inventory-fail") != std::string::npos) {
            state.status = order_status_t::failed;
            state.reason = "inventory unavailable";
        } else if (payment_method_id.find ("decline") != std::string::npos) {
            state.status = order_status_t::failed;
            state.reservation_id = "reservation-" + order_id;
            state.reason = "payment declined";
        } else {
            state.status = order_status_t::confirmed;
            state.reservation_id = "reservation-" + order_id;
            state.payment_id = "payment-" + order_id;
            state.amount = 120.0;
            state.currency = "USD";
        }
        state.updated_at_unix_ms = next_version ();
        _orders[order_id] = state;
        _evidence.push_back (order_id + ":" + state.status + ":" +
                             (state.reason.empty () ? "ok" : state.reason));
        return {state};
    }

    get_order_state_res_t delete_projection (const std::string &order_id)
    {
        _deleted_projections.insert (order_id);
        _evidence.push_back (order_id + ":projection-deleted");
        return {require_order (order_id)};
    }

    rebuild_order_projection_res_t rebuild_projection (const std::string &order_id)
    {
        _deleted_projections.erase (order_id);
        _evidence.push_back (order_id + ":projection-rebuilt");
        return {require_order (order_id)};
    }

    server_assertion_res_t assert_evidence (const server_assertion_req_t &request) const
    {
        const auto success = require_order (request.successful_order_id);
        const auto pending = require_order (request.pending_recovered_order_id);
        const auto inventory = require_order (request.inventory_failure_order_id);
        const auto payment = require_order (request.payment_failure_order_id);
        const auto scale = require_order (request.scale_out_order_id);
        const bool passed =
          success.status == order_status_t::confirmed &&
          pending.status == order_status_t::confirmed &&
          inventory.reason.find ("inventory") != std::string::npos &&
          payment.reason.find ("payment") != std::string::npos &&
          scale.status == order_status_t::confirmed &&
          contains_evidence ("projection-rebuilt") &&
          contains_evidence ("idempotency-hit");
        return {passed, _evidence};
    }

  private:
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
        state.updated_at_unix_ms = next_version ();
        return state;
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

    order_state_t require_order (const std::string &order_id) const
    {
        const auto found = _orders.find (order_id);
        if (found == _orders.end ()) {
            throw std::logic_error ("unknown order");
        }
        return found->second;
    }

    long long next_version () { return ++_version; }

    bool contains_evidence (const std::string &needle) const
    {
        for (const auto &entry : _evidence) {
            if (entry.find (needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::map<std::string, order_state_t> _orders;
    std::map<std::string, std::string> _idempotency;
    std::set<std::string> _deleted_projections;
    std::vector<std::string> _evidence;
    int _sequence{0};
    long long _version{0};
};

} // namespace zlink::samples::shoppingmall
