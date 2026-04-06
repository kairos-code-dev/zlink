# perf-refactor 미완료 감사 결과

검토 일시: 2026-04-06

> 이 문서는 2026-04-06 초기 감사본이다.
> 현재 최신 결론은 [PROGRESS.md](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/PROGRESS.md)와
> [INCOMPLETE_AUDIT_2026-04-06.md](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/INCOMPLETE_AUDIT_2026-04-06.md)를 따른다.
> 현재 상태: all 8 targets complete, 현재 기준 미완료 없음.

검토 범위:
- `doc/plan/perf-refactor/*.md`
- 관련 구현/산출물 (`core/perf`, `bindings/*/perf`)

검토 방식:
- 문서의 `완료 정의`, `완료 상태`, `PROGRESS.md` 완료 표기를 실제 코드/결과 파일과 대조
- 문서 체크만 보지 않고 관련 소스와 결과 산출물을 직접 확인
- 전체 빌드/스모크 재실행은 하지 않았고, 현재 워크스페이스의 코드와 저장된 산출물 기준으로 판정

이후 수정으로 현재 상태는 모두 완료되었으며, 아래 내용은 초기 감사 시점의 기록으로만 보관한다.

## 미완료 항목 목록

### 1. core/perf: `bench_common.hpp` 400줄 이하 목표 미달

- 문서 근거:
  - `core-perf-posd-refactor-plan.ko.md:564`
    - 완료 정의: ``bench_common.hpp`가 400줄 이하로 축소됨`
  - `core-perf-posd-refactor-plan.ko.md:578`
    - 완료 상태: `bench_common.hpp 562줄로 분리`
- 실제 코드:
  - `core/perf/single/common/bench_common.hpp` 현재 562줄
- 판정:
  - 완료 정의 불충족
  - 완료 상태 문장도 스스로 목표 미달 상태를 인정하고 있음
- 확인 근거:
  - `wc -l core/perf/single/common/bench_common.hpp` 결과: `562`

### 2. bindings/cpp/perf: `settle 삭제` 완료 아님

- 문서 근거:
  - `bindings-cpp-perf-posd-refactor-plan.ko.md:344`
    - 완료 상태: `phase_drain/settle/drain_warmup 삭제`
- 실제 코드:
  - `bindings/cpp/perf/multi/src/perf_spot_client.cpp:98`
    - `bool wait_for_spot_ready_settle (int timeout_ms_)`
  - `bindings/cpp/perf/multi/src/perf_spot_client.cpp:494-496`
    - `wait_for_spot_ready_settle(PERF_MULTI_SPOT_READY_SETTLE_MS)`
- 판정:
  - `settle 삭제` 완료 표기는 사실과 다름
  - 이름만 남은 정도가 아니라 실제 호출 경로가 살아 있음

### 3. bindings/node/perf: `전체 패턴/전체 사이즈 정상 동작` 완료 아님

- 문서 근거:
  - `bindings-node-perf-posd-refactor-plan.ko.md:173`
    - 완료 정의: `전체 패턴/전체 사이즈 정상 동작`
  - `PROGRESS.md:75-76`
    - `status=complete`와 동시에 `대형 메시지(262144) 일부 패턴에서 throughput 0`
  - `PROGRESS.md:107`
    - Known Issue: `PUBSUB/ROUTER_ROUTER/SPOT 262144 크기에서 0.00`
