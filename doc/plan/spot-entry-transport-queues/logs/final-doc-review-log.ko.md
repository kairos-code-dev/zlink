# Final Doc Review Log

## 2026-05-06

- 날짜: 2026-05-06
- 대상: 정식 문서 반영과 3회 리뷰
- 수행한 명령:
  - `rg -n "zlink_actor_destroy\\(|zlink_actor_get_ref\\(|zlink_actor_join_spot\\(|zlink_actor_leave_spot\\(|zlink_actor_recv_part\\(|zlink_spot_node_destroy_remote_actor\\(|Actor handle|actor handle|void \\*actor|void\\* actor|language-exchange|문서작성" doc/spec/core doc/spec/bindings doc/spec/sample doc/guide doc/internals README.md`
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
