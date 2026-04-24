# 단계 1 Peer Disconnect by Routing ID 로그

- 시작 시각: 2026-04-24T15:55:05+09:00
- 기준 commit: 924f8f0a6740eb1df674749379a52c320c68c77d
- 읽은 초안 section: 1 목적, 3 범위, 5 공개 동작 계약, 6 추가 C API와 옵션, 테스트 항목, POSD 리뷰 초점, 문서 반영 목록

## 구현 체크리스트
- 완료: public API, result mapping, duplicate policy option, socket rid disconnect, SpotNode rid disconnect, tests, docs

## 테스트 실패 처리 기록
- 1차 `ctest -L integration -j1`: `test_helper_request_sequence_failure` timeout, `test_zmp_request_reply` segfault.
- 같은 build 단독 재실행: 두 테스트 모두 통과. flaky 의심으로 기록하고 integration lane 전체 재실행.
- 문서 반영 중 초안 재대조에서 rid duplicate policy 기본값이 `REJECT`여야 함을 확인했다. `options_t`와 `router_t` 초기값, 관련 테스트와 문서를 수정했다.
- targeted test를 build와 병렬 실행해 `libzlink.so.5: file too short`가 한 번 발생했다. 빌드 중 산출물을 읽은 실행 순서 문제로 보고, 빌드 완료 뒤 같은 build 상태에서 재실행해 통과했다.
- `test_peer_admission`은 기존 `ROUTER_HANDOVER=1` 기본값을 검증하고 있어 새 기본값 `0` 및 공통 duplicate policy 기본 `REJECT` 검증으로 수정했다.
- perf common multi smoke를 병렬 실행하면서 report 파일 timestamp가 겹쳤다. 결과는 통과했지만 report 경로를 명확히 남기기 위해 multi smoke를 순차 재실행했다.

## 전체 테스트 결과
- `ctest -L unittest`: 통과, 20/20
- `ctest -L integration`: 1차 flaky 실패 후 단독 재실행 통과, 기본값 테스트 수정 뒤 최종 전체 재실행 통과, 60/60
- `ctest -L e2e`: 통과, 2/2
- `ctest -L regression`: 통과, 16/16

## POSD 리뷰
- public API는 `socket_base_t::term_peer_rid()`와 `spot_node_t::disconnect_peer_pub_rid()` 경계로 분리했다.
- 일반 socket fallback은 attached pipe snapshot을 사용하고, ROUTER/STREAM은 routing map 기반 helper를 사용해 hot path에 새 순회를 추가하지 않았다.
- duplicate policy는 common option으로 추가하고 기존 ROUTER handover setter도 호환되게 유지했다.
- 남은 구조 문제 없음.

## perf smoke
- 사전 rebuild: `cmake --build core/build -j$(nproc)` 통과.
- 공통 single: `./bindings/c/perf/run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1`
  - 통과, report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_163451.txt`
- 공통 multi SPOT_REQREP: `./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 통과, runtime: `core/build/lib/libzlink.so.5.3.4`, report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_163504.txt`
- 공통 multi SPOT_SENDSEND: `./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 통과, runtime: `core/build/lib/libzlink.so.5.3.4`, report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_163507.txt`
- stage 1 single: `./bindings/c/perf/run_benchmarks.sh --pattern DEALER_ROUTER,ROUTER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --duration 1`
  - 통과, report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_163510.txt`
- stage 1 multi: `./bindings/c/perf/run_benchmarks_multi.sh --pattern STREAM,SPOT_REQREP --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 통과, runtime: `core/build/lib/libzlink.so.5.3.4`, report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_163513.txt`

## 문서 반영
- `doc/spec/core/socket/README.{ko.md,md}`
- `doc/spec/core/socket/router.{ko.md,md}`
- `doc/spec/core/service/spot.{ko.md,md}`
- `doc/spec/core/errno-map.{ko.md,md}`
- `doc/spec/bindings/README.md` 및 각 언어 binding README
- `doc/guide/03-0-socket-patterns.{ko.md,md}`
- `doc/guide/03-4-router.{ko.md,md}`
- `doc/guide/03-5-stream.{ko.md,md}`
- `doc/guide/07-3-spot.{ko.md,md}`
- `doc/guide/12-socket-options.{ko.md,md}`
- `doc/internals/peer-disconnect-rid.{ko.md,md}`
- `doc/internals/stream-socket.{ko.md,md}`
- `doc/internals/spot-internals.{ko.md,md}`
- `doc/internals/socket-option-defaults.{ko.md,md}`
- `doc/site/docs/` 대응 문서

## 문서 검색
- `/tmp/zlink-doc-forbidden-terms.txt` 생성 후 `rg -n -f /tmp/zlink-doc-forbidden-terms.txt doc`: 결과 없음.
- `rg -n "admission" doc/spec doc/guide doc/internals doc/site/docs`: 기존 admission 공개 기능과 문서 문맥 다수 확인. stage 1에서는 기존 문맥으로 유지하고, stage 2에서 제거/weight 교체 대상으로 재검토한다.

## 커밋과 push
- 기능 커밋: `7d8b3af7` (`core: add peer disconnect by routing id`)
- 단계 로그 hash 기록은 별도 follow-up commit으로 남긴다.
- push: 진행 예정
