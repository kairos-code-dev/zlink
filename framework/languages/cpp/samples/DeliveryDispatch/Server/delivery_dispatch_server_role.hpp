/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::deliverydispatch
{

class delivery_dispatch_server_role_t
{
  public:
    delivery_created_t create_delivery (create_delivery_req_t request)
    {
        if (_deliveries.find (request.delivery_id) != _deliveries.end ()) {
            return {request.delivery_id};
        }

        delivery_state_t state{request.delivery_id,
                               std::move (request.customer_id),
                               std::move (request.pickup_address),
                               std::move (request.dropoff_address),
                               "",
                               delivery_status_t::created};
        _deliveries.emplace (state.delivery_id, state);
        record_status (state);
        run_dispatch_plan (state.delivery_id);
        return {state.delivery_id};
    }

    subscribe_delivery_accepted_t subscribe_delivery (std::string delivery_id)
    {
        _subscriptions.insert (delivery_id);
        return {std::move (delivery_id)};
    }

    delivery_state_t assign_courier (const std::string &delivery_id, std::string courier_id)
    {
        auto state = require_delivery (delivery_id);
        state.courier_id = std::move (courier_id);
        state.status = delivery_status_t::assigned;
        _deliveries[state.delivery_id] = state;
        record_status (state);
        return state;
    }

    delivery_state_t advance (const std::string &delivery_id,
                              std::string status,
                              std::string courier_id = {})
    {
        auto state = require_delivery (delivery_id);
        if (!courier_id.empty ()) {
            state.courier_id = std::move (courier_id);
        }
        state.status = std::move (status);
        _deliveries[state.delivery_id] = state;
        record_status (state);
        return state;
    }

    server_assertion_res_t assert_evidence (const std::string &successful_delivery_id,
                                            const std::string &reassigned_delivery_id) const
    {
        const auto successful = statuses_for (successful_delivery_id);
        const auto reassigned = statuses_for (reassigned_delivery_id);
        const bool passed =
          includes_in_order (successful,
                             {delivery_status_t::created,
                              delivery_status_t::assigned,
                              delivery_status_t::accepted,
                              delivery_status_t::picked_up,
                              delivery_status_t::delivered}) &&
          includes_in_order (reassigned,
                             {delivery_status_t::created,
                              delivery_status_t::assigned,
                              delivery_status_t::reassigned,
                              delivery_status_t::accepted,
                              delivery_status_t::delivered}) &&
          courier_for (reassigned_delivery_id, delivery_status_t::reassigned) == "courier-b" &&
          _subscriptions.find (successful_delivery_id) != _subscriptions.end () &&
          _subscriptions.find (reassigned_delivery_id) != _subscriptions.end ();
        return {passed, _evidence};
    }

  private:
    delivery_state_t require_delivery (const std::string &delivery_id) const
    {
        const auto found = _deliveries.find (delivery_id);
        if (found == _deliveries.end ()) {
            throw std::logic_error ("unknown delivery");
        }
        return found->second;
    }

    void run_dispatch_plan (const std::string &delivery_id)
    {
        if (delivery_id.find ("reassign") != std::string::npos) {
            assign_courier (delivery_id, "courier-a");
            advance (delivery_id, delivery_status_t::reassigned, "courier-b");
            advance (delivery_id, delivery_status_t::accepted, "courier-b");
            advance (delivery_id, delivery_status_t::delivered, "courier-b");
            return;
        }

        assign_courier (delivery_id, "courier-a");
        advance (delivery_id, delivery_status_t::accepted, "courier-a");
        advance (delivery_id, delivery_status_t::picked_up, "courier-a");
        advance (delivery_id, delivery_status_t::delivered, "courier-a");
    }

    void record_status (const delivery_state_t &state)
    {
        _notifications.push_back ({state.delivery_id, state.status, state.courier_id, "now"});
        _evidence.push_back (state.delivery_id + ":" + state.status + ":" +
                             (state.courier_id.empty () ? "none" : state.courier_id));
    }

    std::vector<std::string> statuses_for (const std::string &delivery_id) const
    {
        std::vector<std::string> statuses;
        for (const auto &notification : _notifications) {
            if (notification.delivery_id == delivery_id) {
                statuses.push_back (notification.status);
            }
        }
        return statuses;
    }

    std::string courier_for (const std::string &delivery_id, const std::string &status) const
    {
        for (const auto &notification : _notifications) {
            if (notification.delivery_id == delivery_id && notification.status == status) {
                return notification.courier_id;
            }
        }
        return {};
    }

    static bool includes_in_order (const std::vector<std::string> &actual,
                                   const std::vector<std::string> &expected)
    {
        std::size_t cursor = 0;
        for (const auto &status : actual) {
            if (cursor < expected.size () && status == expected[cursor]) {
                ++cursor;
            }
        }
        return cursor == expected.size ();
    }

    std::map<std::string, delivery_state_t> _deliveries;
    std::set<std::string> _subscriptions;
    std::vector<delivery_status_notify_t> _notifications;
    std::vector<std::string> _evidence;
};

} // namespace zlink::samples::deliverydispatch
