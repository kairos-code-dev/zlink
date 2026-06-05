/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/channels/channel_pending_requests.hpp"

#include <set>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

class channel_runtime_bundle_t
{
  public:
    bool try_add_manual_connection (std::string endpoint);
    void remove_manual_connection (const std::string &endpoint);
    bool contains_manual_connection (const std::string &endpoint) const;
    std::vector<std::string> list_manual_connections () const;

    bool try_enter_receive () noexcept;
    void leave_receive () noexcept;
    bool receive_active () const noexcept;

    channel_pending_requests_t &dealer_mesh_pending_requests () noexcept;
    const channel_pending_requests_t &dealer_mesh_pending_requests () const noexcept;

  private:
    std::set<std::string> _manual_connections;
    bool _receive_active = false;
    channel_pending_requests_t _dealer_mesh_pending_requests;
};

} // namespace zlink::framework::detail
