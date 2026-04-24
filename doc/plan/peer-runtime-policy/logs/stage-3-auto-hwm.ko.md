# 단계 3 자동 HWM 로그

- 기록 시각: 2026-04-24T18:38:04+09:00
- 기준 commit: 1158aa4583ca0403e4d19fe92a87d1f2a4ba3d45
- feature commit: 58a644cd4c04595a77f940af14a2dd6301f66b16 (`core: add automatic hwm policy`)
- 읽은 초안 section: 1 목적, 2 범위, 4 기본 원칙, 5 개념 모델, 6 역할 묶음, 7 일반 socket 정책, 8 spot/spotnode 매핑, 9 monitoring 진단, 테스트 항목, POSD 리뷰 초점, 문서 반영 목록

## 구현 체크리스트
- 완료: context auto HWM option 2종 추가, 기본값 `enable=1`, `budget=128MB`
- 완료: queue/transport/reserve `60/30/10`, effective message bytes `1280`
- 완료: 역할 묶음 `control`, `routed`, `fanout`, `recv_ingress`
- 완료: raw socket 기본 역할 매핑과 Spot/SpotNode 내부 소켓 역할 override
- 완료: 수동 `SNDHWM` / `RCVHWM` / `SNDBUF` / `RCVBUF` 우선
- 완료: 기존 pipe/new pipe HWM 갱신, 새 transport 연결 buffer 적용
- 완료: monitor snapshot budget/buffer 진단 필드 추가
- 완료: 관련 integration/unittest 갱신

## 테스트 실패 처리 기록
- 초기 POSD 정리에서 `socket_base_t::refresh_auto_hwm_policy()` 안에 public-api lock scope를 넣었다가 teardown 경합으로 여러 integration binary가 hang/timeout 났다.
- timeout이 관찰된 binary:
  - `test_monitor_socket_contract`
  - `test_monitor_perf_contract`
  - `test_socket_with_handler`
  - `test_multi_socket_contract_regressions`
  - `test_backpressure_matrix`
- 원인 확인 뒤 public-api lock을 제거하고, attached pipe count 조회와 attached pipe HWM refresh 경계만 남겼다.
- 수정 후 위 binary들을 단독 재실행해 통과를 확인했고, 전체 integration lane도 다시 통과했다.
- perf smoke 첫 시도는 `run_benchmarks.sh`와 `run_benchmarks_multi.sh`를 같은 `bindings/c/build`에서 병렬 실행해 실패했다.
  - single runner: `libzlink_c.so` symlink already exists 오류
  - multi runner: 기대한 `libzlink_c.so.1.0.0` target 누락 오류
- `bindings/c/build`를 지우고 perf smoke를 순차 실행하도록 바꿔 재실행했고 통과했다.

## 전체 테스트 결과
- `cmake --build core/build -j"$(nproc)"`: 통과
- `ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"`: 통과, 20/20
- `ctest --test-dir core/build --output-on-failure -L integration -j1`: 통과, 60/60
- `ctest --test-dir core/build --output-on-failure -L e2e -j1`: 통과, 2/2
- `ctest --test-dir core/build --output-on-failure -L regression -j1`: 통과, 16/16

## POSD 리뷰
- 계산식은 `core/src/core/auto_hwm_policy.{hpp,cpp}`로 분리해 socket hot path에서 떼어냈다.
- context option, 역할 매핑, monitor projection이 같은 private plan struct를 읽되 직접 서로를 건드리지 않게 정리했다.
- Spot/SpotNode 내부 소켓 역할 매핑은 runtime 설정 지점에 모아 퍼진 상수를 제거했다.
- HWM 적용과 transport buffer 적용 시점 차이는 socket base 내부 갱신 경계로 드러냈다.
- teardown hang을 만든 public-api lock 재진입은 제거했다.
- 남은 구조 문제 없음.

## perf smoke
- 사전 rebuild: `cmake --build core/build -j"$(nproc)"` 통과
- 실패 시도:
  - 병렬 single: `./bindings/c/perf/run_benchmarks.sh --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --duration 1`
  - 병렬 multi: `./bindings/c/perf/run_benchmarks_multi.sh --pattern PUBSUB,STREAM,SPOT_REQREP,SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 결과: `bindings/c/build` 경합으로 실패
- 재실행 1:
  - 명령: `rm -rf bindings/c/build && ./bindings/c/perf/run_benchmarks.sh --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --duration 1`
  - 결과: 통과
  - report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_180551.txt`
- 재실행 2:
  - 명령: `./bindings/c/perf/run_benchmarks_multi.sh --pattern PUBSUB,STREAM,SPOT_REQREP,SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 결과: 통과
  - runtime: `/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so.5.3.4`
  - report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_180649.txt`

## 문서 반영
- `doc/spec/core/{context,monitoring}.{ko.md,md}`
- `doc/spec/core/socket/{dealer,router,pub,sub,stream}.{ko.md,md}`
- `doc/spec/core/service/spot.{ko.md,md}`
- `doc/spec/bindings/{cpp,dotnet,go,java,node,python,rust}/README.md`
- `doc/guide/{02-core-api,03-1-pair,03-2-pubsub,03-3-dealer,03-4-router,03-5-stream,06-monitoring,10-performance,12-socket-options}.{ko.md,md}`
- `doc/internals/{socket-option-defaults,services-internals,spot-internals,stream-socket}.{ko.md,md}`
- `doc/site/docs/` 대응 api/guide/internals 문서

## 문서 검색
- `rg -n -f /tmp/zlink-doc-forbidden-terms.txt doc`
  - 결과 없음

## 커밋과 push
- feature commit: `58a644cd` (`core: add automatic hwm policy`)
- push: 성공, `main` -> `github.com-kairos:kairos-code-dev/zlink.git`, `1158aa45..58a644cd`
