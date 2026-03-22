# `core/perf` send/recv template policy 리팩토링 계획

> multi echo client 3파일과 relay server 2파일의 코드 중복을
> template policy 패턴으로 해소한다.

---

## 1. 목표

| 목표 | 기준 |
|------|------|
| **성능 유지** | 리팩토링 전후 throughput/latency 5% 이내. template inline으로 런타임 비용 0 |
| **가독성** | 각 패턴 파일은 해당 패턴의 고유 특성(send/recv API, socket 생성)만 보여준다. 공통 phase/event loop는 이름으로 의미 전달 |
| **유지보수** | phase 로직 변경 시 수정 파일 3→1. 신규 echo 패턴 추가 비용 ~700줄 복붙 → ~100줄. 과도한 공통화는 하지 않는다 |

### 1.1 리팩토링 원칙과 비목표

이 계획은 저장소의 POSD 원칙을 따른다. 줄 수 감소 자체보다
**복잡도 감소, 변경 증폭 축소, 숨은 결합 제거, 깊은 모듈 설계**를 우선한다.

- **의미 보존 우선**: 이번 작업의 기본 목표는 구조 리팩토링이다.
  동작 변경은 별도 패치로 분리한다.
- **깊은 모듈**: 공통 header는 phase/event loop/metric 집계를 숨기되,
  패턴별 차이(send/recv API, socket/gateway 생성, cleanup)는 각 파일에 남긴다.
- **숨은 계약 금지**: 공통 helper가 기대하는 reply shape, inflight 모델,
  포인터 안정성 같은 전제는 문서에 명시한다.
- **확장 범위 제한**: 이 공통화는 `DEALER_ROUTER`, `ROUTER_ROUTER`,
  `GATEWAY` echo client와 relay server 2파일만 대상으로 한다.
  `PUBSUB`, `SPOT`, `STREAM`, `DEALER_DEALER`, `GATEWAY server`까지
  억지로 일반화하지 않는다.
- **검증 가능한 리팩토링**: wrapper 1회 실행만으로 충분하지 않다.
  공통 모듈이 새로 책임지는 READY/QUEUE/STOP 및 phase 계약을 직접 검증한다.

---

## 2. 현재 문제

multi echo client 3파일이 **타입 이름만 다르고 ~95% 동일한 코드**를 가짐.

| 파일 | 줄 수 | send 1줄 차이 |
|------|-------|--------------|
| `perf_multi_dealer_router_client.cpp` | 673 | `zlink_send(h, &part, 1, f)` |
| `perf_multi_router_router_client.cpp` | 701 | `zlink_send_rid(h, &rid, &part, 1, f)` |
| `perf_multi_gateway_client.cpp` | 821 | `zlink_gateway_send_rid(h, &rid, &part, 1, f)` |

phase 로직 변경 시 3파일 동시 수정 필요. `percentile_from_sorted()` 3번,
`make_routing_id()` 2번, `send_status_t`/`recv_status_t` 3번 복붙.

---

## 3. 리팩토링 후 파일 구조

```text
multi/common/
├── perf_multi_echo_client.hpp         ← 신규 (~350줄)
├── perf_multi_relay_server.hpp        ← 신규 (~200줄)
├── perf_multi_client_helpers.hpp      ← 기존 (make_routing_id 추가)
└── (기존 파일 유지)

multi/src/
├── perf_multi_dealer_router_client.cpp ← 673줄 → ~100줄
├── perf_multi_router_router_client.cpp ← 701줄 → ~120줄
├── perf_multi_gateway_client.cpp       ← 821줄 → ~140줄
├── perf_multi_dealer_router_server.cpp ← 252줄 → ~20줄
└── perf_multi_router_router_server.cpp ← 252줄 → ~20줄
```

---

## 4. 신규 파일: `multi/common/perf_multi_echo_client.hpp`

### 4.1 공통 echo client header 계약

이 header는 "아무 multi client"를 일반화하지 않는다. 아래 계약을 만족하는
**3개 echo client 전용 공통 모듈**이다.

- request/reply 왕복 모델이며, **slot당 inflight request는 최대 1개**다.
- `Policy::send()`는 `slot->target_routing_id`와 payload 1파트를 받아 전송한다.
- `Policy::recv()`는 routing id를 별도 out-parameter로 넘기더라도,
  **payload frame을 `parts[0]`로 돌려줘야 한다**. 공통 header는
  `parts[0]`에서 metric header를 decode한다.
- send blocked errno 분류는 공통 helper의 하드코딩이 아니라
  `Policy::is_blocked_send_errno()`가 소유한다. 이 덕분에 gateway의
  현재 동작도 의미 변경 없이 보존할 수 있다.
- reply header가 decode되지 않으면 해당 reply는 metric에 집계하지 않지만,
  recv 자체는 처리된 것으로 간주한다. 이 동작은 원본 구현과 동일해야 한다.
- `send_enabled && auto_send_on_recv`이면 reply 1건 처리 직후 다음 request를
  자동 seed한다. 즉 phase 진행 모델도 공통 header가 소유한다.
- active metric 집계 기준은 `run_id`, `phase_active`, `msg_size` 일치다.
  throughput은 `active_received / duration_seconds`로 계산한다.
- poller에 등록한 `user_data`는 `echo_slot_t` 주소를 가리키므로,
  **등록 후 slot 주소가 바뀌면 안 된다**. socket 기반 client는
  `resize()` 후 등록하고, gateway client는 `reserve()` 후 `push_back()`으로
  재할당을 막아야 한다.
- gateway 전용 resource 생성/monitor 연결/`zlink_gateway_destroy()`는 공통
  header의 책임이 아니라 패턴 파일의 책임이다.

