/* SPDX-License-Identifier: MPL-2.0 */

#include "addon_core_perf.h"

#include <chrono>

namespace
{

static const uint32_t k_perf_metric_magic = 0x5A4C4E4BU;

void perf_write_u32_le (unsigned char *dst, uint32_t value)
{
    dst[0] = static_cast<unsigned char> (value & 0xffU);
    dst[1] = static_cast<unsigned char> ((value >> 8) & 0xffU);
    dst[2] = static_cast<unsigned char> ((value >> 16) & 0xffU);
    dst[3] = static_cast<unsigned char> ((value >> 24) & 0xffU);
}

void perf_write_u64_le (unsigned char *dst, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i)
        dst[i] = static_cast<unsigned char> ((value >> (i * 8)) & 0xffU);
}

uint32_t perf_read_u32_le (const unsigned char *src)
{
    return static_cast<uint32_t> (src[0]) | (static_cast<uint32_t> (src[1]) << 8)
           | (static_cast<uint32_t> (src[2]) << 16) | (static_cast<uint32_t> (src[3]) << 24);
}

uint64_t perf_read_u64_le (const unsigned char *src)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t> (src[i]) << (i * 8);
    return value;
}

} // namespace

uint64_t perf_now_ns ()
{
    return static_cast<uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (
                                    std::chrono::system_clock::now ().time_since_epoch ())
                                    .count ());
}

bool perf_stamp_payload (void *payload,
                         size_t payload_size,
                         uint32_t run_id,
                         uint8_t phase,
                         uint32_t msg_size,
                         uint64_t seq)
{
    if (!payload || payload_size < k_perf_metric_header_size)
        return false;

    unsigned char *p = static_cast<unsigned char *> (payload);
    perf_write_u32_le (p + 0, k_perf_metric_magic);
    perf_write_u32_le (p + 4, run_id);
    p[8] = phase;
    perf_write_u32_le (p + 9, msg_size);
    perf_write_u64_le (p + 13, seq);
    perf_write_u64_le (p + 21, perf_now_ns ());
    return true;
}

bool perf_decode_payload_header (
  zlink_msg_t *parts, size_t part_count, uint32_t run_id, uint32_t msg_size, uint64_t *sent_ts_ns)
{
    if (!parts || part_count == 0 || !sent_ts_ns)
        return false;
    if (zlink_msg_size (&parts[0]) < k_perf_metric_header_size)
        return false;
    const unsigned char *p = static_cast<const unsigned char *> (zlink_msg_data (&parts[0]));
    if (perf_read_u32_le (p + 0) != k_perf_metric_magic)
        return false;
    if (perf_read_u32_le (p + 4) != run_id)
        return false;
    if (p[8] != 1U)
        return false;
    if (perf_read_u32_le (p + 9) != msg_size)
        return false;
    *sent_ts_ns = perf_read_u64_le (p + 21);
    return true;
}

void perf_set_double_prop (napi_env env, napi_value obj, const char *name, double value)
{
    napi_value out;
    napi_create_double (env, value, &out);
    napi_set_named_property (env, obj, name, out);
}
