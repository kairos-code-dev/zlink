# 2026-07-11 C++ bindings 성능 개선 라운드

이 문서는 core 9.0.0 기준 C++ bindings의 pattern별 paired 측정과 성능 개선 판단을
기록한다. 완료되지 않은 report와 다른 pattern의 수치는 판정에 사용하지 않는다.

## 재현 환경

- source: `41246081f08a463adc8a3ed637ec7ab84d076641`, dirty 작업 트리
- core runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.9.0.0`
- host: WSL2 Linux `6.6.87.2-microsoft-standard-WSL2`, x86_64
- CPU: Intel Core Ultra 7 265K, 논리 CPU 20개
- memory: 94 GiB
- toolchain: GCC 13.3.0, CMake 3.28.3, Python 3.12.3
- CPU governor: WSL2에서 governor 파일을 제공하지 않아 기록할 수 없음
- 측정 중 별도 perf process: 없음

## Single PAIR

### 측정 조건

- pattern: `PAIR`
- message size: 64, 256, 1024, 65536, 131072, 262144 bytes
- transport: tcp, tls, ws, wss, inproc, ipc
- duration: 5초
- 기본 판정: 3회 중앙값
- secure transport와 변동 셀 최종 판정: CPU 고정 5회 중앙값
- auto-HWM profile: balanced
- I/O thread: 1
- send/receive timeout: 200ms

### C 대비 결과

아래 비율은 C++ throughput을 가까운 시점에 측정한 C throughput으로 나눈 값이다. 괄호 안은
latency mean, p95, p99 가운데 가장 큰 C 대비 비율이다.

| Transport | 64 | 256 | 1024 | 65536 | 131072 | 262144 |
|-----------|----|-----|------|-------|--------|--------|
| tcp | 99.9% (0.71배) | 100.0% (0.84배) | 99.8% (1.01배) | 100.0% (1.03배) | 100.1% (1.06배) | 99.8% (1.14배) |
| tls | 99.4% (1.00배) | 98.5% (1.03배) | 100.5% (1.00배) | 99.8% (1.01배) | 97.8% (1.05배) | 99.6% (1.00배) |
| ws | 100.0% (0.86배) | 100.0% (0.96배) | 100.4% (0.73배) | 100.0% (1.02배) | 100.2% (1.24배) | 100.0% (1.01배) |
| wss | 100.6% (0.99배) | 99.9% (1.00배) | 96.8% (1.05배) | 98.2% (1.02배) | 97.3% (1.05배) | 95.1% (1.08배) |
| inproc | 90.6% (0.65배) | 93.9% (1.11배) | 89.2% (1.07배) | 99.8% (1.04배) | 100.1% (1.16배) | 100.3% (0.97배) |
| ipc | 100.5% (0.74배) | 100.3% (0.93배) | 95.3% (1.08배) | 99.8% (0.98배) | 100.0% (1.33배) | 100.0% (1.04배) |

모든 셀이 C++ 단순 one-way 최소 목표 85%와 latency 상한 2배를 만족했다. 최초 tcp 1024B
3회 측정에서는 C++ p95가 C의 2.62배였지만 CPU 고정 5회 재측정에서는 1.01배로
안정됐다. wss 131072B의 첫 5회 C 측정은 throughput 변동 폭이 16.0%였으므로 판정에
사용하지 않았다. 같은 셀을 단독으로 다시 측정한 결과 C 5.2%, C++ 7.2%로 변동 기준을
만족했고 throughput은 97.3%였다.

### Report

`perf_c_...` 파일은 `bindings/c/perf/results/single/report/`에 있고 `perf_cpp_...` 파일은
`bindings/cpp/perf/results/single/report/`에 있다.

- tcp C/C++:
  - `perf_c_single_linux_20260711_133702_core_9_0_cpp_pair_tcp_paired_20260711.txt`
  - `perf_cpp_single_linux_20260711_133834_core_9_0_cpp_pair_tcp_paired_20260711.txt`
- tcp 1024B 최종 5회 C/C++:
  - `perf_c_single_linux_20260711_134011_core_9_0_cpp_pair_tcp1024_final_20260711.txt`
  - `perf_cpp_single_linux_20260711_134040_core_9_0_cpp_pair_tcp1024_final_20260711.txt`
- tls 최종 5회 C/C++:
  - `perf_c_single_linux_20260711_135702_core_9_0_cpp_pair_tls_final_20260711.txt`
  - `perf_cpp_single_linux_20260711_135934_core_9_0_cpp_pair_tls_final_20260711.txt`
- ws C/C++:
  - `perf_c_single_linux_20260711_134420_core_9_0_cpp_pair_ws_paired_20260711.txt`
  - `perf_cpp_single_linux_20260711_134553_core_9_0_cpp_pair_ws_paired_20260711.txt`
- wss 최종 5회 C/C++:
  - `perf_c_single_linux_20260711_140209_core_9_0_cpp_pair_wss_final_20260711.txt`
  - `perf_cpp_single_linux_20260711_140439_core_9_0_cpp_pair_wss_final_20260711.txt`
- wss 131072B 안정성 보강 C/C++:
  - `perf_c_single_linux_20260711_140720_core_9_0_cpp_pair_wss131072_stability_20260711.txt`
  - `perf_cpp_single_linux_20260711_140756_core_9_0_cpp_pair_wss131072_stability_20260711.txt`
- inproc C/C++:
  - `perf_c_single_linux_20260711_135041_core_9_0_cpp_pair_inproc_paired_20260711.txt`
  - `perf_cpp_single_linux_20260711_135214_core_9_0_cpp_pair_inproc_paired_20260711.txt`
- ipc C/C++:
  - `perf_c_single_linux_20260711_135351_core_9_0_cpp_pair_ipc_paired_20260711.txt`
  - `perf_cpp_single_linux_20260711_135523_core_9_0_cpp_pair_ipc_paired_20260711.txt`

모든 report는 `status: complete`이고 C와 C++의 Effective Options와 auto-HWM detail이
일치한다.

기능 회귀 확인은 다음 명령으로 통과했다.

```bash
ctest --test-dir bindings/cpp/build \
  -R '^sample_smoke_sample_cpp_pair_recv_sample$' \
  --output-on-failure
