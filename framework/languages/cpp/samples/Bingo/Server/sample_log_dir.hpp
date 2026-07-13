/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace zlink::samples::bingo
{

// Resolves where a server role writes its message-flow log file. The directory
// comes from $BINGO_LOG_DIR (set by run_sample.sh to <sample>/logs) and defaults
// to a relative "logs" folder; use_file() creates the directory if it is missing.
inline std::string flow_log_path (const std::string &role)
{
    const char *dir = std::getenv ("BINGO_LOG_DIR");
    const std::string base = (dir != nullptr && *dir != '\0') ? dir : "logs";
    return base + "/bingo-" + role + ".log";
}

// Bingo §17.2: pipe the zlink instrument catalog into a per-role metrics log —
// the sample stand-in for an app's OTel/Prometheus pipeline. One line per
// sample keeps the smoke assertions greppable.
inline void observe_runtime_metrics (zlink::framework::app_t &app, const std::string &role)
{
    const char *dir = std::getenv ("BINGO_LOG_DIR");
    const std::string base = (dir != nullptr && *dir != '\0') ? dir : "logs";
    std::filesystem::create_directories (base);
    auto sink = std::make_shared<std::ofstream> (base + "/bingo-" + role + "-metrics.log",
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