```cpp
#ifndef PERF_MULTI_ECHO_CLIENT_HPP_INCLUDED
#define PERF_MULTI_ECHO_CLIENT_HPP_INCLUDED

#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "perf_multi_client_helpers.hpp"
#include "perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

namespace perf_multi_echo {

// ─── enum ────────────────────────────────────────────────────────

enum send_status_t { send_ok = 0, send_blocked = 1, send_fatal = 2 };
enum recv_status_t { recv_processed = 0, recv_none = 1, recv_fatal = 2 };

// ─── structs ─────────────────────────────────────────────────────

struct echo_slot_t
{
    echo_slot_t () :
        handle (NULL),
        slot_index (0),
        send_pending (false),
        inflight (false),
        send_enabled (false),
        auto_send_on_recv (false),
        poller_events (0),
        run_id (0),
        msg_size (0),
        next_seq (1),
        phase (perf_multi_metric::phase_unknown)
    {
        std::memset (&target_routing_id, 0, sizeof (target_routing_id));
    }

    void *handle;
    size_t slot_index;
    std::vector<char> payload;
    bench_latency_sampler_t latency;
    bool send_pending;
    bool inflight;
    bool send_enabled;
    bool auto_send_on_recv;
    short poller_events;
    zlink_routing_id_t target_routing_id;
    uint32_t run_id;
    size_t msg_size;
    uint64_t next_seq;
    perf_multi_metric::phase_t phase;
};

struct echo_state_t
{
    echo_state_t () :
        poller (NULL),
        collect_active (false),
        active_run_id (0),
        active_msg_size (0),
        active_received (0),
        fatal (false),
        fatal_errno (0)
    {
    }

    std::vector<echo_slot_t> slots;
    void *poller;
    std::atomic<bool> collect_active;
    std::atomic<uint32_t> active_run_id;
    std::atomic<size_t> active_msg_size;
    std::atomic<unsigned long long> active_received;
    std::atomic<bool> fatal;
    std::atomic<int> fatal_errno;
};

// ─── non-template helpers ────────────────────────────────────────

inline void echo_mark_fatal (echo_state_t *state, int err)
{
    if (!state)
        return;
    state->fatal.store (true, std::memory_order_release);
    state->fatal_errno.store (err != 0 ? err : EIO, std::memory_order_release);
}

inline void echo_reset_active_metrics (echo_state_t *state,
                                       uint32_t run_id,
                                       size_t msg_size)
{
    if (!state)
        return;
    state->collect_active.store (false, std::memory_order_release);
    state->active_run_id.store (run_id, std::memory_order_release);
    state->active_msg_size.store (msg_size, std::memory_order_release);
    state->active_received.store (0, std::memory_order_release);
    for (size_t i = 0; i < state->slots.size (); ++i)
        state->slots[i].latency = bench_latency_sampler_t ();
}

inline void echo_configure_phase_slots (
  std::vector<echo_slot_t> &slots,
  uint32_t run_id,
  size_t msg_size,
  perf_multi_metric::phase_t phase,
  bool send_enabled)
{
    for (size_t i = 0; i < slots.size (); ++i) {
        echo_slot_t &slot = slots[i];
        slot.run_id = run_id;
        slot.msg_size = msg_size;
        slot.phase = phase;
        slot.next_seq = 1;
        slot.send_pending = false;
        slot.inflight = false;
        slot.send_enabled = send_enabled;
        slot.auto_send_on_recv = send_enabled;
        slot.poller_events = 0;
    }
}

inline void echo_stop_phase (std::vector<echo_slot_t> &slots)
{
    for (size_t i = 0; i < slots.size (); ++i) {
        slots[i].send_enabled = false;
        slots[i].send_pending = false;
        slots[i].auto_send_on_recv = false;
    }
}

inline double echo_percentile_from_sorted (
  const std::vector<double> &sorted_samples,
  double quantile)
{
    if (sorted_samples.empty ())
        return 0.0;
    if (quantile <= 0.0)
        return sorted_samples.front ();
    if (quantile >= 1.0)
        return sorted_samples.back ();

    const double pos =
      (static_cast<double> (sorted_samples.size ()) - 1.0) * quantile;
    const size_t lo = static_cast<size_t> (pos);
    const size_t hi =
      lo + 1 < sorted_samples.size () ? lo + 1 : lo;
    const double frac = pos - static_cast<double> (lo);
    return sorted_samples[lo]
           + (sorted_samples[hi] - sorted_samples[lo]) * frac;
}

// ─── template: send ──────────────────────────────────────────────

// Policy 계약:
//   static int send(void *handle, const zlink_routing_id_t *target,
//                   zlink_msg_t *part, int flags);
//   static int recv(void *handle, zlink_routing_id_t *source,
//                   zlink_msg_t **parts, size_t *count, int flags);
//   static bool is_blocked_send_errno(int err);

template <typename Policy>
inline send_status_t echo_send_request (echo_slot_t *slot)
{
    if (!slot || !slot->handle || slot->msg_size == 0 || !slot->send_enabled)
        return send_fatal;

    const size_t payload_size =
      std::max (slot->msg_size, perf_multi_metric::header_size ());
    if (slot->payload.size () < payload_size)
        slot->payload.resize (payload_size, 'x');

    zlink_msg_t part;
    if (zlink_msg_init_data (
          &part,
          payload_size > 0
            ? static_cast<void *> (slot->payload.data ())
            : static_cast<void *> (NULL),
          payload_size, NULL, NULL)
        != 0)
        return send_fatal;

    if (!perf_multi_metric::stamp_payload (
          slot->payload.data (), payload_size,
          slot->run_id, slot->phase, slot->msg_size,
          (static_cast<uint64_t> (slot->slot_index) << 48) | slot->next_seq,
          perf_multi_metric::now_us ())) {
        zlink_msg_close (&part);
        return send_fatal;
    }

    const int rc = Policy::send (
      slot->handle, &slot->target_routing_id, &part, ZLINK_DONTWAIT);
    if (rc == 0) {
        slot->send_pending = false;
        slot->inflight = true;
        ++slot->next_seq;
        return send_ok;
    }

    const int saved_errno = errno;
    (void) zlink_msg_close (&part);
    if (Policy::is_blocked_send_errno (saved_errno)) {
        slot->send_pending = true;
        slot->inflight = false;
        errno = saved_errno;
        return send_blocked;
    }

    errno = saved_errno;
    return send_fatal;
}

// ─── template: recv ──────────────────────────────────────────────

template <typename Policy>
inline recv_status_t echo_receive_reply (echo_state_t *state,
                                         echo_slot_t *slot)
{
    if (!state || !slot || !slot->handle)
        return recv_fatal;

    zlink_routing_id_t source_rid;
    std::memset (&source_rid, 0, sizeof (source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = Policy::recv (
      slot->handle, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
    if (rc != 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return recv_none;
        return recv_fatal;
    }

    if (!parts || part_count == 0) {
        if (parts) {
            zlink_multipart_close (parts, part_count);
            free (parts);
        }
        return recv_fatal;
    }

    perf_multi_metric::header_t header;
    const bool header_ok =
      perf_multi_metric::decode_payload_header (
        zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]), &header);
    zlink_multipart_close (parts, part_count);
    free (parts);

    slot->inflight = false;
    if (header_ok
        && state->collect_active.load (std::memory_order_acquire)
        && perf_multi_metric::is_expected (
             header,
             state->active_run_id.load (std::memory_order_acquire),
             perf_multi_metric::phase_active,
             state->active_msg_size.load (std::memory_order_acquire))) {
        state->active_received.fetch_add (1, std::memory_order_acq_rel);
        const uint64_t now_us = perf_multi_metric::now_us ();
        const double latency_us =
          header.sent_ts_us > 0 && now_us >= header.sent_ts_us
            ? static_cast<double> (now_us - header.sent_ts_us) * 0.5
            : 0.0;
        slot->latency.add (latency_us);
    }

    if (slot->send_enabled && slot->auto_send_on_recv) {
        const send_status_t send_rc = echo_send_request<Policy> (slot);
        if (send_rc == send_fatal) {
            echo_mark_fatal (state, errno);
            return recv_fatal;
        }
    }

    return recv_processed;
}

// ─── template: drain + service ───────────────────────────────────

template <typename Policy>
inline bool echo_drain_replies (echo_state_t *state,
                                echo_slot_t *slot,
                                bool *progressed_out)
{
    bool progressed = false;
    while (true) {
        const recv_status_t recv_rc = echo_receive_reply<Policy> (state, slot);
        if (recv_rc == recv_none)
            break;
        if (recv_rc == recv_fatal)
            return false;
        progressed = true;
    }
    if (progressed_out)
        *progressed_out = progressed;
    return true;
}

template <typename Policy>
inline bool echo_service_slots (echo_state_t *state,
                                int timeout_ms,
                                bool *progressed_out)
{
    if (progressed_out)
        *progressed_out = false;
    if (!state || !state->poller || state->slots.empty ())
        return true;

    for (size_t i = 0; i < state->slots.size (); ++i) {
        echo_slot_t &slot = state->slots[i];
        if (!slot.handle)
            continue;
        short events = ZLINK_POLLIN;
        if (slot.send_pending && slot.send_enabled)
            events = static_cast<short> (events | ZLINK_POLLOUT);
        if (slot.poller_events != events) {
            if (zlink_poller_modify (state->poller, slot.handle, events) != 0) {
                echo_mark_fatal (state, zlink_errno ());
                return false;
            }
            slot.poller_events = events;
        }
    }

    bool progressed = false;
    std::vector<zlink_poller_event_t> events (state->slots.size ());
    const int poll_rc =
      zlink_poller_wait_all (state->poller,
                             events.empty () ? NULL : &events[0],
                             static_cast<int> (events.size ()),
                             timeout_ms);
    if (poll_rc < 0) {
        const int err = zlink_errno ();
        if (err != EINTR && err != EAGAIN) {
            echo_mark_fatal (state, err);
            return false;
        }
    }

    for (int i = 0; i < poll_rc; ++i) {
        echo_slot_t *slot =
          static_cast<echo_slot_t *> (events[i].user_data);
        if (!slot)
            continue;

        if ((events[i].events & ZLINK_POLLIN) != 0) {
            bool recv_progressed = false;
            if (!echo_drain_replies<Policy> (state, slot, &recv_progressed)) {
                echo_mark_fatal (state, errno);
                return false;
            }
            progressed = progressed || recv_progressed;
        }

        if ((events[i].events & ZLINK_POLLOUT) != 0
            && slot->send_pending && slot->send_enabled) {
            const send_status_t send_rc = echo_send_request<Policy> (slot);
            if (send_rc == send_fatal) {
                echo_mark_fatal (state, errno);
                return false;
            }
            progressed = progressed || send_rc == send_ok;
        }
    }

    if (progressed_out)
        *progressed_out = progressed;
    return !state->fatal.load (std::memory_order_acquire);
}

// ─── template: phase control ─────────────────────────────────────

template <typename Policy>
inline bool echo_start_phase_requests (echo_state_t *state,
                                       int timeout_ms)
{
    if (!state)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (1, timeout_ms));

    while (std::chrono::steady_clock::now () < deadline) {
        bool all_started = true;
        for (size_t i = 0; i < state->slots.size (); ++i) {
            echo_slot_t &slot = state->slots[i];
            if (!slot.handle || slot.inflight || !slot.send_enabled)
                continue;

            const send_status_t send_rc = echo_send_request<Policy> (&slot);
            if (send_rc == send_fatal) {
                echo_mark_fatal (state, errno);
                return false;
            }
            if (send_rc != send_ok)
                all_started = false;
        }

        if (all_started)
            return true;

        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - std::chrono::steady_clock::now ())
            .count ());
        if (remaining_ms <= 0)
            break;
        if (!echo_service_slots<Policy> (state, std::min (remaining_ms, 10),
                                         NULL))
            return false;
    }

    return false;
}

template <typename Policy>
inline bool echo_wait_phase_duration (echo_state_t *state, double seconds)
{
    if (!state)
        return false;
    if (seconds <= 0.0)
        return true;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
          std::chrono::duration<double> (seconds));

    while (!state->fatal.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < deadline) {
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - std::chrono::steady_clock::now ())
            .count ());
        if (remaining_ms <= 0)
            break;
        if (!echo_service_slots<Policy> (state, std::min (remaining_ms, 10),
                                         NULL))
            return false;
    }

    return !state->fatal.load (std::memory_order_acquire);
}

// ─── template: run one size case ─────────────────────────────────

template <typename Policy>
inline bool echo_run_single_size_case (
  echo_state_t *state,
  const multi_bench_settings_t &settings,
  const char *pattern,
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size)
{
    if (!state)
        return false;

    const uint32_t run_id = perf_multi_client::next_metric_run_id ();

    // ── warmup ──
    echo_reset_active_metrics (state, run_id, msg_size);
    echo_configure_phase_slots (state->slots, run_id, msg_size,
                                perf_multi_metric::phase_warmup, true);
    if (!echo_start_phase_requests<Policy> (
          state, settings.connect_ready_timeout_ms))
        return false;
    if (!echo_wait_phase_duration<Policy> (
          state, static_cast<double> (std::max (0, settings.warmup_seconds))))
        return false;

    // ── active ──
    echo_stop_phase (state->slots);
    echo_reset_active_metrics (state, run_id, msg_size);
    state->collect_active.store (true, std::memory_order_release);
    echo_configure_phase_slots (state->slots, run_id, msg_size,
                                perf_multi_metric::phase_active, true);
    if (!echo_start_phase_requests<Policy> (
          state, settings.connect_ready_timeout_ms))
        return false;

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample ();
    if (!echo_wait_phase_duration<Policy> (
          state,
          static_cast<double> (std::max (1, settings.duration_seconds))))
        return false;

    state->collect_active.store (false, std::memory_order_release);
    echo_stop_phase (state->slots);

    // ── collect metrics ──
    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    const unsigned long long active_received =
      state->active_received.load (std::memory_order_acquire);

    unsigned long long latency_count = 0;
    double latency_sum_us = 0.0;
    std::vector<double> latency_samples;
    for (size_t i = 0; i < state->slots.size (); ++i) {
        latency_count += state->slots[i].latency.count ();
        latency_sum_us += state->slots[i].latency.sum_us ();
        state->slots[i].latency.append_samples (&latency_samples);
    }

    if (state->fatal.load (std::memory_order_acquire) || active_received == 0
        || latency_count == 0)
        return false;

    bench_latency_stats_t latency;
    latency.mean_us =
      latency_sum_us / static_cast<double> (latency_count);
    if (latency_samples.empty ()) {
        latency.p95_us = latency.mean_us;
        latency.p99_us = latency.mean_us;
    } else {
        std::sort (latency_samples.begin (), latency_samples.end ());
        latency.p95_us = echo_percentile_from_sorted (latency_samples, 0.95);
        latency.p99_us = echo_percentile_from_sorted (latency_samples, 0.99);
    }

    const double throughput =
      static_cast<double> (active_received)
      / static_cast<double> (std::max (1, settings.duration_seconds));
    perf_multi_client::print_client_result_lines (
      pattern, lib_name, transport, msg_size, throughput, latency, metrics);
    return true;
}

// ─── template: full benchmark loop ───────────────────────────────

template <typename Policy>
inline int echo_run_benchmark (
  echo_state_t *state,
  const multi_bench_settings_t &settings,
  const std::vector<size_t> &msg_sizes,
  const char *pattern,
  const std::string &lib_name,
  const std::string &transport)
{
    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        if (!echo_run_single_size_case<Policy> (
              state, settings, pattern, lib_name, transport, msg_sizes[si]))
            return 1;
    }
    return 0;
}

} // namespace perf_multi_echo

#endif // PERF_MULTI_ECHO_CLIENT_HPP_INCLUDED
```

