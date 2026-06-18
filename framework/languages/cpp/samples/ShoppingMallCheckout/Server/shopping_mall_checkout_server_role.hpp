/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <stdexcept>
#include <utility>

namespace zlink::samples::shoppingmallcheckout
{

class shopping_mall_checkout_server_role_t
{
  public:
    checkout_state_t start_checkout (std::string customer_id)
    {
        state_ = checkout_state_t{"checkout-1", std::move (customer_id),
                                  checkout_status_t::cart_created};
        started_ = true;
        return state_;
    }

    checkout_state_t advance (std::string status)
    {
        require_started ();
        state_.status = std::move (status);
        return state_;
    }

    checkout_state_t advance (const std::string &checkout_id, std::string status)
    {
        require_started ();
        if (state_.checkout_id != checkout_id) {
            throw std::logic_error ("unknown checkout");
        }
        state_.status = std::move (status);
        return state_;
    }

  private:
    void require_started () const
    {
        if (!started_) {
            throw std::logic_error ("checkout was not started");
        }
    }

    bool started_{false};
    checkout_state_t state_;
};

} // namespace zlink::samples::shoppingmallcheckout
