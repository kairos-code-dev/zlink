# 2026-07-11 C++ bindings 성능 개선 라운드

이 문서는 core 9.0.0 기준 C++ bindings의 pattern별 paired 측정과 성능 개선 판단을
기록한다. 완료되지 않은 report와 다른 pattern의 수치는 판정에 사용하지 않는다.

## 재현 환경

- source: `8340fbc088a43b6ced006341ae732dffdefa211f`, dirty 작업 트리
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

## Single PUBSUB 진행 기록

### tcp 기준 측정 의미 수정

최초 tcp paired 측정에서는 C가 active publish에 `DONTWAIT`를 사용하고 C++은 blocking
publish를 사용했다. Single 정책은 연속 blocking send와 socket HWM의 자연스러운
backpressure를 요구하므로 같은 동작을 비교한 결과가 아니었다. 실제 중앙값도 64B에서
C 180.46 Kmsg/s, C++ 962.76 Kmsg/s로 비정상적으로 벌어졌고, 이 결과는 판정에서
제외했다.

POSD 관점에서 확인한 위험 신호는 정책에 정의된 송신 방식이 pattern 코드의 flag 선택과
중복되어 서로 달라진 점이다. 다음 두 방안을 비교했다.

1. C++을 `DONTWAIT` 재시도로 바꾸면 기존 C 수치와 가까워질 수 있지만, 잘못된 기준을
   binding에 복제하고 Single 정책의 책임 경계를 흐리므로 폐기했다.
2. C PUBSUB을 정책대로 blocking publish로 고치면 HWM이 backpressure를 담당하고 C와
   C++이 같은 의미를 측정한다. 공개 API나 특수 우회를 추가하지 않으므로 이 방안을
   선택했다.

수정 뒤 CPU 고정 5회 paired 측정의 중앙값은 다음과 같다.

| Size | C throughput | C++ throughput | 비율 | 최대 latency 비율 |
|------|--------------|----------------|------|-------------------|
| 64 | 1,101,111.0 | 967,475.2 | 87.9% | 0.99배 |
| 1024 | 895,080.4 | 781,915.8 | 87.4% | 0.95배 |
| 65536 | 65,074.2 | 57,588.8 | 88.5% | 1.15배 |

처리량과 latency 비율은 C++ 단순 one-way 목표를 만족했다. 그러나 p99 변동 폭은 C가
각각 61.6%, 10.7%, 37.0%였고 C++이 32.8%, 30.9%, 24.2%였다. 64B 단독 재측정과
CPU 집합 진단에서도 간헐적인 40ms대 host pause가 남았다. 처리량 변동은 작았지만 p99
변동 기준을 만족하지 못했으므로 tcp 셀은 아직 통과로 기록하지 않는다. CPU 집합을 넓히는
runner 후보도 문제를 없애지 못해 채택하지 않았다.

판정에 사용한 report는 다음과 같다.

- C: `perf_c_single_linux_20260711_142234_core_9_0_cpp_pubsub_tcp_blocking_baseline_20260711.txt`
- C++: `perf_cpp_single_linux_20260711_142406_core_9_0_cpp_pubsub_tcp_blocking_compare_20260711.txt`
- C 64B 변동 확인: `perf_c_single_linux_20260711_142623_core_9_0_cpp_pubsub_tcp64_stability_20260711.txt`
- C 64B 두 번째 변동 확인: `perf_c_single_linux_20260711_143556_core_9_0_cpp_pubsub_tcp64_stability_retry2_20260711.txt`

네 report는 모두 `status: complete`이고 runtime은
`core/build/lib/libzlink.so.9.0.0`, auto-HWM profile은 `balanced`, I/O thread는 1,
timeout은 200ms로 일치한다.

두 번째 64B 공식 재시도도 throughput 변동 폭은 0.3%였지만 p99는
29.227~42.666ms로 변동 폭 33.6%였다. C 기준이 먼저 변동 gate를 넘지 못했으므로
이 직후 C++ 측정은 실행하지 않았다.

### 현재 판정

- Single `PUBSUB`: 진행 중
- tcp throughput과 latency 비율: 목표 만족
- tcp 변동성: 미달, 통과 처리하지 않음
- C++ binding 변경: 없음
- 기준 측정 수정 커밋: `77d180588` (`main` push 완료)
- 다음 측정: host p99가 안정된 시점에 tcp 64B를 C와 C++ 순서로 다시 측정