---

## 5. 리팩토링 후: `perf_multi_dealer_router_client.cpp`

> 673줄 → ~90줄

```cpp
#include "../common/perf_multi_echo_client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

static const char *k_pattern = "MULTI_DEALER_ROUTER";
static const int k_client_socket_type = ZLINK_SOCKET_DEALER;

using perf_multi_client::close_client_monitors;
using perf_multi_client::create_client_sockets;
using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::resolve_case_max_msg_size;
using perf_multi_client::resolve_case_msg_sizes;
using perf_multi_client::wait_all_client_connect_ready;
using perf_multi_echo::echo_state_t;

struct dealer_policy
{
    static bool is_blocked_send_errno (int err)
    {
        return err == EAGAIN || err == EINTR
               || err == ENOTCONN || err == EHOSTUNREACH;
    }
    static int send (void *h, const zlink_routing_id_t *,
                     zlink_msg_t *p, int f)
    {
        return zlink_send (h, p, 1, f);
    }
    static int recv (void *h, zlink_routing_id_t *,
                     zlink_msg_t **p, size_t *c, int f)
    {
        return zlink_recv (h, NULL, p, c, f);
    }
};

void close_slots (echo_state_t *state, std::vector<void *> *sockets)
{
    if (state && state->poller) {
        for (size_t i = 0; i < state->slots.size (); ++i)
            if (state->slots[i].handle)
                (void) zlink_poller_remove (state->poller, state->slots[i].handle);
        zlink_poller_destroy (&state->poller);
    }
    if (sockets) {
        for (size_t i = 0; i < sockets->size (); ++i)
            if ((*sockets)[i])
                zlink_close ((*sockets)[i]);
        sockets->clear ();
    }
    if (state)
        state->slots.clear ();
}

bool create_slots (echo_state_t *state,
                   ctx_guard_t &ctx,
                   const std::string &transport,
                   const std::string &endpoint,
                   const multi_bench_settings_t &settings,
                   size_t max_payload_size,
                   std::vector<void *> *sockets_out)
{
    std::vector<ready_monitor_t> monitors;
    if (!create_client_sockets (ctx, transport, endpoint, settings,
                                k_client_socket_type, sockets_out, &monitors)) {
        close_client_monitors (&monitors);
        return false;
    }
    if (!wait_all_client_connect_ready (monitors,
                                        settings.connect_ready_timeout_ms)) {
        close_client_monitors (&monitors);
        return false;
    }
    close_client_monitors (&monitors);

    state->poller = zlink_poller_new ();
    if (!state->poller)
        return false;

    const size_t cap =
      std::max (max_payload_size, perf_multi_metric::header_size ());
    state->slots.resize (sockets_out->size ());
    for (size_t i = 0; i < sockets_out->size (); ++i) {
        perf_multi_echo::echo_slot_t &slot = state->slots[i];
        slot.handle = (*sockets_out)[i];
        slot.slot_index = i;
        slot.payload.assign (cap, 'd');
        if (zlink_poller_add (state->poller, slot.handle, &slot, ZLINK_POLLIN)
            != 0)
            return false;
        slot.poller_events = ZLINK_POLLIN;
    }
    return !state->slots.empty ();
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint;
    if (!parse_endpoint_arg (argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    set_perf_multi_pattern_env (k_pattern);
    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available (transport))
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);
    const size_t max_msg_size =
      resolve_case_max_msg_size (fallback_size, msg_sizes);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    echo_state_t state;
    std::vector<void *> sockets;
    if (!create_slots (&state, ctx, transport, endpoint, settings,
                       max_msg_size, &sockets)) {
        close_slots (&state, &sockets);
        return 1;
    }

    const int rc = perf_multi_echo::echo_run_benchmark<dealer_policy> (
      &state, settings, msg_sizes, k_pattern, lib_name, transport);
    close_slots (&state, &sockets);
    return rc;
}
```

