#include "common/ids.hpp"
#include <sstream>
#include <iomanip>

namespace sample {

static std::atomic<uint64_t> g_req_counter{0};

std::string next_req_id() {
    return std::to_string(++g_req_counter);
}

std::string routing_id_to_hex(const void* data, size_t size) {
    std::ostringstream oss;
    auto bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(bytes[i]);
    return oss.str();
}

} // namespace sample
