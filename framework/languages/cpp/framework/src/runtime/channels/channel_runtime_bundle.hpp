/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
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
    bool try_add_auto_connection (std::string endpoint, std::uint32_t weight = 100);
    void remove_auto_connection (const std::string &endpoint);
    bool contains_auto_connection (const std::string &endpoint) const;
    std::vector<std::string> list_auto_connections () const;
    std::optional<std::string> next_manual_connection ();
    std::vector<std::string> manual_connections_from_next ();
    std::vector<std::string> connections_from_next ();

    bool try_enter_receive () noexcept;
    void leave_receive () noexcept;
    bool receive_active () const noexcept;

  private:
    mutable std::mutex _mutex;
    std::set<std::string> _manual_connections;
    std::map<std::string, std::uint32_t> _auto_connections;
    std::size_t _next_manual_connection = 0;
    std::size_t _next_connection = 0;
    std::atomic_bool _receive_active = false;
};

} // namespace zlink::framework::detail