---

## 6. 리팩토링 후: `perf_multi_router_router_client.cpp`

> 701줄 → ~120줄. dealer_router와 차이: `zlink_send_rid`, `target_routing_id` 설정.

```cpp
#include "../common/perf_multi_echo_client.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

static const char *k_pattern = "MULTI_ROUTER_ROUTER";
static const int k_client_socket_type = ZLINK_SOCKET_ROUTER;
static const char *k_server_routing_id = "SERVER";

using perf_multi_client::close_client_monitors;
using perf_multi_client::create_client_sockets;
using perf_multi_client::is_supported_transport;
using perf_multi_client::make_routing_id;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::resolve_case_max_msg_size;
using perf_multi_client::resolve_case_msg_sizes;
using perf_multi_client::wait_all_client_connect_ready;
using perf_multi_echo::echo_state_t;

struct router_policy
{
    static bool is_blocked_send_errno (int err)
    {
        return err == EAGAIN || err == EINTR
               || err == ENOTCONN || err == EHOSTUNREACH;
    }
    static int send (void *h, const zlink_routing_id_t *rid,
                     zlink_msg_t *p, int f)
    {
        return zlink_send_rid (h, rid, p, 1, f);
    }
    static int recv (void *h, zlink_routing_id_t *src,
                     zlink_msg_t **p, size_t *c, int f)
    {
        return zlink_recv (h, src, p, c, f);
    }
};

void close_slots (echo_state_t *state, std::vector<void *> *sockets)
{
    if (state && state->poller) {
        for (size_t i = 0; i < state->slots.size (); ++i)
            if (state->slots[i].handle)
                (void) zlink_poller_remove (state->poller, state->slots[i].handle);
        zlink_poller_destroy (&state->poller);
    }
    if (sockets) {
        for (size_t i = 0; i < sockets->size (); ++i)
            if ((*sockets)[i])
                zlink_close ((*sockets)[i]);
        sockets->clear ();
    }
    if (state)
        state->slots.clear ();
}

bool create_slots (echo_state_t *state,
                   ctx_guard_t &ctx,
                   const std::string &transport,
                   const std::string &endpoint,
                   const multi_bench_settings_t &settings,
                   size_t max_payload_size,
                   std::vector<void *> *sockets_out)
{
    std::vector<ready_monitor_t> monitors;
    if (!create_client_sockets (ctx, transport, endpoint, settings,
                                k_client_socket_type, sockets_out, &monitors)) {
        close_client_monitors (&monitors);
        return false;
    }
    if (!wait_all_client_connect_ready (monitors,
                                        settings.connect_ready_timeout_ms)) {
        close_client_monitors (&monitors);
        return false;
    }
    close_client_monitors (&monitors);

    state->poller = zlink_poller_new ();
    if (!state->poller)
        return false;

    zlink_routing_id_t target_rid;
    if (!make_routing_id (k_server_routing_id, &target_rid))
        return false;

    const size_t cap =
      std::max (max_payload_size, perf_multi_metric::header_size ());
    state->slots.resize (sockets_out->size ());
    for (size_t i = 0; i < sockets_out->size (); ++i) {
        perf_multi_echo::echo_slot_t &slot = state->slots[i];
        slot.handle = (*sockets_out)[i];
        slot.slot_index = i;
        slot.target_routing_id = target_rid;
        slot.payload.assign (cap, 'r');
        if (zlink_poller_add (state->poller, slot.handle, &slot, ZLINK_POLLIN)
            != 0)
            return false;
        slot.poller_events = ZLINK_POLLIN;
    }
    return !state->slots.empty ();
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint;
    if (!parse_endpoint_arg (argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    set_perf_multi_pattern_env (k_pattern);
    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available (transport))
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);
    const size_t max_msg_size =
      resolve_case_max_msg_size (fallback_size, msg_sizes);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    echo_state_t state;
    std::vector<void *> sockets;
    if (!create_slots (&state, ctx, transport, endpoint, settings,
                       max_msg_size, &sockets)) {
        close_slots (&state, &sockets);
        return 1;
    }

    const int rc = perf_multi_echo::echo_run_benchmark<router_policy> (
      &state, settings, msg_sizes, k_pattern, lib_name, transport);
    close_slots (&state, &sockets);
    return rc;
}
```

