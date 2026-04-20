#ifndef PERF_SINGLE_METRIC_HEADER_HPP
#define PERF_SINGLE_METRIC_HEADER_HPP

#include <chrono>
#include <cstring>
#include <stdint.h>

namespace perf_single_metric {

static const uint32_t k_magic = 0x5A4C4E4BU;

enum phase_t : uint8_t
{
    phase_warmup = 0,
    phase_active = 1,
    phase_cooldown = 2
};

struct header_t
{
    uint32_t magic;
    uint32_t run_id;
    uint8_t phase;
    uint32_t msg_size;
    uint64_t seq;
    int64_t sent_ts_ns;
};

inline size_t header_size ()
{
    return 29U;
}

inline int64_t now_ns ()
{
    return static_cast<int64_t> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::system_clock::now ().time_since_epoch ())
        .count ());
}

inline void init_header (header_t *out,
                         uint32_t run_id,
                         phase_t phase,
                         size_t msg_size,
                         uint64_t seq,
                         int64_t sent_ts_ns)
{
    if (!out)
        return;

    out->magic = k_magic;
    out->run_id = run_id;
    out->phase = static_cast<uint8_t> (phase);
    out->msg_size = static_cast<uint32_t> (msg_size);
    out->seq = seq;
    out->sent_ts_ns = sent_ts_ns;
}

inline bool encode_header (void *dst, size_t dst_size, const header_t &h)
{
    if (!dst || dst_size < header_size ())
        return false;

    unsigned char *p = static_cast<unsigned char *> (dst);
    std::memcpy (p + 0, &h.magic, sizeof (h.magic));
    std::memcpy (p + 4, &h.run_id, sizeof (h.run_id));
    std::memcpy (p + 8, &h.phase, sizeof (h.phase));
    std::memcpy (p + 9, &h.msg_size, sizeof (h.msg_size));
    std::memcpy (p + 13, &h.seq, sizeof (h.seq));
    std::memcpy (p + 21, &h.sent_ts_ns, sizeof (h.sent_ts_ns));
    return true;
}

inline bool decode_header (const void *src, size_t src_size, header_t *out)
{
    if (!src || src_size < header_size () || !out)
        return false;

    const unsigned char *p = static_cast<const unsigned char *> (src);
    std::memcpy (&out->magic, p + 0, sizeof (out->magic));
    std::memcpy (&out->run_id, p + 4, sizeof (out->run_id));
    std::memcpy (&out->phase, p + 8, sizeof (out->phase));
    std::memcpy (&out->msg_size, p + 9, sizeof (out->msg_size));
    std::memcpy (&out->seq, p + 13, sizeof (out->seq));
    std::memcpy (&out->sent_ts_ns, p + 21, sizeof (out->sent_ts_ns));
    return true;
}

inline bool stamp_payload (void *payload,
                           size_t payload_size,
                           uint32_t run_id,
                           phase_t phase,
                           size_t msg_size,
                           uint64_t seq,
                           int64_t sent_ts_ns)
{
    if (!payload || payload_size < header_size ())
        return false;

    header_t h;
    init_header (&h, run_id, phase, msg_size, seq, sent_ts_ns);
    return encode_header (payload, payload_size, h);
}

inline bool decode_payload_header (const void *payload,
                                   size_t payload_size,
                                   header_t *out)
{
    if (!decode_header (payload, payload_size, out))
        return false;
    return out->magic == k_magic;
}

inline bool is_expected (const header_t &h,
                         uint32_t run_id,
                         phase_t phase,
                         size_t msg_size)
{
    return h.magic == k_magic && h.run_id == run_id
           && h.phase == static_cast<uint8_t> (phase)
           && h.msg_size == static_cast<uint32_t> (msg_size);
}

} // namespace perf_single_metric

#endif
