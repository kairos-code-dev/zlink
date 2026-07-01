/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::resilience_lifecycle::registry
{

class evidence_store_t
{
  public:
    explicit evidence_store_t (std::string rid, std::string file_path = {}) :
        _rid (std::move (rid)), _file_path (std::move (file_path))
    {
        if (!_file_path.empty ()) {
            if (const auto parent = std::filesystem::path (_file_path).parent_path ();
                !parent.empty ()) {
                std::filesystem::create_directories (parent);
            }
            std::ofstream file (_file_path, std::ios::trunc);
        }
    }

    const std::string &rid () const noexcept { return _rid; }

    void add (std::string entry)
    {
        std::lock_guard lock (_mutex);
        _entries.push_back (entry);
        if (!_file_path.empty ()) {
            std::ofstream file (_file_path, std::ios::app);
            file << entry << '\n';
        }
    }

    std::vector<std::string> snapshot () const
    {
        std::lock_guard lock (_mutex);
        return _entries;
    }

    void clear ()
    {
        std::lock_guard lock (_mutex);
        _entries.clear ();
        if (!_file_path.empty ()) {
            std::ofstream file (_file_path, std::ios::trunc);
        }
    }

  private:
    std::string _rid;
    std::string _file_path;
    mutable std::mutex _mutex;
    std::vector<std::string> _entries;
};

} // namespace zlink::framework::e2e::resilience_lifecycle::registry