---

## 7. 리팩토링 후: `perf_multi_gateway_client.cpp`

> 821줄 → ~140줄. gateway API, service monitor, `zlink_gateway_destroy`.

```cpp
#include "../common/perf_multi_echo_client.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

static const char *k_pattern = "MULTI_GATEWAY";
static const char *k_service_name = "perf-gateway";
static const char *k_server_routing_id = "perf-gateway-server";

using perf_multi_client::is_supported_transport;
using perf_multi_client::make_routing_id;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::resolve_case_msg_sizes;
using perf_multi_echo::echo_state_t;

struct gateway_policy
{
    static bool is_blocked_send_errno (int err)
    {
        return err == EAGAIN || err == ENOTCONN || err == EHOSTUNREACH;
    }
    static int send (void *h, const zlink_routing_id_t *rid,
                     zlink_msg_t *p, int f)
    {
        return zlink_gateway_send_rid (h, rid, p, 1, f);
    }
    static int recv (void *h, zlink_routing_id_t *src,
                     zlink_msg_t **p, size_t *c, int f)
    {
        return zlink_recv (h, src, p, c, f);
    }
};

bool apply_gateway_options (void *gateway,
                            const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int sndhwm = bench_hwm_from_env ("PERF_MULTI_SNDHWM", settings.hwm);
    const int rcvhwm = bench_hwm_from_env ("PERF_MULTI_RCVHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env ("PERF_MULTI_SNDTIMEO_MS", 200);

    return zlink_set_option (gateway, ZLINK_OPT_LINGER,
                             &linger_ms, sizeof (linger_ms)) == 0
           && zlink_set_option (gateway, ZLINK_OPT_SNDHWM,
                                &sndhwm, sizeof (sndhwm)) == 0
           && zlink_set_option (gateway, ZLINK_OPT_RCVHWM,
                                &rcvhwm, sizeof (rcvhwm)) == 0
           && zlink_set_option (gateway, ZLINK_OPT_SNDTIMEO,
                                &sndtimeo_ms, sizeof (sndtimeo_ms)) == 0;
}

void close_slots (echo_state_t *state)
{
    if (!state)
        return;
    if (state->poller) {
        for (size_t i = 0; i < state->slots.size (); ++i)
            if (state->slots[i].handle)
                (void) zlink_poller_remove (state->poller, state->slots[i].handle);
        zlink_poller_destroy (&state->poller);
    }
    for (size_t i = 0; i < state->slots.size (); ++i) {
        if (state->slots[i].handle)
            zlink_gateway_destroy (&state->slots[i].handle);
    }
    state->slots.clear ();
}

bool create_slots (echo_state_t *state,
                   ctx_guard_t &ctx,
                   const std::string &transport,
                   const std::string &endpoint,
                   const multi_bench_settings_t &settings,
                   size_t max_payload_size)
{
    const size_t service_clients =
      resolve_multi_service_clients (settings.clients);
    zlink_routing_id_t server_rid;
    if (!make_routing_id (k_server_routing_id, &server_rid))
        return false;

    state->poller = zlink_poller_new ();
    if (!state->poller)
        return false;

    const size_t cap =
      std::max (max_payload_size, perf_multi_metric::header_size ());

    // reserve로 push_back 시 reallocation 방지 → poller user_data 포인터 안정
    state->slots.reserve (service_clients);

    for (size_t i = 0; i < service_clients; ++i) {
        perf_multi_echo::echo_slot_t slot;
        char rid_text[64];
        std::snprintf (rid_text, sizeof (rid_text), "gwc-%zu", i);

        slot.handle = zlink_gateway_new (ctx.get (), k_service_name);
        if (!slot.handle || !apply_gateway_options (slot.handle, settings)
            || zlink_set_routing_id (slot.handle, rid_text,
                                     std::strlen (rid_text)) != 0
            || !setup_tls_client (slot.handle, transport)) {
            if (slot.handle)
                zlink_gateway_destroy (&slot.handle);
            return false;
        }

        ready_monitor_t monitor;
        if (!open_configured_service_monitor (
              slot.handle,
              ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED
                | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
              &monitor)) {
            zlink_gateway_destroy (&slot.handle);
            return false;
        }

        if (zlink_gateway_connect (slot.handle, endpoint.c_str (),
                                   &server_rid) != 0) {
            close_ready_monitor (monitor);
            zlink_gateway_destroy (&slot.handle);
            return false;
        }

        if (!wait_for_service_monitor_event (
              monitor,
              ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED,
              ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
              settings.connect_ready_timeout_ms)) {
            close_ready_monitor (monitor);
            zlink_gateway_destroy (&slot.handle);
            return false;
        }
        close_ready_monitor (monitor);

        slot.slot_index = i;
        slot.target_routing_id = server_rid;
        slot.payload.assign (cap, 'g');
        state->slots.push_back (slot);
        perf_multi_echo::echo_slot_t &final_slot = state->slots.back ();
        if (zlink_poller_add (state->poller, final_slot.handle,
                              &final_slot, ZLINK_POLLIN) != 0) {
            zlink_gateway_destroy (&final_slot.handle);
            state->slots.pop_back ();
            return false;
        }
        final_slot.poller_events = ZLINK_POLLIN;
    }

    return !state->slots.empty ();
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint;
    if (!parse_endpoint_arg (argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    set_perf_multi_pattern_env (k_pattern);
    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available (transport))
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);
    size_t max_msg_size = fallback_size > 0 ? fallback_size : 64;
    for (size_t i = 0; i < msg_sizes.size (); ++i)
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    echo_state_t state;
    if (!create_slots (&state, ctx, transport, endpoint, settings,
                       max_msg_size)) {
        close_slots (&state);
        return 1;
    }

    const int rc = perf_multi_echo::echo_run_benchmark<gateway_policy> (
      &state, settings, msg_sizes, k_pattern, lib_name, transport);
    close_slots (&state);
    return rc;
}
```

