/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace zlink::samples::bingo
{

// Resolves where a server role writes its message-flow log file. The directory is
// application configuration (sample.topology.logDir); use_file() creates it if missing.
inline std::string flow_log_path (const std::string &log_dir, const std::string &role)
{
    return log_dir + "/bingo-" + role + ".log";
}

// Bingo §17.2: pipe the zlink instrument catalog into a per-role metrics log —
// the sample stand-in for an app's OTel/Prometheus pipeline. One line per
// sample keeps the smoke assertions greppable.
inline void observe_runtime_metrics (zlink::framework::app_t &app,
                                    const std::string &log_dir,
                                    const std::string &role)
{
    std::filesystem::create_directories (log_dir);
    auto sink = std::make_shared<std::ofstream> (log_dir + "/bingo-" + role + "-metrics.log",
                                                 std::ios::app);
    auto gate = std::make_shared<std::mutex> ();
    app.monitoring ().on<zlink::framework::metric_event_payload_t> (
      [sink, gate] (const zlink::framework::metric_event_payload_t &event) {
          const std::lock_guard<std::mutex> lock (*gate);
          (*sink) << event.name << " value=" << event.value << " unit=" << event.unit;
          for (const auto &[key, value] : event.tags) {
              (*sink) << ' ' << key << '=' << value;
          }
          (*sink) << '\n';
          sink->flush ();
      });
}

} // namespace zlink::samples::bingo