```

### Resource와 POSD 판단

tcp 64B 대표 실행에서 C process는 CPU 최대 194.0%, 최대 `nlwp=6`이었고 C++ process는
관찰 시 CPU 188%, `nlwp=6`이었다. PAIR은 한 process 안에서 sender와 receiver를 실행한다.

최초 측정부터 모든 throughput 셀이 목표를 만족했으므로 C++ binding 코드를 변경하지 않았다.
새 helper, 특수 분기, public API 또는 perf 전용 우회를 추가하지 않았으며 POSD 위험 신호도
새로 만들지 않았다. 측정 가능한 개선 필요가 없으므로 개선 후보 설계와 커밋은 수행하지
않는다.

### 판정

- Single `PAIR`: 완료
- 코드 변경: 없음
- 성능 개선 커밋과 푸시: 해당 없음
- 다음 pattern: Single `PUBSUB`

## Single PUBSUB

### 측정 조건과 perf 의미 정합화

- pattern: `PUBSUB`
- message size: 64, 256, 1024, 65536, 131072, 262144 bytes
- transport: tcp, tls, ws, wss, inproc, ipc
- duration: 5초
- 기본 판정: 3회 중앙값
- secure transport, 목표 경계, 고변동 셀: 5회 중앙값
- CPU pin: 사용하지 않음
- auto-HWM profile: balanced
- I/O thread: 1

C PUBSUB은 active 구간에서 `ZLINK_DONTWAIT`를 사용하고 `EAGAIN`을 재시도하고 있었다.
이는 Single 정책의 blocking send와 socket HWM backpressure 의미와 달랐다. C perf의 send를
blocking으로 맞추고 `EINTR`만 재시도하도록 수정했다. C++ perf는 이미 같은 의미였으므로
수정하지 않았다.

### C 대비 최종 throughput

아래 표는 throughput 비율이다. 평균 latency도 별도로 비교했으며 p95와 p99는 진단
자료로만 보존하고 통과 판정에는 사용하지 않았다.

| Transport | 64 | 256 | 1024 | 65536 | 131072 | 262144 |
|-----------|----|-----|------|-------|--------|--------|
| tcp | 95.7% | 95.2% | 101.7% | 109.2% | 99.4% | 91.4% |
| tls | 91.9% | 95.0% | 101.6% | 98.4% | 99.5% | 99.0% |
| ws | 95.3% | 91.2% | 100.7% | 96.4% | 93.0% | 97.9% |
| wss | 95.2% | 92.6% | 97.4% | 96.8% | 100.0% | 100.6% |
| inproc | 101.8% | 95.0% | 92.4% | 85.2% | 105.9% | 125.4% |
| ipc | 94.3% | 95.4% | 95.2% | 94.2% | 97.8% | 94.6% |

평균 latency의 최대 비율은 inproc 1.36배, ipc 1.06배였고 tcp, tls, ws, wss도 C++ 상한
2배 이내였다. 모든 transport와 size의 throughput이 단순 one-way 최소 목표 85%를
만족했다.

### 병목과 POSD 판단

최초 inproc 측정에서 C++의 131072B와 262144B throughput은 C의 66.0%와 18.7%였다.
직접 계측한 C++ `message_t::from`의 할당과 복사는 262144B에서 메시지당 28.3us였고,
C의 같은 구간은 6.2us였다. submit과 latency stamp 비용 차이는 작았으므로 binding의 대형
메시지 저장소 할당을 병목으로 판정했다.

두 가지 방안을 검토했다.

1. 전역 allocator를 jemalloc으로 강제하면 probe 수치는 회복되지만 애플리케이션 전체의
   allocator와 배포 정책을 binding 밖으로 노출한다.
2. `message_t` 공개 API는 유지하고 Messaging 구현 안에서 대형 저장소만 제한적으로
   재사용하면 호출자 복잡도를 늘리지 않고 병목 지식을 한 모듈에 가둘 수 있다.

두 번째 방안을 선택했다. 저장소 풀은 총 8MiB로 제한하고 정확히 같은 크기의 block만
재사용한다. 64KiB까지 재사용하면 ipc 65536B가 72.3%로 낮아졌으므로 실제 병목이 확인된
128KiB 이상으로 범위를 좁혔다. 확정된 hot path와 회귀 근거는 구현 주석으로 남겼다.

### 최종 report

report의 공통 위치는 C가 `bindings/c/perf/results/single/report/`, C++가
`bindings/cpp/perf/results/single/report/`다.

- tcp: `perf_c_single_linux_20260711_151703_core_9_0_cpp_pubsub_tcp_nopin_policy_aligned_20260711.txt`, `perf_cpp_single_linux_20260711_151852_core_9_0_cpp_pubsub_tcp_nopin_policy_aligned_20260711.txt`
- tcp 131072B: `perf_c_single_linux_20260711_152048_core_9_0_cpp_pubsub_tcp131072_nopin_boundary_20260711.txt`, `perf_cpp_single_linux_20260711_152131_core_9_0_cpp_pubsub_tcp131072_nopin_boundary_20260711.txt`
- tls: `perf_c_single_linux_20260711_152332_core_9_0_cpp_pubsub_tls_nopin_policy_aligned_20260711.txt`, `perf_cpp_single_linux_20260711_152519_core_9_0_cpp_pubsub_tls_nopin_policy_aligned_20260711.txt`
- ws: `perf_c_single_linux_20260711_153039_core_9_0_cpp_pubsub_ws_nopin_policy_aligned_20260711.txt`, `perf_cpp_single_linux_20260711_153228_core_9_0_cpp_pubsub_ws_nopin_policy_aligned_20260711.txt`
- wss: `perf_c_single_linux_20260711_153604_core_9_0_cpp_pubsub_wss_nopin_policy_aligned_20260711.txt`, `perf_cpp_single_linux_20260711_153753_core_9_0_cpp_pubsub_wss_nopin_policy_aligned_20260711.txt`
- inproc 전체: `perf_c_single_linux_20260711_163231_core_9_0_cpp_pubsub_inproc_pool_full_final_20260711.txt`, `perf_cpp_single_linux_20260711_163527_core_9_0_cpp_pubsub_inproc_pool_full_final_20260711.txt`
- inproc 65536B 최종: `perf_c_single_linux_20260711_164419_core_9_0_cpp_pubsub_inproc65536_pool_boundary_final_20260711.txt`, `perf_cpp_single_linux_20260711_164451_core_9_0_cpp_pubsub_inproc65536_pool_boundary_final_20260711.txt`
- ipc: `perf_c_single_linux_20260711_163826_core_9_0_cpp_pubsub_ipc_nopin_final_20260711.txt`, `perf_cpp_single_linux_20260711_164045_core_9_0_cpp_pubsub_ipc_nopin_final_20260711.txt`
- ipc 65536B 최종: `perf_c_single_linux_20260711_164255_core_9_0_cpp_pubsub_ipc65536_pool_boundary_final_20260711.txt`, `perf_cpp_single_linux_20260711_164334_core_9_0_cpp_pubsub_ipc65536_pool_boundary_final_20260711.txt`

최종 코드로 `test_cpp_contract_message`, `test_cpp_contract_behavior`,
`test_cpp_contract_socket`, `sample_smoke_sample_cpp_pubsub_recv_sample`을 다시 빌드해 모두
통과했다. message 계약 테스트에는 262144B owned storage를 해제하고 다시 할당한 뒤 copy의
양 끝 payload를 확인하는 회귀 항목을 추가했다. 같은 테스트의 Valgrind full leak check도
오류 없이 통과했다.

### 판정

- Single `PUBSUB`: 완료
- C perf 변경: Single 정책과 다른 nonblocking send 의미를 blocking send로 정합화
- C++ binding 변경: 128KiB 이상, 1MiB 이하 owned message storage의 8MiB 제한 재사용
- 다음 pattern: Single `DEALER_DEALER`

## Single DEALER_DEALER

### 측정 조건과 결과

- source: `99c58f4d0d3e97a1f85b7cbdb6941375be9d53a3`
- message size: 64, 256, 1024, 65536, 131072, 262144 bytes
- transport: tcp, tls, ws, wss, inproc, ipc
- duration: 5초
- tcp, ws, inproc, ipc: 3회 중앙값
- tls, wss: 5회 중앙값
- CPU pin: 사용하지 않음

아래 표는 C++ throughput을 가까운 시점의 C throughput으로 나눈 값이다.

| Transport | 64 | 256 | 1024 | 65536 | 131072 | 262144 |
|-----------|----|-----|------|-------|--------|--------|
| tcp | 99.9% | 99.8% | 100.0% | 99.9% | 100.0% | 99.8% |
| tls | 100.0% | 99.9% | 97.9% | 96.8% | 99.7% | 99.7% |
| ws | 100.0% | 99.9% | 95.1% | 100.0% | 100.1% | 100.0% |
| wss | 100.0% | 99.5% | 98.4% | 98.8% | 99.5% | 100.7% |
| inproc | 89.0% | 100.3% | 90.4% | 99.9% | 100.7% | 100.0% |
| ipc | 100.1% | 100.1% | 93.5% | 99.9% | 100.1% | 100.0% |

모든 throughput 셀이 단순 one-way 최소 목표 85%를 만족했다. 평균 latency의 transport별
최대 비율은 tcp 1.22배, tls 1.15배, ws 1.22배, wss 1.01배, inproc 1.06배,
ipc 1.20배로 모두 C++ 상한 2배 이내였다.

### Report와 회귀 확인

report의 공통 위치는 C가 `bindings/c/perf/results/single/report/`, C++가
`bindings/cpp/perf/results/single/report/`다.

- tcp: `perf_c_single_linux_20260711_165321_core_9_0_cpp_dealer_dealer_tcp_nopin_paired_20260711.txt`, `perf_cpp_single_linux_20260711_165452_core_9_0_cpp_dealer_dealer_tcp_nopin_paired_20260711.txt`
- tls: `perf_c_single_linux_20260711_165709_core_9_0_cpp_dealer_dealer_tls_nopin_final_20260711.txt`, `perf_cpp_single_linux_20260711_165940_core_9_0_cpp_dealer_dealer_tls_nopin_final_20260711.txt`
- ws: `perf_c_single_linux_20260711_170217_core_9_0_cpp_dealer_dealer_ws_nopin_paired_20260711.txt`, `perf_cpp_single_linux_20260711_170350_core_9_0_cpp_dealer_dealer_ws_nopin_paired_20260711.txt`
- wss: `perf_c_single_linux_20260711_170553_core_9_0_cpp_dealer_dealer_wss_nopin_final_20260711.txt`, `perf_cpp_single_linux_20260711_170819_core_9_0_cpp_dealer_dealer_wss_nopin_final_20260711.txt`
- inproc: `perf_c_single_linux_20260711_171107_core_9_0_cpp_dealer_dealer_inproc_nopin_paired_20260711.txt`, `perf_cpp_single_linux_20260711_171251_core_9_0_cpp_dealer_dealer_inproc_nopin_paired_20260711.txt`
- ipc: `perf_c_single_linux_20260711_171438_core_9_0_cpp_dealer_dealer_ipc_nopin_paired_20260711.txt`, `perf_cpp_single_linux_20260711_171612_core_9_0_cpp_dealer_dealer_ipc_nopin_paired_20260711.txt`

모든 report는 `status: complete`이고 Effective Options와 auto-HWM detail이 C와 C++에서
일치한다. `test_cpp_contract_message`, `test_cpp_contract_socket`,
`test_cpp_contract_behavior`도 최종 코드로 다시 실행해 통과했다.

### POSD 판단과 판정

최초 paired 측정에서 모든 셀이 목표를 만족했다. 추가 helper, pattern 전용 분기, public API,
perf 우회가 필요하지 않았으며 binding 코드를 변경하지 않았다. PUBSUB에서 확정한 대형 메시지
저장소 hot path 최적화가 다른 one-way pattern에서도 회귀 없이 유지됐다.

- Single `DEALER_DEALER`: 완료
- 코드 변경: 없음
- 다음 pattern: Single `DEALER_ROUTER`

## Single DEALER_ROUTER

### 최초 측정과 측정 의미 확인

- source: `0ac653692`
- message size: 64, 256, 1024, 65536, 131072, 262144 bytes
- transport: tcp, tls, ws, wss, inproc, ipc
- duration: 5초
- 기본 판정: 3회 중앙값
- secure transport와 변동 셀: 5회 중앙값
- CPU pin: 사용하지 않음

최초 tcp paired 측정에서 65536B, 131072B, 262144B의 C++ throughput은 C의 58.3%,
45.0%, 45.2%였다. C++ binding의 공개 경계를 raw C 호출과 교차한 진단에서는 두 경로가
메시지당 약 33~37us로 같았고, C와 C++의 메시지 할당·복사 단독 진단도 약 3.5us로 같았다.
따라서 binding 내부 allocation이나 send 경계는 이 pattern의 병목이 아니었다.

C perf는 재사용 payload 전체를 채운 뒤 매 메시지마다 새 message에 전체 payload를 복사한다.
기존 C++ perf는 새 message를 할당한 뒤 metric header만 쓰고 나머지 payload에는 쓰지 않았다.
이는 같은 message size를 전송하더라도 page touch와 copy 의미가 달라 C 대비 binding 비용을
측정한다는 정책에 맞지 않았다. C++ perf가 재사용 payload를 채우고 기존
`message_from_payload` 경로로 전체 payload를 복사하도록 정합화했다. 이 측정 의미가 이후
리팩토링에서 사라지지 않도록 send loop에 C 기준과 full payload copy를 설명하는 주석을
남겼다.

### POSD 대안 검토

위험 신호는 성능 원인을 binding으로 단정하면 pattern 전용 allocation 분기가 범용 message
모듈에 섞일 수 있다는 점이었다. 두 가지 방향을 검토했다.

1. `message_t::allocate`와 size constructor를 core 소유 native storage로 분리하는 방안은 공개
   API를 바꾸지 않지만, 262144B probe를 45.2%에서 47.0%로만 높여 병목을 제거하지 못했다.
   이 후보는 최종 코드에서 제거했다.
2. C와 C++의 payload 생성·복사 의미를 같게 만들면 binding API와 무관한 page-touch 차이를
   제거하고 기존 공개 API와 책임 경계를 유지할 수 있다.

두 번째 방안을 선택했다. 새 public API, helper, timeout, sleep 또는 pattern 전용 binding
분기를 추가하지 않았다. PUBSUB에서 실제로 확인한 대형 owned message allocation hot path의
제한 재사용과 근거 주석은 `message.cpp` 안에 그대로 유지된다.

### C 대비 최종 throughput

아래 표는 C++ throughput을 가까운 시점의 C throughput으로 나눈 값이다. 평균 latency만
latency gate로 비교했고 p95와 p99는 진단 자료로만 보존했다.

| Transport | 64 | 256 | 1024 | 65536 | 131072 | 262144 |
|-----------|----|-----|------|-------|--------|--------|
| tcp | 91.9% | 97.9% | 92.3% | 99.0% | 100.4% | 96.1% |
| tls | 90.8% | 91.6% | 98.0% | 97.2% | 100.5% | 102.9% |
| ws | 89.5% | 90.3% | 94.2% | 95.2% | 97.8% | 99.3% |
| wss | 92.5% | 94.6% | 97.6% | 101.9% | 97.9% | 98.4% |
| inproc | 87.7% | 92.8% | 97.4% | 80.2% | 106.1% | 90.3% |
| ipc | 90.2% | 91.1% | 99.1% | 95.3% | 88.9% | 90.9% |

모든 throughput 셀이 routed one-way 최소 목표 70%를 만족했다. 평균 latency의 transport별
최대 비율은 tcp 1.03배, tls 1.15배, ws 1.57배, wss 1.27배, inproc 1.17배,
ipc 1.12배로 C++ 상한 2배 이내였다. 변동 폭이 10%를 넘었던 ws 65536B와 wss 65536B,
262144B는 CPU pin 없이 C와 C++를 각각 5회 다시 측정했다. 최종 throughput 변동 폭은
ws가 C 7.6%, C++ 3.4%, wss가 C 8.5% 이하, C++ 6.7% 이하로 안정화됐다.

### 최종 report와 회귀 확인

report의 공통 위치는 C가 `bindings/c/perf/results/single/report/`, C++가
`bindings/cpp/perf/results/single/report/`다.

- tcp: `perf_c_single_linux_20260711_174157_core_9_0_cpp_dealer_router_tcp_payload_aligned_final_20260711.txt`, `perf_cpp_single_linux_20260711_174328_core_9_0_cpp_dealer_router_tcp_payload_aligned_final_20260711.txt`
- tls: `perf_c_single_linux_20260711_174511_core_9_0_cpp_dealer_router_tls_payload_aligned_final_20260711.txt`, `perf_cpp_single_linux_20260711_174741_core_9_0_cpp_dealer_router_tls_payload_aligned_final_20260711.txt`
- ws: `perf_c_single_linux_20260711_175020_core_9_0_cpp_dealer_router_ws_payload_aligned_final_20260711.txt`, `perf_cpp_single_linux_20260711_175152_core_9_0_cpp_dealer_router_ws_payload_aligned_final_20260711.txt`
- ws 65536B 안정성: `perf_c_single_linux_20260711_180627_core_9_0_cpp_dealer_router_ws65536_payload_aligned_stability2_20260711.txt`, `perf_cpp_single_linux_20260711_180655_core_9_0_cpp_dealer_router_ws65536_payload_aligned_stability2_20260711.txt`
- wss: `perf_c_single_linux_20260711_175436_core_9_0_cpp_dealer_router_wss_payload_aligned_final_20260711.txt`, `perf_cpp_single_linux_20260711_175705_core_9_0_cpp_dealer_router_wss_payload_aligned_final_20260711.txt`
- wss 대형 안정성: `perf_c_single_linux_20260711_180721_core_9_0_cpp_dealer_router_wss_large_payload_aligned_stability2_20260711.txt`, `perf_cpp_single_linux_20260711_180814_core_9_0_cpp_dealer_router_wss_large_payload_aligned_stability2_20260711.txt`
- inproc: `perf_c_single_linux_20260711_175936_core_9_0_cpp_dealer_router_inproc_payload_aligned_final_20260711.txt`, `perf_cpp_single_linux_20260711_180114_core_9_0_cpp_dealer_router_inproc_payload_aligned_final_20260711.txt`
- ipc: `perf_c_single_linux_20260711_180309_core_9_0_cpp_dealer_router_ipc_payload_aligned_final_20260711.txt`, `perf_cpp_single_linux_20260711_180446_core_9_0_cpp_dealer_router_ipc_payload_aligned_final_20260711.txt`

`test_cpp_contract_message`, `test_cpp_contract_socket`, `test_cpp_contract_behavior`가 통과했다.
`sample_cpp_dealer_router_recv_sample` target을 빌드한 뒤
`sample_smoke_sample_cpp_dealer_router_recv_sample`도 통과했다.

### 판정

- Single `DEALER_ROUTER`: 완료
- C++ perf 변경: C와 같은 full payload copy 의미로 정합화
- C++ binding 변경: 없음
- 완료 커밋: `3643bf345`
- 다음 pattern: Single `DEALER_ROUTER_REQREP`