---

## 8. 원본과의 동작 차이 (클라이언트)

### 8.1 send errno 분류 차이 — 별도 패치로 분리

원본 파일 간에 errno 분류가 미세하게 다르다:

| 패턴 | `send_blocked` 판정 errno |
|------|--------------------------|
| dealer_router (원본) | `EAGAIN, EINTR, ENOTCONN, EHOSTUNREACH` |
| router_router (원본) | `EAGAIN, EINTR, ENOTCONN, EHOSTUNREACH` |
| gateway (원본) | `EAGAIN, EHOSTUNREACH, ENOTCONN` (**EINTR 없음**) |

이 리팩토링에서는 **원본 동작을 그대로 보존**한다.
공통 helper는 blocked errno 집합을 하드코딩하지 않고
`Policy::is_blocked_send_errno()`로 위임한다. 따라서 dealer/router는
기존대로 `EINTR`를 blocked로 취급하고, gateway는 기존대로 `EINTR` 없이 유지한다.

분리 방법: gateway 리팩토링 커밋에서는 원본과 동일하게 `EAGAIN`,
`ENOTCONN`, `EHOSTUNREACH`만 blocked로 유지한다. 이후 별도 커밋에서
gateway `EINTR` 추가 + signal-interrupt 경로 검증을 수행한다.

