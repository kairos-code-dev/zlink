#include "raw_client/console_display.hpp"
#include <cstdio>

namespace sample {

static std::string pad_name(const std::string& name, size_t width = 5) {
    std::string result = name;
    while (result.size() < width) result += ' ';
    return result;
}

const char* ConsoleDisplay::color_for_client(const std::string& name) {
    if (name == "Alice") return CYAN;
    if (name == "Bob")   return YELLOW;
    if (name == "Carol") return MAGENTA;
    return GREEN;
}

void ConsoleDisplay::print_banner() {
    std::printf("\n");
    std::printf("%s", BOLD);
    std::printf("  %s\n", std::string(60, '=').c_str());
    std::printf("    MMORPG Dual-Server Sample  --  Multi-Client Scenario\n");
    std::printf("  %s\n", std::string(60, '=').c_str());
    std::printf("%s", RESET);
    std::printf("\n");
    std::fflush(stdout);
}

void ConsoleDisplay::print_phase(const std::string& phase_name) {
    std::printf("\n%s--- %s ---%s\n\n", BOLD, phase_name.c_str(), RESET);
    std::fflush(stdout);
}

void ConsoleDisplay::print_send(const std::string& client_name, const std::string& server_label,
                                const std::string& message) {
    const char* color = color_for_client(client_name);
    std::string padded = pad_name(client_name);
    std::printf("  %s[%s->%s]%s >>> %s\n",
                color, padded.c_str(), server_label.c_str(), RESET,
                message.c_str());
    std::fflush(stdout);
}

void ConsoleDisplay::print_recv(const std::string& client_name, const std::string& server_label,
                                const std::string& message, const std::string& annotation) {
    const char* color = color_for_client(client_name);
    std::string padded = pad_name(client_name);
    if (annotation.empty()) {
        std::printf("  %s[%s<-%s]%s <<< %s\n",
                    color, padded.c_str(), server_label.c_str(), RESET,
                    message.c_str());
    } else {
        std::printf("  %s[%s<-%s]%s <<< %s  %s%s%s\n",
                    color, padded.c_str(), server_label.c_str(), RESET,
                    message.c_str(), DIM, annotation.c_str(), RESET);
    }
    std::fflush(stdout);
}

void ConsoleDisplay::print_no_event(const std::string& client_name, const std::string& server_label,
                                    const std::string& reason) {
    std::string padded = pad_name(client_name);
    std::printf("  %s[%s<-%s]     (no event -- %s)%s\n",
                DIM, padded.c_str(), server_label.c_str(), reason.c_str(), RESET);
    std::fflush(stdout);
}

void ConsoleDisplay::print_result(int passed, int total) {
    std::printf("\n%s", BOLD);
    std::printf("  %s\n", std::string(60, '=').c_str());
    if (passed == total) {
        std::printf("  %s  Result: %d / %d checks passed  %s\n",
                    GREEN, passed, total, RESET);
    } else {
        std::printf("  %s  Result: %d / %d checks passed  %s\n",
                    RED, passed, total, RESET);
    }
    std::printf("%s  %s%s\n", BOLD, std::string(60, '=').c_str(), RESET);
    std::printf("\n");
    std::fflush(stdout);
}

void ConsoleDisplay::print_info(const std::string& message) {
    std::printf("  %s%s%s\n", DIM, message.c_str(), RESET);
    std::fflush(stdout);
}

} // namespace sample
