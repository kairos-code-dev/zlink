/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <service_wire_constants.hpp>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace zlink::framework::runtime::protocol
{

class service_wire_error_t : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct liveness_record_t
{
    command kind;
    std::uint64_t probe_id;
};

std::vector<std::uint8_t> encode_liveness (command kind, std::uint64_t probe_id);
liveness_record_t decode_liveness (std::span<const std::uint8_t> bytes);

} // namespace zlink::framework::runtime::protocol
