/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/codecs.hpp"
#include "../../Shared/env.hpp"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::yield_dispatch::server::play
{

struct play_options_t
{
    std::string log_dir;
    std::string node_rid;
    std::string http_endpoint;
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string control_endpoint;
    std::string delay_endpoint;
    std::string spot_route_endpoint;
    std::string spot_router_endpoint;
    std::string spot_pub_endpoint;
};

inline play_options_t read_play_options ()
{
    return play_options_t{
      .log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
      .node_rid = server::env_or ("ZLINK_CPP_E2E_NODE_RID", "play-a"),
      .http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
      .redis_endpoint = server::env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT"),
      .redis_key_prefix = server::env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX"),
      .control_endpoint = server::env_or ("ZLINK_CPP_E2E_CONTROL_ENDPOINT"),
      .delay_endpoint = server::env_or ("ZLINK_CPP_E2E_DELAY_ENDPOINT"),
      .spot_route_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTE_ENDPOINT"),
      .spot_router_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT"),
      .spot_pub_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT")};
}

class evidence_store_t
{
  public:
    explicit evidence_store_t (std::string node_rid) : node_rid (std::move (node_rid)) {}

    evidence_store_t (std::string node_rid, std::string log_file) :
        node_rid (std::move (node_rid)), _log_file (std::move (log_file))
    {
    }

    void add (std::string entry)
    {
        std::string persisted;
        {
            std::lock_guard lock (_mutex);
            _entries.push_back (std::move (entry));
            persisted = _entries.back ();
        }
        if (!_log_file.empty ()) {
            std::ofstream output (_log_file, std::ios::app);
            output << persisted << '\n';
        }
        _changed.notify_all ();
    }

    yield_evidence_res_t snapshot (const std::string &request_id) const
    {
        std::lock_guard lock (_mutex);
        std::vector<std::string> selected;
        for (const auto &entry : _entries) {
            if (entry.find ("request=" + request_id) != std::string::npos) {
                selected.push_back (entry);
            }
        }
        return {.request_id = request_id, .evidence = std::move (selected)};
    }

    yield_evidence_res_t wait (const yield_evidence_wait_req_t &request)
    {
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (request.timeout_milliseconds);
        std::unique_lock lock (_mutex);
        _changed.wait_until (lock, deadline, [&] {
            return contains_locked (request.request_id, request.marker);
        });
        std::vector<std::string> selected;
        for (const auto &entry : _entries) {
            if (entry.find ("request=" + request.request_id) != std::string::npos) {
                selected.push_back (entry);
            }
        }
        return {.request_id = request.request_id, .evidence = std::move (selected)};
    }

    std::string node_rid;

  private:
    bool contains_locked (const std::string &request_id, const std::string &marker) const
    {
        for (const auto &entry : _entries) {
            if (entry.find ("request=" + request_id) != std::string::npos
                && entry.find (marker) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex _mutex;
    std::condition_variable _changed;
    std::vector<std::string> _entries;
    std::string _log_file;
};

} // namespace zlink::framework::e2e::yield_dispatch::server::play
