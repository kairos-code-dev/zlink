#include "raw_client/scenario_runner.hpp"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    // Default: scenario mode with 3 clients
    std::vector<sample::ClientConfig> configs;

    // Parse --client Name:host:port arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--client" && i + 1 < argc) {
            std::string spec = argv[++i];
            // Parse "Alice:127.0.0.1:7001"
            auto pos1 = spec.find(':');
            auto pos2 = spec.find(':', pos1 + 1);
            if (pos1 != std::string::npos && pos2 != std::string::npos) {
                sample::ClientConfig cfg;
                cfg.name = spec.substr(0, pos1);
                cfg.host = spec.substr(pos1 + 1, pos2 - pos1 - 1);
                cfg.port = static_cast<uint16_t>(std::stoi(spec.substr(pos2 + 1)));
                // Determine server label from port
                if (cfg.port == 7001) cfg.server_label = "FrontA";
                else if (cfg.port == 7002) cfg.server_label = "FrontB";
                else cfg.server_label = "Front" + std::to_string(cfg.port);
                configs.push_back(cfg);
            }
        }
    }

    // Default configs if none provided
    if (configs.empty()) {
        configs = {
            {"Alice", "127.0.0.1", 7001, "FrontA"},
            {"Bob",   "127.0.0.1", 7002, "FrontB"},
            {"Carol", "127.0.0.1", 7001, "FrontA"},
        };
    }

    sample::ScenarioRunner runner(configs);
    return runner.run();
}
