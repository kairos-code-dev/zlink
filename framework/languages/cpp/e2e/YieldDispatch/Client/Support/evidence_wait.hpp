/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <thread>

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
std::optional<yield_evidence_reply_t>
wait_for_evidence_snapshot (TConnector &connector,
                            const std::string &request_id,
                            const std::string &marker,
                            const std::string &target_node_rid,
                            std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now () + timeout;
    std::optional<yield_evidence_reply_t> latest;
    while (std::chrono::steady_clock::now () < deadline) {
        auto result =
          connector.request (yield_evidence_req_t{.request_id = request_id})
            .packet_name (yield_evidence_req_t::packet_name)
            .metadata (target_node_rid_metadata, target_node_rid)
            .timeout (std::chrono::milliseconds (5000))
            .template submit<yield_evidence_reply_t> ();
        if (static_cast<bool> (result)) {
            latest = result.value ();
            for (const auto &line : latest->evidence) {
                if (line.find ("request=" + request_id) != std::string::npos
                    && line.find (marker) != std::string::npos) {
                    return latest;
                }
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    (void) latest;
    return std::nullopt;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
