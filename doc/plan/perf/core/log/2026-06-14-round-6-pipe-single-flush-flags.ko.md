# 라운드 6: pipe single flush flags 재사용 확인

- goal: DEALER/PUBSUB/SPOT 공통 64B one-way hot path에서 pipe write/flush 비용을 줄인다.
- 완료 기준: targeted 64B one-way set 중앙값 +10% 이상, 평균 +8% 이상, 관련 core test 통과, 작업 로그 작성.
- 시작 시각: 2026-06-14 16:21:00 +0900
- 기준 commit: `1886624ed`
- 시작 git status: round-5 로그만 새 파일로 있음. core 소스 변경 없음.
- 과거 기준 report: `bindings/c/perf/baseline/perf_c_multi_linux_20260513_101034.txt`
- 문제 report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260614_103936.txt`
- 실패 0 full report: `bindings/c/perf/results/multi/report/perf_c_multi_linux_20260614_151925.txt`
- 대상 pattern/transport/size: `MULTI_DEALER_DEALER,MULTI_PUBSUB,MULTI_SPOT` / `tcp,tls,ws,wss` / `64B`

## 가설

- 가설 1: `pipe_t::write_and_flush()`와 `write_and_flush_no_recursive_hwm_check()`는 이미 `more` 값을 계산한 뒤 `write_message_unlocked()`에서 flags와 routing-id를 다시 확인한다. 단일 64B 메시지가 대부분인 경로에서 이 중복을 없애면 pipe write/flush 비용이 줄 수 있다.
- 가설 2: 병목은 pipe 내부 분기보다 reader wakeup 또는 ASIO transport write 쪽에 있어, flags 재사용은 수치에 거의 영향을 주지 않을 수 있다.
- 먼저 검증할 가설: 가설 1. HWM, flush, routing-id count 의미는 그대로 두고, 이미 계산한 `more`만 재사용한다.

## 읽은 코드

- `core/src/runtime/core/pipe.cpp`: `write_and_flush()` 계열은 HWM 확인 뒤 `more`를 계산하고, `write_message_unlocked()`가 다시 `more`와 routing-id를 확인한다.
- `core/src/runtime/core/pipe.hpp`: `_msgs_written`과 `_peers_msgs_read`는 HWM과 activate-write 의미에 묶인 상태라 count 의미를 바꾸면 안 된다.
- `core/src/runtime/sockets/dealer/dealer.cpp`, `core/src/runtime/sockets/internal/lb.cpp`, `core/src/runtime/sockets/internal/dist.cpp`: one-way send가 pipe write/flush 계열을 통과한다.

## 보안 하드닝 보존 확인

- 참조 report: `core/doc/report/odl/2026-06-13-core-src-security-review.ko.md`
- 이번 변경이 건드린 보안 항목: 아직 없음
- 보안 의미를 유지한 근거: WS/WSS pending message, mtrie, 포트 파싱, IPC unlink, decoder/message/send guard, maxmsgsize 정책을 건드리지 않는다. pipe HWM과 routing-id count 의미도 유지한다.
- 추가로 실행한 회귀 테스트: 변경 후 기록한다.