> 이유: EINTR 추가는 기능 수정이며, 구조 리팩토링과 섞으면 회귀 원인
> 특정이 어려워진다. signal-interrupt 경로는 일반 benchmark 실행으로는
> 거의 검증되지 않으므로 전용 검증이 필요하다.

### 8.2 debug 로그 제거

원본 gateway에 있던 `g_debug_send_logs`, `g_debug_recv_logs` 기반의
throttled debug 출력은 template에 포함하지 않는다. `bench_debug_enabled()`
체크 자체가 hot path에 분기를 추가하므로 제거가 맞다. 디버깅이 필요하면
pattern 파일에서 send/recv policy에 로그를 추가할 수 있다.

### 8.3 `completed_replies` 필드 제거

원본 gateway의 `completed_replies` 카운터는 메트릭 수집에 사용되지 않고
debug 전용이므로 `echo_slot_t`에 포함하지 않는다.

### 8.4 `make_routing_id` 중복

router_router와 gateway에 있던 동일한 `make_routing_id()`는
`perf_multi_client_helpers.hpp`로 추출한다. 최종 상태에서는 pattern 파일에
동일 구현을 남기지 않는다.

### 8.5 `close_slots` 중복

dealer_router와 router_router의 `close_slots`는 동일하다.
gateway는 `zlink_gateway_destroy` 사용으로 다르다.
socket 기반 2파일의 close_slots는 공통 helper로 추출 가능하나,
각각 ~10줄이고 패턴별 cleanup이므로 현 상태를 유지한다.

---

## 9. 서버 리팩토링

### 9.1 대상 분석

서버 7파일의 core loop는 패턴별로 구조가 다르므로 클라이언트처럼
template 통합은 하지 않는다. 대상은 두 가지다.

**A. relay 서버 2파일 통합 (100% 동일 코드)**

`perf_multi_dealer_router_server.cpp`와 `perf_multi_router_router_server.cpp`는
252줄 전부 동일하고 상수 4개만 다르다:

| 상수 | dealer_router | router_router |
|------|---------------|---------------|
| `k_pattern` | `"MULTI_DEALER_ROUTER"` | `"MULTI_ROUTER_ROUTER"` |
| `k_token` | `"dealer_router"` | `"router_router"` |
| `k_server_has_routing_id` | `false` | `true` |
| `k_server_socket_type` | `ZLINK_SOCKET_ROUTER` | `ZLINK_SOCKET_ROUTER` (동일) |

**B. 유틸리티 함수 추출 (5~7파일에서 복붙)**

| 함수 | 줄 | 복붙 횟수 |
|------|---|----------|
| `request_queue_probe()` | 6 | 5 |
| `emit_requested_queue_probe()` | 16 | 5 |
| `print_server_metrics()` | ~20 | 5 |

### 9.2 relay 서버 리팩토링

상수만 다르므로 공통 구현 1개 + 패턴별 main 파일로 분리한다.

**신규: `multi/common/perf_multi_relay_server.hpp`**

```cpp
#ifndef PERF_MULTI_RELAY_SERVER_HPP_INCLUDED
#define PERF_MULTI_RELAY_SERVER_HPP_INCLUDED

#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace perf_multi_relay {

static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);

inline void request_queue_probe (size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store (msg_size, std::memory_order_release);
    g_queue_probe_pending.store (true, std::memory_order_release);
}

inline void emit_requested_queue_probe (const char *pattern,
                                        const std::string &lib_name,
                                        const std::string &transport,
                                        void *send_socket,
                                        void *recv_socket)
{
    if (!g_queue_probe_pending.exchange (false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load (std::memory_order_acquire);
    if (msg_size == 0 || !send_socket || !recv_socket)
        return;

    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (send_socket, recv_socket);
    print_server_queue_metrics (
      lib_name, pattern, transport, msg_size, queue_stats);
}

inline bool relay_router_once (void *server)
{
    zlink_routing_id_t source_rid;
    source_rid.size = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int id_len =
      ::zlink_recv (server, &source_rid, &parts, &part_count, 0);
    if (id_len < 0) {
        const int err = zlink_errno ();
        return err == EAGAIN || err == EINTR;
    }

    if (part_count == 0 || !parts) {
        zlink_msg_t empty_part;
        if (zlink_msg_init_size (&empty_part, 0) != 0)
            return false;
        const int send_rc =
          ::zlink_send_rid (server, &source_rid, &empty_part, 1, 0);
        if (send_rc >= 0)
            return true;
        const int err = zlink_errno ();
        zlink_msg_close (&empty_part);
        return err == EAGAIN || err == EINTR;
    }

    const int send_rc =
      ::zlink_send_rid (server, &source_rid, parts, part_count, 0);
    if (send_rc >= 0) {
        free (parts);
        return true;
    }

    const int err = zlink_errno ();
    if (parts) {
        zlink_multipart_close (parts, part_count);
        free (parts);
    }
    return err == EAGAIN || err == EINTR;
}

inline void print_server_metrics (
  const char *pattern,
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_multi_resource_metrics_t &metrics,
  const server_queue_stats_t &queue_stats)
{
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision (2) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision (2) << metrics.mem_mb << std::endl;
        }
        print_server_queue_metrics (
          lib_name, pattern, transport, sizes[i], queue_stats);
    }
}

inline int run_relay_server (const char *pattern,
                             const char *token,
                             zlink_socket_type_t socket_type,
                             bool has_routing_id,
                             const char *routing_id,
                             const std::string &lib_name,
                             const std::string &transport)
{
    set_perf_multi_pattern_env (pattern);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available (transport))
        return 1;

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), socket_type);
    if (!server)
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    apply_benchmark_hwm (server, settings.hwm);
    if (has_routing_id && routing_id) {
        zlink_set_routing_id (
          server, routing_id, std::strlen (routing_id));
    }

    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server, transport,
      lib_name + std::string ("_") + token + "_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    perf_stop_requested ().store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    install_perf_signal_handlers ();

    std::thread stdin_watcher ([pattern, &lib_name, &transport] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command (line, &queue_size)) {
                request_queue_probe (queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                perf_stop_requested ().store (
                  true, std::memory_order_release);
                return;
            }
        }
        perf_stop_requested ().store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample ();
    std::cout << "READY," << endpoint << std::endl;

    bool loop_ok = true;
    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        emit_requested_queue_probe (
          pattern, lib_name, transport, server, server);
        if (!relay_router_once (server)) {
            loop_ok = false;
            break;
        }
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (
      pattern, lib_name, transport, sizes, metrics, queue_stats);

    zlink_close (server);
    return loop_ok ? 0 : 1;
}

} // namespace perf_multi_relay

#endif // PERF_MULTI_RELAY_SERVER_HPP_INCLUDED
```

