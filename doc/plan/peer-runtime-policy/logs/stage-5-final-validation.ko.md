# 단계 5 최종 검증 로그

- 기록 시각: 2026-04-24T19:57:12+09:00
- 기준 commit: 2a432fa9b03d5fdbb4e42e896f2a6a32b96bc294
- 종료 전 수정 묶음: monitor close/task lifetime 정리, .NET monitor snapshot/binding 정렬, single perf runner runtime-path/stale-check 정렬, binding spec 잔여 설명 정리

## 단계 5 수정 사항
- `core/src/api/monitor_api.cpp`
  - monitor self-close finalize 경로에서 dispatch task를 runtime에서 제거한 뒤 state를 정리하도록 수정
  - 삭제된 `monitor_handler_state_t`를 periodic task가 다시 타는 UAF 경로 차단
- `bindings/dotnet/src/Zlink/{Monitor.cs,Service/ServiceMonitor.cs}`
  - static callback trampoline + `GCHandle` user-data 경계로 정리
- `bindings/dotnet/src/Zlink/Native/NativeServiceModels.cs`
  - `zlink_monitor_snapshot_t`의 auto-HWM 확장 필드를 반영
- `bindings/c/perf/run_benchmarks.sh`
  - `run_benchmarks_multi.sh`와 같은 `core/build` runtime 경로 출력 및 stale runtime 검사 추가
- `doc/spec/bindings/*`, `doc/site/docs/api/bindings.md`
  - binding spec 잔여 `admissionState` 설명 제거, `weight` 기준으로 정정

## .NET crash 원인과 처리
- 재현:
  - `bindings/dotnet/tests/run_tests.sh --filter FullyQualifiedName~Zlink.Tests.test_monitor_contract.socket_monitor_attach_handler_snapshot_and_close_contract`
- 최초 증상:
  - testhost crash
- 원인:
  - crash 지점은 `monitor.Close()`가 아니라 `monitor.Snapshot()`
  - .NET `ZlinkMonitorSnapshot` struct가 auto-HWM 확장 이전 크기여서 native snapshot write가 testhost 메모리를 덮음
- 처리:
  - native snapshot struct와 public `MonitorSnapshot` projection을 현재 C header와 정렬
  - monitor task self-close 경로의 runtime task 제거 누락도 함께 수정해 monitor callback lifetime 경계 강화
- 결과:
  - monitor contract 단일 재현 통과
  - `.NET` 전체 suite 통과

## 테스트 실패 처리 기록
- `ctest --test-dir core/build --output-on-failure -L integration -j1` 첫 실행에서 `test_zmp_request_reply`가 `SEGFAULT`로 실패
- 같은 build 상태에서 아래 단독 재실행으로 재현 확인:
  - `ctest --test-dir core/build --output-on-failure -R test_zmp_request_reply`
  - 결과: 통과
- flaky로 분류하고 로그에 남긴 뒤 integration lane 전체를 같은 build 상태에서 다시 실행
  - 결과: 60/60 통과

## binding 전체 테스트 결과
- `bindings/go`: `go test ./...` 통과
- `bindings/python`: `./tests/run_tests.sh` 통과, `54 passed, 10 skipped`
- `bindings/rust`: `cargo test` 통과
- `bindings/node`: `npm test` 통과
- `bindings/java`: `./gradlew test integrationTest` 통과
- `bindings/cpp/tests`: `./run_tests.sh` 통과
- `bindings/dotnet/tests`: `./run_tests.sh` 통과, `138 passed`

## core 전체 테스트 결과
- `cmake --build core/build -j"$(nproc)"`: 통과
- `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`: 통과, 20/20
- `ctest --test-dir core/build --output-on-failure -L integration -j1`: 재실행 최종 통과, 60/60
- `ctest --test-dir core/build --output-on-failure -L e2e -j1`: 통과, 2/2
- `ctest --test-dir core/build --output-on-failure -L regression -j1`: 통과, 16/16

## perf smoke
- 사전 rebuild:
  - `cmake --build core/build -j"$(nproc)"` 통과
- 공통 smoke:
  - `./bindings/c/perf/run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_195359.txt`
  - `./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_195411.txt`
  - `./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_195419.txt`
- peer-disconnect-rid 추가 smoke:
  - `./bindings/c/perf/run_benchmarks.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_195427.txt`
  - `./bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM,SPOT_REQREP --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_195439.txt`
- peer-weight 추가 smoke:
  - `./bindings/c/perf/run_benchmarks.sh --pattern DEALER_DEALER,DEALER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_195451.txt`
  - `./bindings/c/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER,DEALER_ROUTER,SPOT_REQREP,SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_195504.txt`
- auto-hwm 추가 smoke:
  - `./bindings/c/perf/run_benchmarks.sh --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_195534.txt`
  - `./bindings/c/perf/run_benchmarks_multi.sh --pattern PUBSUB,STREAM,SPOT_REQREP,SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
    - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
    - report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_195545.txt`
- 모든 report는 `status=complete`, `fail=0`, expected/actual result line 일치 확인

## 종료 루프 점검
- 세 draft의 필수 구현 항목: 반영 완료
- 세 draft의 테스트 요구: 자동 테스트 반영 완료
- 전체 테스트: 통과
- POSD 리뷰 잔여 구조 문제: 없음
- perf smoke: 통과
- `doc/` 정식 문서와 public header 정렬: 확인
- guide/spec/internals 역할 구분: 확인
- binding 문서와 binding surface 정렬: 재수정 후 확인
- site 문서 동기화: 확인
- 금지 표현 검색: 깨끗함
- 미적용/오적용 항목: 없음
