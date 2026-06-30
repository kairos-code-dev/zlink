/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/codecs.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::yield_dispatch::server::play
{

class evidence_store_t
{
  public:
    explicit evidence_store_t (std::string node_rid) : node_rid (std::move (node_rid)) {}

    void add (std::string entry)
    {
        {
            std::lock_guard lock (_mutex);
            _entries.push_back (std::move (entry));
        }
        _changed.notify_all ();
    }

    yield_evidence_reply_t snapshot (const std::string &request_id) const
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

    yield_evidence_reply_t wait (const yield_evidence_wait_req_t &request)
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
};

} // namespace zlink::framework::e2e::yield_dispatch::server::play
