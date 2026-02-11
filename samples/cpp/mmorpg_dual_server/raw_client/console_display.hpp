#pragma once
#include <string>

namespace sample {

class ConsoleDisplay {
public:
    // ANSI color codes
    static constexpr const char* RESET   = "\033[0m";
    static constexpr const char* RED     = "\033[31m";
    static constexpr const char* GREEN   = "\033[32m";
    static constexpr const char* YELLOW  = "\033[33m";
    static constexpr const char* BLUE    = "\033[34m";
    static constexpr const char* MAGENTA = "\033[35m";
    static constexpr const char* CYAN    = "\033[36m";
    static constexpr const char* BOLD    = "\033[1m";
    static constexpr const char* DIM     = "\033[2m";

    static void print_banner();
    static void print_phase(const std::string& phase_name);
    static void print_send(const std::string& client_name, const std::string& server_label,
                           const std::string& message);
    static void print_recv(const std::string& client_name, const std::string& server_label,
                           const std::string& message, const std::string& annotation = "");
    static void print_no_event(const std::string& client_name, const std::string& server_label,
                               const std::string& reason);
    static void print_result(int passed, int total);
    static void print_info(const std::string& message);

    static const char* color_for_client(const std::string& name);
};

} // namespace sample
