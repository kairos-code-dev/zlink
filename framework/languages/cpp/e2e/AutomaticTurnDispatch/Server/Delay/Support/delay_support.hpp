/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../../Shared/env.hpp"

#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::automatic_turn_dispatch::server::delay {

struct delay_options_t
{
    std::string log_dir;
    std::string node_rid;
    std::string http_endpoint;
    std::string delay_endpoint;
};

inline delay_options_t read_delay_options ()
{
    return delay_options_t{
      .log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
      .node_rid = server::env_or ("ZLINK_CPP_E2E_NODE_RID", "delay-a"),
      .http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
      .delay_endpoint = server::env_or ("ZLINK_CPP_E2E_DELAY_ENDPOINT")};
}

class delay_evidence_store_t
{
  public:
    explicit delay_evidence_store_t (std::string file_path) :
        _file_path (std::move (file_path))
    {
    }

    void add (std::string entry)
    {
        std::lock_guard lock (_mutex);
        _entries.push_back (entry);
        std::ofstream out (_file_path, std::ios::app);
        out << entry << '\n';
    }

    std::vector<std::string> snapshot () const
    {
        std::lock_guard lock (_mutex);
        return _entries;
    }

  private:
    std::string _file_path;
    mutable std::mutex _mutex;
    std::vector<std::string> _entries;
};

struct delay_state_t
{
    explicit delay_state_t (std::string rid, std::string evidence_file) :
        node_rid (std::move (rid)), evidence (std::move (evidence_file))
    {
    }

    std::string node_rid;
    delay_evidence_store_t evidence;
};

} // namespace zlink::framework::e2e::automatic_turn_dispatch::server::delay