**리팩토링 후: `perf_multi_dealer_router_server.cpp` (252줄 → ~20줄)**

```cpp
#include "../common/perf_multi_relay_server.hpp"

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern ("MULTI_DEALER_ROUTER"))
        return 1;

    return perf_multi_relay::run_relay_server (
      "MULTI_DEALER_ROUTER",
      "dealer_router",
      ZLINK_SOCKET_ROUTER,
      false,               // no server routing_id
      "SERVER",
      argv[1],             // lib_name
      argv[2]);            // transport
}
```

**리팩토링 후: `perf_multi_router_router_server.cpp` (252줄 → ~20줄)**

```cpp
#include "../common/perf_multi_relay_server.hpp"

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern ("MULTI_ROUTER_ROUTER"))
        return 1;

    return perf_multi_relay::run_relay_server (
      "MULTI_ROUTER_ROUTER",
      "router_router",
      ZLINK_SOCKET_ROUTER,
      true,                // set server routing_id
      "SERVER",
      argv[1],             // lib_name
      argv[2]);            // transport
}
```

### 9.3 나머지 서버 (변경 없음)

gateway, dealer_dealer, pubsub, spot, stream 서버는 core loop 구조가
각각 다르므로 통합하지 않는다.

- **gateway**: pending deque 기반 backpressure + poller POLLOUT 토글
- **dealer_dealer**: 유일하게 서버 측 latency 측정
- **pubsub/spot**: one-way publish + phase-driven send
- **stream**: callback/poller 하이브리드 + frame 재조립

`request_queue_probe()`, `emit_requested_queue_probe()`,
`print_server_metrics()`는 5파일에서 복붙되어 있지만, 각각 ~6~20줄이고
파일마다 `k_pattern`을 참조하는 방식이 달라 추출 효과가 크지 않다.
성능/가독성에 영향이 없으므로 현 상태를 유지한다.

### 9.4 서버 Before vs After

| 지표 | Before | After |
|------|--------|-------|
| relay 서버 2파일 합계 | 504줄 | ~40줄 + header ~200줄 |
| relay 서버 변경 시 수정 파일 | 2 | 1 (header) |
| 나머지 서버 5파일 | 변경 없음 | 변경 없음 |

---

## 10. Before vs After (전체)

| 지표 | Before | After |
|------|--------|-------|
| echo client 3파일 합계 | 2,195줄 | ~350줄 + header ~350줄 |
| relay server 2파일 합계 | 504줄 | ~40줄 + header ~200줄 |
| 전체 대상 5파일 합계 | 2,699줄 | ~390줄 + header ~550줄 |
| echo client phase 로직 변경 시 | 3파일 수정 | 1파일 (header) |
| relay server 변경 시 | 2파일 수정 | 1파일 (header) |
| 신규 echo 패턴 추가 | ~700줄 복붙 | ~100줄 policy+create |
| 신규 relay 패턴 추가 | ~250줄 복붙 | ~20줄 main |
| 런타임 비용 | baseline | 동일 (template inline) |
| 나머지 서버 5파일 | 변경 없음 | 변경 없음 |

---

## 11. 검증 방법

### 11.1 빌드 (client + server 전체)

```bash
cmake --build core/build --target \
  comp_src_dealer_router_client \
  comp_src_router_router_client \
  comp_src_gateway_client \
  comp_src_dealer_router_server \
  comp_src_router_router_server \
  -- -j$(nproc)
```

### 11.2 relay 서버 프로세스 제어 계약 검증

wrapper 실행만으로는 READY/QUEUE/STOP 제어 경로 회귀를
놓칠 수 있으므로, server 단독 기동 시나리오를 직접 확인한다.

```bash
# bash coproc로 stdin/stdout을 동시에 제어한다.
coproc SERVER { ./core/build/bin/comp_src_dealer_router_server current tcp; }

# 1. READY 출력 확인
IFS= read -r ready_line <&"${SERVER[0]}"
printf '%s\n' "${ready_line}"
# 기대: READY,tcp://...

# 2. queue probe 요청
printf 'QUEUE,1024\n' >&"${SERVER[1]}"

# 3. queue metrics RESULT 출력 확인
while IFS= read -r line <&"${SERVER[0]}"; do
  printf '%s\n' "${line}"
  [[ "${line}" == RESULT,*snd_pending_max,* ]] && break
done

# 4. STOP → graceful shutdown 확인
printf 'STOP\n' >&"${SERVER[1]}"
wait "${SERVER_PID}"
# 기대: exit code 0
```

동일 시나리오를 `comp_src_router_router_server`에도 수행한다.

### 11.3 기능 확인 (wrapper 실행)

```bash
core/perf/run_benchmarks_multi.sh \
  --pattern MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_GATEWAY \
  --msg-sizes 64,1024 --transports tcp --runs 1
```

확인 항목:
- 모든 패턴에서 RESULT line 정상 출력
- server_cpu_pct, server_mem_mb RESULT line 포함
- `status=complete` (partial 아님)
- relay server 2개는 단독 제어 검증에서도 READY/QUEUE/STOP 계약 유지

### 11.4 성능 비회귀

```bash
# 리팩토링 전 기준선 (리팩토링 전에 미리 실행)
core/perf/run_benchmarks_multi.sh \
  --pattern MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_GATEWAY \
  --results-tag before

# 리팩토링 후
core/perf/run_benchmarks_multi.sh \
  --pattern MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_GATEWAY \
  --results-tag after

# throughput/latency가 before 대비 5% 이내 차이 확인
```
