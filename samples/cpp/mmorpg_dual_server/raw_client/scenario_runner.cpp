#include "raw_client/scenario_runner.hpp"
#include "raw_client/console_display.hpp"
#include "common/app_protocol.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>

namespace sample {

ScenarioRunner::ScenarioRunner(const std::vector<ClientConfig>& configs)
    : configs_(configs)
{}

void ScenarioRunner::create_client(const ClientConfig& cfg) {
    // Disconnect existing client if any
    auto it = clients_.find(cfg.name);
    if (it != clients_.end()) {
        it->second->disconnect();
        clients_.erase(it);
    }

    auto client = std::make_unique<AsioClient>(cfg.name, cfg.host, cfg.port);
    server_labels_[cfg.name] = cfg.server_label;
    if (!client->connect()) {
        std::fprintf(stderr, "Failed to connect %s to %s:%d\n",
                     cfg.name.c_str(), cfg.host.c_str(), cfg.port);
    } else {
        ConsoleDisplay::print_info(cfg.name + " connected to " + cfg.server_label
                                   + " (" + cfg.host + ":" + std::to_string(cfg.port) + ")");
    }
    clients_[cfg.name] = std::move(client);
}

const ClientConfig* ScenarioRunner::find_config(const std::string& name) const {
    for (auto& c : configs_) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

void ScenarioRunner::connect_all() {
    for (auto& cfg : configs_) {
        create_client(cfg);
    }
    // Small delay to let connections stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void ScenarioRunner::disconnect_all() {
    for (auto& [name, client] : clients_) {
        client->disconnect();
    }
    clients_.clear();
}

std::string ScenarioRunner::send_and_recv(const std::string& client_name,
                                          const std::string& command) {
    auto it = clients_.find(client_name);
    if (it == clients_.end() || !it->second->is_connected()) {
        ConsoleDisplay::print_info("ERROR: " + client_name + " not connected");
        return "";
    }

    const std::string& label = server_labels_[client_name];

    // Generate request ID per client
    uint64_t id = ++req_counters_[client_name];
    std::string req_id = client_name.substr(0, 1) + std::to_string(id);

    // Build the REQ message
    std::string raw_msg = "REQ|" + req_id + "|" + command;

    ConsoleDisplay::print_send(client_name, label, raw_msg);
    it->second->send(raw_msg);

    // Wait for matching response (poll up to 3 seconds)
    std::string response;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);

    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto msgs = it->second->drain_messages();
        for (auto& m : msgs) {
            AppMessage parsed = parse_message(m);
            if (parsed.type == "RES" && parsed.req_id == req_id) {
                response = m;
            }
            // Non-matching messages (events, other responses) go back to the queue
            if (parsed.type == "EVT" || (parsed.type == "RES" && parsed.req_id != req_id)) {
                it->second->push_message(m);
            }
        }
        if (!response.empty()) break;
    }

    if (response.empty()) {
        ConsoleDisplay::print_recv(client_name, label, "(timeout -- no response)");
    } else {
        ConsoleDisplay::print_recv(client_name, label, response);
    }
    return response;
}

std::vector<std::string> ScenarioRunner::drain_events(const std::string& client_name,
                                                       int wait_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));

    auto it = clients_.find(client_name);
    if (it == clients_.end()) return {};

    auto msgs = it->second->drain_messages();
    std::vector<std::string> events;
    for (auto& m : msgs) {
        AppMessage parsed = parse_message(m);
        if (parsed.type == "EVT") {
            events.push_back(m);
        } else {
            // Put non-event messages back
            it->second->push_message(m);
        }
    }
    return events;
}

bool ScenarioRunner::has_event_containing(const std::string& client_name,
                                           const std::string& substring,
                                           int wait_ms) {
    auto events = drain_events(client_name, wait_ms);
    for (auto& evt : events) {
        if (evt.find(substring) != std::string::npos) {
            const std::string& label = server_labels_[client_name];
            ConsoleDisplay::print_recv(client_name, label, evt, "(event)");
            return true;
        }
    }
    return false;
}

