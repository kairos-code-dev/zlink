/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::framework::e2e::observability_ops::client
{

struct verification_input_t
{
    std::string scenario_id;
    std::map<std::string, std::string> files;
};

inline void require (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

inline const std::string &required_file (const verification_input_t &input, const std::string &name)
{
    const auto found = input.files.find (name);
    require (found != input.files.end () && !found->second.empty (),
             input.scenario_id + " requires evidence file " + name);
    return found->second;
}

inline nlohmann::json read_json (const verification_input_t &input, const std::string &name)
{
    std::ifstream stream (required_file (input, name));
    require (static_cast<bool> (stream), input.scenario_id + " cannot open evidence file " + name);
    return nlohmann::json::parse (stream);
}

inline std::vector<std::string> read_lines (const verification_input_t &input,
                                            const std::string &name)
{
    std::ifstream stream (required_file (input, name));
    require (static_cast<bool> (stream), input.scenario_id + " cannot open log file " + name);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline (stream, line)) {
        lines.push_back (std::move (line));
    }
    return lines;
}

inline std::vector<std::string> read_optional_lines (const verification_input_t &input,
                                                     const std::string &name)
{
    std::ifstream stream (required_file (input, name));
    std::vector<std::string> lines;
    std::string line;
    while (std::getline (stream, line)) {
        lines.push_back (std::move (line));
    }
    return lines;
}

inline std::set<std::string> flow_ids (const std::vector<std::string> &lines,
                                       const std::string &required = {})
{
    static const std::regex pattern (R"(flow=([0-9a-f-]{36}))");
    std::set<std::string> ids;
    for (const auto &line : lines) {
        if (!required.empty () && line.find (required) == std::string::npos) {
            continue;
        }
        std::smatch match;
        if (std::regex_search (line, match, pattern)) {
            ids.insert (match[1].str ());
        }
    }
    return ids;
}

inline bool has_line (const std::vector<std::string> &lines, const std::string &value)
{
    return std::any_of (lines.begin (), lines.end (),
                        [&] (const auto &line) { return line.find (value) != std::string::npos; });
}

inline std::vector<nlohmann::json> metrics_named (const nlohmann::json &body,
                                                  const std::string &name)
{
    std::vector<nlohmann::json> result;
    for (const auto &metric : body.at ("metrics")) {
        if (metric.at ("name").get<std::string> () == name) {
            result.push_back (metric);
        }
    }
    return result;
}

inline bool has_drain_state (const nlohmann::json &body, const std::string &state)
{
    return std::any_of (body.at ("drainEvents").begin (), body.at ("drainEvents").end (),
                        [&] (const nlohmann::json &event) {
                            return event.at ("state").get<std::string> () == state;
                        });
}

int run_scenario_verification (const verification_input_t &input);

} // namespace zlink::framework::e2e::observability_ops::client
