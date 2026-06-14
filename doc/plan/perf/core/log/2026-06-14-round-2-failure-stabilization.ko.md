# 라운드 2: multi perf 실패 안정화

- goal: current 문제 report의 남은 실패 그룹을 현재 core/build runtime에서 재현하거나 실패 0개 targeted 기준으로 분리한다.
- 완료 기준: `MULTI_SPOT_REQREP ws/wss`, `MULTI_SPOT_SENDSEND ws/wss`, `MULTI_STREAM ws 1024B+`의 targeted perf 실패 여부를 확인하고, 실패가 재현되면 core 원인을 추적한다. 실패가 재현되지 않으면 full multi 전 단계의 실패 안정화 증거로 기록한다.
- 시작 시각: 2026-06-14 15:12:00 +0900
- 기준 commit: `82175d004`
- 시작 git status: `bindings/cpp/src/Runtime/Service/*` 변경과 round-1 로그 변경이 있음. core 소스 변경 없음.
- 기준 report: `bindings/c/perf/baseline/perf_c_multi_linux_20260513_101034.txt`
- 비교 report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260614_103936.txt`
- 대상 pattern/transport/size:
  - `MULTI_SPOT_REQREP`, `MULTI_SPOT_SENDSEND` / `ws,wss` / 전체 size
  - `MULTI_STREAM` / `ws` / `1024B+`

## 가설

- 가설 1: current 문제 report의 SPOT 계열 ws/wss 실패는 core runtime 문제가 아니라 stale runtime 또는 일시적 readiness 상태였고, 현재 `core/build` runtime에서는 재현되지 않는다.
- 가설 2: SPOT_REQREP/SENDSEND의 ws/wss 실패는 SPOT one-way와 다른 request/reply local dispatch 또는 router-channel readiness 경로 문제라서 64B 단건 성공만으로 전체 size 성공을 보장하지 않는다.
- 가설 3: STREAM ws 1024B+ 실패는 SPOT 실패와 독립적인 WebSocket stream frame 재조립 또는 routing-id echo 경로 문제이며, 전체 64B 성능 개선보다 먼저 별도 실패 수정이 필요하다.
- 선택한 가설: 먼저 가설 1을 current runtime targeted perf로 검증한다. 재현되면 해당 pattern의 core call path를 추적하고, 재현되지 않으면 실패 0개 기준선 후보로 기록한다.

## 기존 증거

- `MULTI_SPOT ws/wss` 전체 size targeted perf는 현재 runtime에서 success 12, fail 0, status complete.
- `MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND ws 64B` 과거 targeted report `perf_c_multi_linux_20260614_113921_final_spot_reqrep_sendsend_ws_core_fix.txt`는 success 2, fail 0, status complete였으나 commit이 `e24e9d695`로 현재 HEAD와 다르다.
- `MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND wss 64B` 과거 targeted report `perf_c_multi_linux_20260614_114509_final_spot_wss_core_fix.txt`는 success 3, fail 0, status complete였으나 commit이 `e24e9d695`로 현재 HEAD와 다르다.
- `MULTI_STREAM ws 64,256,1024` 과거 targeted report `perf_c_multi_linux_20260614_114221_final_stream_ws_core_fix_retry2.txt`는 success 3, fail 0, status complete였으나 전체 size와 현재 HEAD를 아직 확인하지 않았다.

## 읽은 코드

- 예정: 실패가 재현되는 pattern의 core path를 우선 읽는다.

## 변경

- 변경 파일: 없음
- 변경 이유: 실패 재현 확인 전이다.
- perf 전용 변경이 아닌 이유: perf 코드를 수정하지 않는다.

## 보안 하드닝 보존 확인

- 참조 report: `core/doc/report/odl/2026-06-13-core-src-security-review.ko.md`
- 이번 변경이 건드린 보안 항목: 없음
- 보안 의미를 유지한 근거: 아직 코드 변경 전이다.
- 추가로 실행한 회귀 테스트: 없음

## 검증

- build: targeted perf runner가 `core/build/lib/libzlink.so.6.0.4`를 사용함을 확인했다.
- test: 미실행
- targeted perf:
  - `PERF_FAIL_FAST=1 bindings/c/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND --transports ws,wss --duration 5`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.4`
    - result: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260614_151524.txt`
    - completion: success 24, fail 0, status complete
  - `PERF_FAIL_FAST=1 bindings/c/perf/run_benchmarks_multi.sh --reuse-build --pattern MULTI_STREAM --transports ws --duration 5`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.6.0.4`
    - result: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260614_151826.txt`
    - completion: success 6, fail 0, status complete
- full perf: 미실행

## 결과

- 실패 안정화: current 문제 report의 실패 그룹 중 `MULTI_SPOT ws/wss`, `MULTI_SPOT_REQREP ws/wss`, `MULTI_SPOT_SENDSEND ws/wss`, `MULTI_STREAM ws`는 현재 runtime targeted perf에서 모두 fail 0으로 끝났다.
- 목표 달성 여부: full multi perf 실패 0개는 아직 미확인이라 미달성이다.

## 다음 작업

- full 또는 축소 full perf를 실행해 실패 0개 기준선을 승격한다.
- 실패 0개가 확인되면 새 current report 기준으로 64B 회귀 수치를 다시 계산한다.