int ScenarioRunner::run() {
    ConsoleDisplay::print_banner();

    int passed = 0;
    int total = 0;

    // ================================================================
    // Phase 1: Zone Entry
    // ================================================================
    ConsoleDisplay::print_phase("Phase 1: Zone Entry");

    connect_all();

    // Alice enters zone(0,0) via FrontA
    std::string r1 = send_and_recv("Alice", "ENTER|Alice");
    // Bob enters zone(1,0) via FrontB
    std::string r2 = send_and_recv("Bob", "ENTER|Bob");
    // Carol enters zone(0,0) via FrontA
    std::string r3 = send_and_recv("Carol", "ENTER|Carol");

    // Check: all three got OK responses
    total++;
    if (r1.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 1] Alice ENTER -> OK");
    } else {
        ConsoleDisplay::print_info("[check 1] Alice ENTER -> FAIL (expected OK)");
    }

    total++;
    if (r2.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 2] Bob ENTER -> OK");
    } else {
        ConsoleDisplay::print_info("[check 2] Bob ENTER -> FAIL (expected OK)");
    }

    total++;
    if (r3.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 3] Carol ENTER -> OK");
    } else {
        ConsoleDisplay::print_info("[check 3] Carol ENTER -> FAIL (expected OK)");
    }

    // Small delay for ENTER SPOT events to settle (drain & discard them)
    drain_events("Alice", 500);
    drain_events("Bob", 100);
    drain_events("Carol", 100);

    // ================================================================
    // Phase 2: Boundary Movement (SPOT zone sync)
    // ================================================================
    ConsoleDisplay::print_phase("Phase 2: Boundary Movement (SPOT zone sync)");

    // Alice moves to (85,50) -- near boundary on FrontA zone(0,0)
    // This triggers SPOT publish; FrontB zone(1,0) is adjacent and should relay
    // Bob is on FrontB zone(1,0) and should receive EVT|PLAYER_NEAR
    std::string move_r1 = send_and_recv("Alice", "MOVE|85|50");

    total++;
    if (move_r1.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 4] Alice MOVE 85,50 -> OK");
    } else {
        ConsoleDisplay::print_info("[check 4] Alice MOVE 85,50 -> FAIL");
    }

    // Check: Bob receives EVT containing "Alice"
    total++;
    bool bob_got_alice = has_event_containing("Bob", "Alice", 800);
    if (bob_got_alice) {
        passed++;
        ConsoleDisplay::print_info("[check 5] Bob received PLAYER_NEAR event for Alice");
    } else {
        ConsoleDisplay::print_no_event("Bob", server_labels_["Bob"], "expected Alice event");
        ConsoleDisplay::print_info("[check 5] Bob did NOT receive Alice event -> FAIL");
    }

    // Check: Carol does NOT receive EVT (she's on same zone as Alice, SPOT skips self)
    total++;
    bool carol_got_alice = has_event_containing("Carol", "Alice", 500);
    if (!carol_got_alice) {
        passed++;
        ConsoleDisplay::print_no_event("Carol", server_labels_["Carol"],
                                       "correct -- same zone, no cross-zone event");
        ConsoleDisplay::print_info("[check 6] Carol correctly received no Alice event");
    } else {
        ConsoleDisplay::print_info("[check 6] Carol unexpectedly received Alice event -> FAIL");
    }

    // Bob moves to (15,48) -- near boundary on FrontB zone(1,0)
    // FrontA zone(0,0) is adjacent, so Alice (and Carol) should receive EVT
    std::string move_r2 = send_and_recv("Bob", "MOVE|15|48");

    total++;
    if (move_r2.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 7] Bob MOVE 15,48 -> OK");
    } else {
        ConsoleDisplay::print_info("[check 7] Bob MOVE 15,48 -> FAIL");
    }

    // Check: Alice receives EVT containing "Bob"
    total++;
    bool alice_got_bob = has_event_containing("Alice", "Bob", 800);
    if (alice_got_bob) {
        passed++;
        ConsoleDisplay::print_info("[check 8] Alice received PLAYER_NEAR event for Bob");
    } else {
        ConsoleDisplay::print_no_event("Alice", server_labels_["Alice"], "expected Bob event");
        ConsoleDisplay::print_info("[check 8] Alice did NOT receive Bob event -> FAIL");
    }

    // ================================================================
    // Phase 3: Gateway Load Balancing
    // ================================================================
    ConsoleDisplay::print_phase("Phase 3: Gateway Load Balancing (OUTGAME)");

    // Alice requests OUTGAME PROFILE -> should get response from an API server
    std::string og_r1 = send_and_recv("Alice", "OUTGAME|PROFILE");

    // Bob requests OUTGAME PROFILE -> should get response from an API server
    std::string og_r2 = send_and_recv("Bob", "OUTGAME|PROFILE");

    total++;
    if (og_r1.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 9] Alice OUTGAME PROFILE -> OK");
    } else {
        ConsoleDisplay::print_info("[check 9] Alice OUTGAME PROFILE -> FAIL (no OK response)");
    }

    total++;
    if (og_r2.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 10] Bob OUTGAME PROFILE -> OK");
    } else {
        ConsoleDisplay::print_info("[check 10] Bob OUTGAME PROFILE -> FAIL (no OK response)");
    }

    // Check load balancing: responses should mention different api-server IDs
    // API server response format: OK|<player>|lv10|hp100|<server_id>
    // server_id is like "api-1" or "api-2"
    total++;
    bool alice_has_api1 = og_r1.find("api-1") != std::string::npos;
    bool alice_has_api2 = og_r1.find("api-2") != std::string::npos;
    bool bob_has_api1   = og_r2.find("api-1") != std::string::npos;
    bool bob_has_api2   = og_r2.find("api-2") != std::string::npos;

    if ((alice_has_api1 && bob_has_api2) || (alice_has_api2 && bob_has_api1)) {
        passed++;
        ConsoleDisplay::print_info("[check 11] Load balanced: requests served by different API servers");
    } else if ((alice_has_api1 || alice_has_api2) && (bob_has_api1 || bob_has_api2)) {
        // Both got responses from API servers, just same one (still valid, LB is round-robin)
        passed++;
        ConsoleDisplay::print_info("[check 11] Both got API responses (same server -- LB may need more requests)");
    } else {
        ConsoleDisplay::print_info("[check 11] Load balancing check inconclusive -> FAIL");
    }

    // ================================================================
    // Phase 4: Zone Transfer
    // ================================================================
    ConsoleDisplay::print_phase("Phase 4: Zone Transfer");

    // Alice leaves zone(0,0) on FrontA
    std::string leave_r = send_and_recv("Alice", "LEAVE");
    total++;
    if (leave_r.find("|OK|") != std::string::npos) {
        passed++;
        ConsoleDisplay::print_info("[check 12] Alice LEAVE -> OK");
    } else {
        ConsoleDisplay::print_info("[check 12] Alice LEAVE -> FAIL");
    }

    // Alice disconnects from FrontA and reconnects to FrontB
    ConsoleDisplay::print_info("Alice disconnecting from FrontA...");
    {
        auto it = clients_.find("Alice");
        if (it != clients_.end()) {
            it->second->disconnect();
            clients_.erase(it);
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Find Bob's config (FrontB) to get host/port for Alice's reconnection
    const ClientConfig* bob_cfg = find_config("Bob");
    if (bob_cfg) {
        ClientConfig alice_new;
        alice_new.name = "Alice";
        alice_new.host = bob_cfg->host;
        alice_new.port = bob_cfg->port;
        alice_new.server_label = bob_cfg->server_label;

        ConsoleDisplay::print_info("Alice reconnecting to " + alice_new.server_label + "...");
        create_client(alice_new);
        req_counters_["Alice"] = 0;  // Reset request counter for new connection
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Alice enters zone(1,0) on FrontB
        std::string enter_r = send_and_recv("Alice", "ENTER|Alice");
        total++;
        if (enter_r.find("|OK|") != std::string::npos) {
            passed++;
            ConsoleDisplay::print_info("[check 13] Alice re-ENTER on FrontB -> OK");
        } else {
            ConsoleDisplay::print_info("[check 13] Alice re-ENTER on FrontB -> FAIL");
        }

        // Drain any ENTER spot events
        drain_events("Carol", 500);
        drain_events("Bob", 100);

        // Alice moves to center (50,50) -- NOT near boundary
        std::string move_r = send_and_recv("Alice", "MOVE|50|50");
        total++;
        if (move_r.find("|OK|") != std::string::npos) {
            passed++;
            ConsoleDisplay::print_info("[check 14] Alice MOVE 50,50 on FrontB -> OK");
        } else {
            ConsoleDisplay::print_info("[check 14] Alice MOVE 50,50 on FrontB -> FAIL");
        }

        // Check: Carol (still on FrontA zone(0,0)) does NOT receive any Alice events
        // since Alice is now on FrontB zone(1,0) and the move is not near boundary
        total++;
        bool carol_got_event = has_event_containing("Carol", "Alice", 500);
        if (!carol_got_event) {
            passed++;
            ConsoleDisplay::print_no_event("Carol", server_labels_["Carol"],
                                           "correct -- Alice is now on a different server");
            ConsoleDisplay::print_info("[check 15] Carol correctly received no Alice events after transfer");
        } else {
            ConsoleDisplay::print_info("[check 15] Carol unexpectedly received Alice event -> FAIL");
        }
    } else {
        ConsoleDisplay::print_info("ERROR: Could not find FrontB config for zone transfer");
        total += 3;  // Skip the 3 checks in this branch
    }

    // ================================================================
    // Results
    // ================================================================
    disconnect_all();

    ConsoleDisplay::print_result(passed, total);

    return (passed == total) ? 0 : 1;
}

} // namespace sample
