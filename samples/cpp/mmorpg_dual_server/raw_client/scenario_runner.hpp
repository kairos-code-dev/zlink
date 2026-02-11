#pragma once
#include "raw_client/asio_client.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace sample {

struct ClientConfig {
    std::string name;
    std::string host;
    uint16_t port;
    std::string server_label;  // "FrontA", "FrontB"
};

class ScenarioRunner {
public:
    explicit ScenarioRunner(const std::vector<ClientConfig>& configs);

    // Run the default scenario. Returns 0 on all checks passed, 1 otherwise.
    int run();

private:
    void connect_all();
    void disconnect_all();

    // Send command and wait for response, printing to console.
    // Returns the full raw response string.
    std::string send_and_recv(const std::string& client_name, const std::string& command);

    // Drain all pending events from a specific client after waiting.
    std::vector<std::string> drain_events(const std::string& client_name, int wait_ms);

    // Check if a specific client received an EVT containing a substring.
    bool has_event_containing(const std::string& client_name, const std::string& substring,
                              int wait_ms);

    // Create (or recreate) a client connection.
    void create_client(const ClientConfig& cfg);

    // Find config by name
    const ClientConfig* find_config(const std::string& name) const;

    std::vector<ClientConfig> configs_;
    std::unordered_map<std::string, std::unique_ptr<AsioClient>> clients_;
    std::unordered_map<std::string, std::string> server_labels_;  // client name -> server label
    std::unordered_map<std::string, uint64_t> req_counters_;
};

} // namespace sample
