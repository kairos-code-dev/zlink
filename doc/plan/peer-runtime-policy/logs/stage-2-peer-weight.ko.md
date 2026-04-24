# 단계 2 Peer Weight 로그

- 기록 시각: 2026-04-24T17:31:34+09:00
- 기준 commit: db1c01f9f029190065ca1d7be188fde2bc59a4bb
- 읽은 초안 section: 1 목적, 3 범위, 5 공개 동작 계약, 6 옵션/이벤트/스냅샷 계약, 테스트 항목, POSD 리뷰 초점, 문서 반영 목록

## 구현 체크리스트
- 완료: public weight 옵션 추가, admission 공개 surface 제거, monitor/service event rename, discovery/registry/Spot peer cache weight 전파, DEALER weighted outbound, runtime weight 변경 전파, tests, docs

## 테스트 실패 처리 기록
- `core/src/sockets/socket_base_api.cpp`에서 incomplete type으로 `socket_discovery_attachment_t` member 호출이 깨졌다. `services/discovery/socket_discovery_attachment.hpp` include를 추가해 수정했다.
- 초기 weighted integration test에서 drain helper가 느려 timeout이 났다. helper를 `ZLINK_DONTWAIT` + 짧은 polling 간격 기반으로 바꿔 재실행했고 통과했다.
- `DEALER -> DEALER` ratio test warmup이 peer ready 이전 send와 겹쳐 간헐적으로 실패했다. warmup send를 재시도 루프로 바꿔 안정화했다.
- perf helper 빌드 중 `Clock skew detected. Your build may be incomplete.` 경고가 한 번 나왔지만, runner가 `core/build/lib/libzlink.so.5.3.4`를 출력했고 benchmark run/report 생성은 정상 완료됐다. 경고는 기록만 남기고 결과는 유효로 처리했다.

## 전체 테스트 결과
- `ctest -L unittest`: 통과, 20/20
- `ctest -L integration`: 통과, 60/60
- `ctest -L e2e`: 통과, 2/2
- `ctest -L regression`: 통과, 16/16

## POSD 리뷰
- `lb_t`가 remote weight, positive active set, weighted schedule dirty state를 소유하도록 정리했다.
- unequal positive weight일 때만 weighted schedule을 재구성하고, send path마다 전체 pipe scan을 반복하지 않는다.
- one-pipe fast path와 equal-weight round-robin fast path를 유지했다.
- multipart atomicity는 `_weighted_multipart_pipe`로 보존된다.
- public admission compatibility surface는 제거하고, 공개 계약은 option/event/snapshot의 weight 기준으로 통일했다.
- 남은 구조 문제 없음.

## perf smoke
- 사전 rebuild: `cmake --build core/build -j$(nproc)` 통과.
- 공통 single: `./bindings/c/perf/run_benchmarks.sh --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1`
  - 통과, report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_170741.txt`
- 공통 multi SPOT_REQREP: `./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 통과, runtime: `core/build/lib/libzlink.so.5.3.4`, report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_170746.txt`
- 공통 multi SPOT_SENDSEND: `./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 통과, runtime: `core/build/lib/libzlink.so.5.3.4`, report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_170752.txt`
- stage 2 single: `./bindings/c/perf/run_benchmarks.sh --pattern DEALER_DEALER,DEALER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --duration 1`
  - 통과, report: `bindings/c/perf/results/single/report/perf_c_single_linux_20260424_170755.txt`
- stage 2 multi: `./bindings/c/perf/run_benchmarks_multi.sh --pattern DEALER_DEALER,DEALER_ROUTER,SPOT_REQREP,SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 2 --duration 1`
  - 통과, runtime: `core/build/lib/libzlink.so.5.3.4`, report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260424_170802.txt`

## 문서 반영
- `doc/spec/core/socket/{README,router,dealer}.{ko.md,md}`
- `doc/spec/core/{errno-map,events,monitoring}.{ko.md,md}`
- `doc/spec/core/service/{discovery,registry}.{ko.md,md}`
- `doc/spec/bindings/README.md` 및 각 언어 binding README
- `doc/guide/{03-3-dealer,03-4-router,06-monitoring,07-0-services}.{ko.md,md}`
- `doc/internals/{services-internals,spot-internals}.{ko.md,md}`
- `doc/site/docs/` 대응 문서

## 문서 검색
- `rg -n "zlink_set_admission_state|zlink_get_admission_state|ZLINK_ADMISSION_|PEER_ADMISSION_CHANGED|PeerAdmissionChanged|AdmissionState|admission_state" doc/spec doc/guide doc/internals doc/site/docs doc/spec/bindings`
  - 결과 없음
- `rg -n "admission guard" doc/spec doc/guide doc/internals doc/site/docs`
  - 내부 lifecycle 용어만 남아 있음. stage 2 제거 대상 아님.

## 커밋과 push
- 기능 커밋, push 결과는 stage 종료 시 별도 commit으로 기록한다.