- 실제 산출물:
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:29`
    - PUBSUB 262144B throughput `0.00`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:59`
    - ROUTER_ROUTER 262144B throughput `0.00`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:67-69`
    - SPOT 65536B/131072B/262144B throughput `0.00`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:128-132`
    - `RESULT,current,PUBSUB,...,262144,...,0.00`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:218-222`
    - `RESULT,current,ROUTER_ROUTER,...,262144,...,0.00`
  - `bindings/node/perf/results/single/report/perf_linux_callback_20260406_160711.txt:238-252`
    - `RESULT,current,SPOT,...,65536/131072/262144,...,0.00`
- 판정:
  - 완료 정의 불충족
  - `DONE`/`status=complete` 표기는 유지할 수 없음

### 4. bindings/python/perf: `single callback-only` 정렬 완료 아님

- 문서 근거:
  - `bindings-python-perf-posd-refactor-plan.ko.md:177`
    - 완료 정의: `single은 callback-only 모델로 정렬됨 (poller 기반 single 경로 제거)`
  - `bindings-python-perf-posd-refactor-plan.ko.md:192`
    - 완료 상태: `패턴 파일은 Poller 방식 유지(on_receive segfault 회피)`
  - `PROGRESS.md:80`
    - `패턴 파일은 원본 Poller 방식 유지`
- 실제 코드:
  - `bindings/python/perf/single/perf_pair.py:33-60`
    - `with zlink.Poller() as poller:` 이후 poll 기반 recv 루프 유지
  - 동일 패턴이 `perf_pubsub.py`, `perf_dealer_router.py`, `perf_spot.py` 등 single 패턴에 남아 있음
- 판정:
  - 완료 정의 불충족
  - 완료 상태 문구도 완료 정의와 정면 충돌

### 5. bindings/python/perf: `perf_common.py / perf_multi_common.py 유틸리티 중복 0건` 완료 아님

- 문서 근거:
  - `bindings-python-perf-posd-refactor-plan.ko.md:175`
    - 완료 정의: ``perf_common.py` / `perf_multi_common.py` 유틸리티 중복 0건`
- 실제 코드 중복:
  - `safe_poll`
    - `bindings/python/perf/single/perf_common.py:190`
    - `bindings/python/perf/multi/perf_multi_common.py:113`
  - `wait_connected_pair`
    - `bindings/python/perf/single/perf_common.py:170`
    - `bindings/python/perf/multi/perf_multi_common.py:100`
  - `tcp_endpoint`
    - `bindings/python/perf/single/perf_common.py:97`
    - `bindings/python/perf/multi/perf_multi_common.py:79`
  - `new_payload`
    - `bindings/python/perf/single/perf_common.py:105`
    - `bindings/python/perf/multi/perf_multi_common.py:87`
  - `stamp_payload`
    - `bindings/python/perf/single/perf_common.py:109`
    - `bindings/python/perf/multi/perf_multi_common.py:91`
  - 추가로 `latency_us_from_message`, `result_metrics`, `print_result_lines`, `build_report_path`는 `perf_metrics.py`와 `single/perf_common.py`에 중복 유지
- 판정:
  - 완료 정의 불충족
  - `8개 함수 통합`은 일부만 수행된 상태

### 6. dotnet/perf: `drainTicks -> recvFlushTicks rename` 완료 표기 불일치

- 문서 근거:
  - `PROGRESS.md:49`
    - `drainTicks → recvFlushTicks rename`
- 실제 코드:
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfPair.cs:72`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfPubSub.cs:109`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerDealer.cs:72`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfDealerRouter.cs:71`
  - `bindings/dotnet/perf/single/Zlink.BindingBench/src/PerfRouterRouter.cs:108`
    - 모두 `drainTicks` 그대로 사용
  - 반면 `PerfSpot.cs`만 `recvFlushTicks` 사용
- 판정:
  - 계획 문서의 완료 정의 자체 위반으로 보긴 어렵지만,
  - `PROGRESS.md` 완료 항목 서술은 코드 기준 사실과 다름

### 7. go/rust: 결과 파일의 `Effective Options (result)` 후반 섹션 누락

- 문서 근거:
  - `PROGRESS.md:108`
    - Known Issue: `rust/go Effective Options (result) 누락`
- 실제 산출물:
  - Go:
    - `bindings/go/perf/results/single/report/perf_linux_callback_20260406_160300.txt:1-18`
    - `## Effective Options (start)`와 `end`만 있고 `result` 섹션 없음
  - Rust:
    - `bindings/rust/perf/results/single/report/perf_linux_callback_20260406_160301.txt:1-30`
    - `## Effective Options (start)`만 있고 `end`/`result` 섹션 없음
- 판정:
  - `PROGRESS.md`가 이미 미완료로 적어둔 항목이며, 산출물로 재확인됨

## 결론

이 문서는 초기 감사 시점의 미완료 판정 기록이다.
현재 워크스페이스 기준의 최신 상태는 [PROGRESS.md](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/PROGRESS.md)에 반영되어 있으며,
현재 기준 미완료 항목은 없다.

초기 감사에서 확인된 미완료/불일치 항목은 다음 7건이었다.

1. core/perf: `bench_common.hpp <= 400` 미달
2. bindings/cpp/perf: `settle 삭제` 미완료
3. bindings/node/perf: 전체 패턴/전체 사이즈 정상 동작 미달
4. bindings/python/perf: single callback-only 정렬 미완료
5. bindings/python/perf: single/multi 유틸리티 중복 0건 미달
6. dotnet/perf: `drainTicks -> recvFlushTicks` 완료 표기 불일치
7. go/rust: 결과 파일 `Effective Options (result)` 누락

반대로, 이번 확인 범위에서 위 항목 외에 `java`, `go`, `rust`의 완료 정의 본문과 직접 충돌하는 추가 증거는 찾지 못했다.
다만 이 문서는 현재 상태가 아니라 초기 감사본이다.
