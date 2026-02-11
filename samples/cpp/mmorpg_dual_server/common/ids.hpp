#pragma once
#include <string>
#include <cstdint>
#include <atomic>

namespace sample {

std::string next_req_id();
std::string routing_id_to_hex(const void* data, size_t size);

} // namespace sample
