/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/connection_pool.hpp"

namespace zlink::http_client::detail
{

std::unique_ptr<pooled_connection_t> connection_pool_t::acquire (const std::string &key)
{
    const std::lock_guard<std::mutex> lock (_mutex);
    auto found = _idle.find (key);
    if (found == _idle.end () || found->second.empty ()) {
        return nullptr;
    }
    auto connection = std::move (found->second.back ());
    found->second.pop_back ();
    return connection;
}

void connection_pool_t::release (const std::string &key,
                                 std::unique_ptr<pooled_connection_t> connection)
{
    static constexpr std::size_t max_idle_per_key = 4;
    const std::lock_guard<std::mutex> lock (_mutex);
    auto &idle = _idle[key];
    if (idle.size () < max_idle_per_key) {
        idle.push_back (std::move (connection));
    }
}

} // namespace zlink::http_client::detail
