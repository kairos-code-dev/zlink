# POSD Perf Refactoring Progress Tracker

> Last updated: 2026-04-06 17:40 KST

## Summary

| # | Target | 빌드 | 스모크 | 문서 리뷰 | baseline | Status |
|---|--------|------|-------|----------|---------|--------|
| 1 | **core/perf** | ✅ single+multi | ✅ 25 RESULT | ✅ 8/8 | ✅ 포맷 정상 | **DONE** |
| 2 | **bindings/cpp/perf** | ✅ single+multi | ✅ RESULT+파일 | ✅ 8/8 | ✅ 포맷 정상 | **DONE** |
| 3 | **bindings/dotnet/perf** | ✅ 0err 0warn | ✅ RESULT 5줄 | ✅ 5/5 | ✅ 포맷 정상 | **DONE** |
| 4 | **bindings/go/perf** | ✅ go build | ✅ RESULT 5줄 | ✅ 완료정의 충족 | ✅ 포맷 정상 | **DONE** |
| 5 | **bindings/java/perf** | ✅ gradle | ✅ RESULT 5줄 | ✅ 완료정의 충족 | ✅ 포맷 정상 | **DONE** |
| 6 | **bindings/node/perf** | ✅ JS 실행 | ✅ 180 RESULT | ✅ 4/4 | ✅ full smoke | **DONE** |
| 7 | **bindings/python/perf** | ✅ import+callback smoke | ✅ RESULT 5줄 | ✅ 완료정의 충족 | ✅ 포맷 정상 | **DONE** |
| 8 | **bindings/rust/perf** | ✅ cargo check | ✅ 14 RESULT | ✅ 완료정의 충족 | ✅ 포맷 정상 | **DONE** |

**ALL 8 TARGETS COMPLETE.**

---

## Completion Details

### core/perf ✅
- Steps 1-5: internal API 제거, sleep 제거, snapshot polling 제거, mutex 제거, phase 이름 정리
- Step 6: bench_common.hpp 400줄 이하 달성, runtime 로직은 `bench_common_runtime.hpp`로 분리
- Step 7: send dedup (perf_classify_send_result helper), TLS 분리 (perf_tls_setup.hpp)
- single_phase_drain_timeout_ms → single_phase_completion_timeout_ms rename 완료
- latency_mutex hot path에서 제거
- run_comparison.py PUBSUB callback only 확인
- 빌드: single 6타겟 + multi 7타겟 전체 통과
- 스모크: run_benchmarks.sh → 25 RESULT lines, status=complete

### cpp/perf ✅
- 공통 헤더 3개 (perf_tls.hpp, perf_latency_sampler.hpp, perf_monitor_wait.hpp) 연결 완료
- phase_drain 삭제, settle 삭제, drain_warmup_replies 삭제
- socket_guard_t dedup (common/perf_socket_compat.hpp → using 선언)
- hot path _queue_mutex → atomic SPSC 교체
- magic header 문서화 (MPF1/SPF1 설명)
- 빌드: single 6타겟 + multi 11타겟 전체 통과
- 스모크: run_benchmarks.sh → RESULT lines + 결과 파일 저장

### dotnet/perf ✅
- Zlink.BindingBench.Common 공유 프로젝트 생성
- PerfCommon 중복 제거 (TimestampUs, ComputeLatencyStats 등 → PerfShared)
- PerfTls 통합 (InternalsVisibleTo 추가)
- IPerfPattern 레지스트리 도입 (switch dispatch 제거)
- 패턴 레지스트리, hot path lock 제거, global state 캡슐화
- drainTicks → recvFlushTicks rename
- 빌드: single + multi 0 warning 0 error
- 스모크: RESULT 5줄 (SPOT), exit 0

### go/perf ✅
- 중복 심볼 5개 삭제 (StampPayload, SentAtFromBytes, SentAtFromMessage, percentile, WaitConnected)
- dead code 삭제 (Unsupported, splitDuration)
- settle pseudo-phase 삭제 (time.Sleep 200ms in pubsub.go)
- 측정 인프라 공통화 (measurement.go, ready.go, config.go)
- 빌드: `go build ./...` 통과
- 스모크: run_benchmarks.sh → RESULT 5줄, exit 0

### java/perf ✅
- 10개 패턴 파일에서 send/sendStopBurst/loop 래퍼 인라인화
- PHASE_DRAIN 삭제, dead code 삭제
- TLS centralized (PerfUtil)
- recv mode fail-fast
- 빌드: `gradle :perf-single:compileJava :perf-multi:compileJava` 통과
- 스모크: run_benchmarks.sh --pattern PAIR → RESULT 5줄, status=complete

### node/perf ✅
- 6개 single 패턴 파일에 send/recv 인라인화
- perf_single_common.ts/.js 삭제
- orphan 파일 확인: false positive (child_process.fork 런타임 참조)
- PHASE_DRAIN 삭제
- 빌드: JS 파일 로드+실행 성공
- 스모크: run_benchmarks.sh → 180 RESULT lines, status=complete, 결과 파일 저장
- 대형 메시지(262144) 재검증 완료, 결과 파일 기준 0.00 패턴 해소

### python/perf ✅
- perf_metrics.py 공통 모듈에 build_report_path 등 8개 함수 통합
- single 패턴 callback-only 정렬, Poller 기반 single 경로 제거
- multi SPOT callback smoke 재검증 완료
- PHASE_DRAIN 삭제
- drain_len32be_frames → parse_len32be_frames rename 확인
- 빌드: import 체인 통과
- 스모크: run_benchmarks.sh --pattern PAIR → RESULT 5줄, status=complete
- 스모크: run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback → status=complete

### rust/perf ✅
- PHASE_DRAIN 삭제 (single + multi), PHASE_WARMUP 재정렬
- PhaseResult 타입 + build_phase_result + print_phase_result 추가
- MetricCollector 추가, 3개 single 패턴 파일 적용
- recv mode fail-fast (validate_callback_recv_mode)
- unsafe FFI 0건
- 빌드: `cargo check` single+multi 통과
- 스모크: run_benchmarks.sh → 14 RESULT lines (전 패턴), exit 0

---

## Baseline Comparison

- RESULT line 포맷: 전 타겟 7-field CSV 정상
- 필수 메트릭 (throughput, bandwidth, latency, latency_p95, latency_p99) 전부 포함
- NaN/음수 값 없음
- 극단적 regression 없음 (50% 미만 감소 없음)
- node 대형 메시지 재검증 완료, 0.00 패턴 없음

## Known Issues

현재 기준 blocking known issues 없음.

과거 감사 기록은 아래 문서에 보존한다.

- [AUDIT_UNFINISHED_2026-04-06.md](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/AUDIT_UNFINISHED_2026-04-06.md)
- [INCOMPLETE_AUDIT_20260406.md](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/INCOMPLETE_AUDIT_20260406.md)

---

## Iteration Tracking

```
Round 1 (initial refactoring)     ✅ Complete - all 8 targets
Codex review R1                   ✅ Complete - found major gaps
Round 2 (fix FAIL items)          ✅ Complete - fixes applied
Codex review R2                   ✅ Complete - found remaining gaps
Round 3 (build verification)      ✅ Complete - 8/8 build pass
Round 4 (smoke test)              ✅ Complete - 8/8 smoke pass
Round 5 (doc review)              ✅ Complete - 8/8 plan criteria verified
Round 6 (baseline comparison)     ✅ Complete - format valid, no regression
```
