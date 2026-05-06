# Final Doc Review Log

## 2026-05-06

- 날짜: 2026-05-06
- 대상: 정식 문서 반영과 3회 리뷰
- 수행한 명령:
  - 제거 API 이름, raw Actor handle 표현, 금지 표현 스캔
  - `rg -n "generation == 0.*invalid|invalid.*generation == 0|Actor HWM option|Actor 전용 HWM|zlink_spot_node_actor_request_channel_part|zlink_spot_node_actor_send_channel_part" doc/spec/core doc/spec/bindings doc/spec/sample doc/guide doc/internals README.md`
  - `git diff --check -- doc/spec/core doc/spec/bindings doc/spec/sample doc/guide doc/internals doc/plan/spot-entry-transport-queues-implementation-plan.ko.md doc/plan/spot-entry-transport-queues/logs`
- 확인한 draft spec 절: Public C API 변경, Actor ref, Actor join/leave,
  STREAM session과 Actor 연결, Queue/Fanout, 비목표
- 발견한 문제:
  - 1차 리뷰에서 dotnet binding spec의 dispatch subject 설명이 old handle 표현을
    남기고 있어 `Actor ref`로 수정했다.
  - core service spec 영어 문서와 internals 한국어 문서의 부정 설명이 stale scan에
    걸려 표현을 더 직접적으로 바꿨다.
  - 2차와 3차 리뷰에서 제거 API 이름, 금지 표현, `void *actor` 표현은 나오지 않았다.
  - guide의 Actor HWM 결과는 “Actor 전용 HWM이 없다”는 설명이라 draft와 일치한다.
- 수정한 파일:
  - `doc/spec/bindings/dotnet/README.md`
  - `doc/spec/core/service/spot.md`
  - `doc/internals/spot-internals.ko.md`
  - `doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
  - `doc/plan/spot-entry-transport-queues/logs/final-doc-review-log.ko.md`
- 검증 결과:
  - 3회 문서 리뷰 완료.
  - guide, internals, core spec 문서가 최종 core 코드와 일치한다.
  - stale API 이름과 제거 대상 API 설명이 정식 문서에 남아 있지 않다.
  - 문서 diff whitespace check 통과.
- 남은 위험:
  - Python/Rust 등 언어별 binding spec 상세 반영은 bindings 순차 적용 gate에서 다시
    5회 비교 리뷰한다.
- 다음 확인: core release와 GitHub Actions 확인

## 2026-05-06 Bindings 이후 문서 리뷰 종료 확인

- 날짜: 2026-05-06
- 대상: bindings spec 상세 반영 이후 3회 리뷰
- 수행한 명령:
  - Rust binding spec과 관련 로그의 금지 표현 스캔
  - `rg -n "zlink_actor_destroy\\(|zlink_actor_get_ref\\(|zlink_actor_join_spot\\(|zlink_actor_leave_spot\\(|zlink_actor_recv_part\\(|zlink_spot_node_destroy_remote_actor\\(|destroy_remote_actor|send_bound_session_packet|ActorChannel|RequestActorChannel|SendActorChannel" bindings/rust/src bindings/rust/tests bindings/rust/samples doc/spec/bindings/rust/README.md`
  - `git diff --check -- bindings/rust/src/ffi.rs bindings/rust/src/service.rs bindings/rust/tests/service_surface_tests.rs bindings/rust/samples/actor_room_server_sample.rs bindings/rust/samples/actor_gateway_relay_sample.rs bindings/rust/samples/actor_single_player_queue_sample.rs doc/spec/bindings/rust/README.md doc/plan/spot-entry-transport-queues/logs/bindings-spec-review-log.ko.md doc/plan/spot-entry-transport-queues/logs/bindings-posd-refactor-log.ko.md doc/plan/spot-entry-transport-queues/logs/sample-perf-smoke-log.ko.md doc/plan/spot-entry-transport-queues-implementation-plan.ko.md`
- 확인한 draft spec 절: Actor ref, unchecked remote ref, Entry Spot, STREAM bind, dispatch subject lifetime, 제거 API, 비목표
- 발견한 문제:
  - Rust binding spec에 오래된 Actor join info와 dispatch enum 요약이 남아 있어 정식 계약에 맞춰 갱신했다.
  - 1차 리뷰: Rust binding spec의 Actor public surface와 제거 API 잔존 여부 확인.
  - 2차 리뷰: 금지 표현과 `generation == 0` invalid 설명이 없음을 확인.
  - 3차 리뷰: diff whitespace check와 sample/perf log 연결을 확인.
- 수정한 파일:
  - `doc/spec/bindings/rust/README.md`
  - `doc/plan/spot-entry-transport-queues/logs/final-doc-review-log.ko.md`
- 검증 결과:
  - bindings spec 상세 반영 이후 3회 문서 리뷰 완료.
  - Python/Rust 포함 언어별 binding spec 5회 비교 리뷰가 완료됐다.
  - 제거 API 이름과 Actor 전용 channel API는 Rust public surface와 Rust binding spec에서 발견되지 않았다.
- 남은 위험: 없음
- 다음 확인: 최종 종료 절차
