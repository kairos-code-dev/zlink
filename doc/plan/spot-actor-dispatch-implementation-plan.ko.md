# SPOT Actor Dispatch 구현 실행 계획

## 상태

이 문서는 `doc/spec/draft/spot-entry-transport-queues.ko.md`를 실제 구현으로 옮기기 위한
Codex 에이전트 실행 계획이다. 공개 API 계약의 기준은 draft spec이며, 이 문서는
Codex 에이전트가 사용자의 추가 판단을 기다리지 않고 끝까지 진행하기 위한 작업
순서와 검증 절차를 정리한다.

현재 진행 상태:

- 2026-05-05 기준 단계 0과 단계 1은 완료 확인했다.
- core Actor 구현은 단계 2-13을 draft spec과 contract matrix 기준으로 보강 중이다.
- `test_spot_actor_dispatch` 대상 build/ctest는 STREAM Actor bind, Actor-to-session
  send, join timeout, remote create-or-get 보강 뒤 통과했다.
- 2026-05-05 전체 plan 재확인에서 true remote mesh forwarding과 remote target
  drop 검증은 아직 완료 상태가 아님을 확인했다. 해당 체크 항목은 미완료로 되돌렸다.
- core release는 `core/vX.Y.Z` tag push로 GitHub Actions에서 진행하는 경로를
  확인했지만 core 구현과 문서 gate가 끝나지 않아 아직 version bump/tag는 실행하지
  않았다.

구현 중 API 이름, enum 숫자, 내부 파일 배치는 draft spec과 충돌하지 않는 범위에서
조정할 수 있다. 조정이 필요하면 Codex 에이전트가 먼저 draft spec을 고친 다음
코드와 테스트를 맞춘다. 구현이 끝난 뒤에는 draft 내용을 정식 spec, errno 문서,
binding 문서로 나누어 반영한다.

이 계획에서 "작업자"는 항상 Codex 에이전트를 뜻한다. 사람이 중간에 별도 지시하지
않아도 Codex 에이전트는 이 문서의 순서, gate, 반복 리뷰 조건을 그대로 따라 다음
단계로 진행한다.

## 기준 문서

- 공개 계약 초안: [`doc/spec/draft/spot-entry-transport-queues.ko.md`](../spec/draft/spot-entry-transport-queues.ko.md)
- 역사적 초안 참고: [`doc/spec/draft/spot-actor-dispatch.ko.md`](../spec/draft/spot-actor-dispatch.ko.md)
- 설계 원칙: [`doc/principal/software-design-principles.md`](../principal/software-design-principles.md)
- 내부 구조 참고: [`doc/internals/spot-internals.ko.md`](../internals/spot-internals.ko.md)
- STREAM 구조 참고: [`doc/internals/stream-socket.ko.md`](../internals/stream-socket.ko.md)
- Discovery 구조 참고: [`doc/internals/discovery-internals.ko.md`](../internals/discovery-internals.ko.md)
- 오류 모델 참고: [`doc/internals/core/error-model.md`](../internals/core/error-model.md)
- 기존 공개 헤더: [`core/include/zlink.h`](../../core/include/zlink.h)
- 기존 result code: [`core/include/zlink_errno.h`](../../core/include/zlink_errno.h)

## Draft spec 참조 규칙

Codex 에이전트는 이 plan의 체크리스트만 보고 구현하면 안 된다. 각 단계에 들어가기
전에 아래 traceability 표의 draft spec 절을 반드시 열고 그 절의 계약을 기준으로
구현한다. plan의 체크리스트는 작업 순서를 나누기 위한 보조 도구일 뿐이며 세부
계약은 항상 draft spec 절이 우선한다.

아래 링크는 draft spec의 절 제목 anchor를 기준으로 한다. draft spec의 절 제목이
바뀌면 링크도 함께 갱신한다.

| plan 단계 | 반드시 확인할 draft spec 절 |
|-----------|------------------------------|
| 단계 0 | [목적](../spec/draft/spot-entry-transport-queues.ko.md#목적), [구현 순서](../spec/draft/spot-entry-transport-queues.ko.md#구현-순서), [비목표](../spec/draft/spot-entry-transport-queues.ko.md#비목표) |
| 단계 1 | [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경), [설계 원칙](../spec/draft/spot-entry-transport-queues.ko.md#설계-원칙) |
| 단계 2 | [핵심 모델](../spec/draft/spot-entry-transport-queues.ko.md#핵심-모델), [Entry Spot](../spec/draft/spot-entry-transport-queues.ko.md#entry-spot), [Actor lifecycle 의미 변경](../spec/draft/spot-entry-transport-queues.ko.md#actor-lifecycle-의미-변경) |
| 단계 3 | [Actor 생성](../spec/draft/spot-entry-transport-queues.ko.md#actor-생성), [Remote Actor create-or-get](../spec/draft/spot-entry-transport-queues.ko.md#remote-actor-create-or-get), [Actor destroy](../spec/draft/spot-entry-transport-queues.ko.md#actor-destroy) |
| 단계 4 | [Actor message 처리 위치](../spec/draft/spot-entry-transport-queues.ko.md#actor-message-처리-위치), [Dispatch event 통합](../spec/draft/spot-entry-transport-queues.ko.md#dispatch-event-통합), [Queue와 backpressure](../spec/draft/spot-entry-transport-queues.ko.md#queue와-backpressure) |
| 단계 5 | [Actor join](../spec/draft/spot-entry-transport-queues.ko.md#actor-join), [Local join process](../spec/draft/spot-entry-transport-queues.ko.md#local-join-process), [Remote join process](../spec/draft/spot-entry-transport-queues.ko.md#remote-join-process), [Actor leave](../spec/draft/spot-entry-transport-queues.ko.md#actor-leave) |
| 단계 6 | [STREAM session과 Actor 연결](../spec/draft/spot-entry-transport-queues.ko.md#stream-session과-actor-연결), [Session과 local Actor](../spec/draft/spot-entry-transport-queues.ko.md#session과-local-actor), [Session과 remote Actor](../spec/draft/spot-entry-transport-queues.ko.md#session과-remote-actor) |
| 단계 7 | [Channel router에서 Actor로 직접 messaging](../spec/draft/spot-entry-transport-queues.ko.md#channel-router에서-actor로-직접-messaging), [Actor message 처리 위치](../spec/draft/spot-entry-transport-queues.ko.md#actor-message-처리-위치) |
| 단계 8 | [STREAM session과 Actor 연결](../spec/draft/spot-entry-transport-queues.ko.md#stream-session과-actor-연결), [Gateway/session 흐름](../spec/draft/spot-entry-transport-queues.ko.md#gatewaysession-흐름) |
| 단계 9 | [Remote Actor create-or-get](../spec/draft/spot-entry-transport-queues.ko.md#remote-actor-create-or-get), [Remote join process](../spec/draft/spot-entry-transport-queues.ko.md#remote-join-process) |
| 단계 10 | [Snapshot과 monitoring](../spec/draft/spot-entry-transport-queues.ko.md#snapshot과-monitoring), [Session과 remote Actor](../spec/draft/spot-entry-transport-queues.ko.md#session과-remote-actor) |
| 단계 11 | [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경), [비목표](../spec/draft/spot-entry-transport-queues.ko.md#비목표) |
| 단계 12 | [Snapshot과 monitoring](../spec/draft/spot-entry-transport-queues.ko.md#snapshot과-monitoring) |
| 단계 13 | [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경), 각 API 상세 계약 절 |
| 단계 14 | [Actor와 Entry Spot 흐름](../spec/draft/spot-entry-transport-queues.ko.md#actor와-entry-spot-흐름), [Game room 흐름](../spec/draft/spot-entry-transport-queues.ko.md#game-room-흐름), [Single-player 흐름](../spec/draft/spot-entry-transport-queues.ko.md#single-player-흐름) |
| 단계 15 | [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경), [비목표](../spec/draft/spot-entry-transport-queues.ko.md#비목표) |
| 단계 16 | [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) 전체 |
| 단계 17 | [Actor와 Entry Spot 흐름](../spec/draft/spot-entry-transport-queues.ko.md#actor와-entry-spot-흐름), [STREAM session과 Actor 연결](../spec/draft/spot-entry-transport-queues.ko.md#stream-session과-actor-연결) |
| 문서-코드 반복 리뷰 | [draft spec 전체](../spec/draft/spot-entry-transport-queues.ko.md), 특히 [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경)과 [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) |
| POSD 리팩토링 | [draft spec 전체](../spec/draft/spot-entry-transport-queues.ko.md)와 [software-design-principles.md](../principal/software-design-principles.md) |
| Core release와 bindings 최신화 | [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경), [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) |
| Bindings 순차 적용 | [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경), 각 API 상세 계약 절, [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) |

각 단계 시작 전 implementation review log에 아래 항목을 기록한다.

- 단계:
- 확인한 draft spec 절:
- draft spec에서 구현해야 할 계약 요약:
- 이번 단계에서 구현하지 않는 계약:
- 관련 회귀 테스트 ID:

draft spec에 없는 동작을 구현해야 할 것처럼 보이면, 코드를 먼저 쓰지 않는다.
draft spec에 계약을 추가하거나 기존 계약을 수정한 뒤 이 plan의 traceability 표도
같이 갱신한다.

## 누락 방지 contract matrix

이 계획은 사람이 체크리스트를 읽는 것만으로 완료 판정하지 않는다. 구현 전에
draft spec에서 구현 단위를 추출해 contract matrix를 만들고, 구현 중과 구현 후에
matrix를 계속 갱신한다. matrix에 빈 칸이 남아 있으면 다음 큰 단계로 넘어가지 않는다.

matrix 파일은 아래 경로에 둔다.

- `doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md`

matrix는 최소한 아래 컬럼을 가진다.

| 컬럼 | 의미 |
|------|------|
| Contract ID | 사람이 붙인 고정 ID. 예: `ACTOR-API-001`, `ACTOR-TEST-001` |
| Contract Kind | 계약 분류. 아래 허용 값을 사용한다 |
| Draft Link | draft spec의 정확한 절 링크 |
| Contract Text | draft spec에서 구현해야 하는 계약 요약 |
| Public API / Enum / Struct | 관련 공개 표면. 없으면 `N/A` |
| Implementation Owner | 구현 파일 또는 모듈. 구현 전에는 예상 경로 |
| Test ID | 관련 `ACT-*` 테스트. 없으면 새 테스트 ID를 draft에 추가 |
| Binding Impact | `none`, `all`, 또는 언어 목록 |
| Doc Impact | `spec`, `guide`, `internals`, `bindings`, `sample` 중 해당 항목 |
| Status | `planned`, `implemented`, `tested`, `documented`, `reviewed` |

`Contract Kind`는 반드시 아래 값 중 하나로 적는다.

- `new-api`: 새로 추가하는 public API
- `changed-api`: 기존 public API 또는 타입의 계약 변경
- `removed-api`: 제거해야 하는 public API
- `new-type`: 새 public struct, enum, typedef
- `new-enum`: 새 enum 값 또는 result code
- `new-option`: 새 option 값
- `behavior`: 상태 전이, dispatch 순서, discovery publish 시점 같은 동작 계약
- `ownership`: message, handle, reply, partial message 소유권 계약
- `timeout`: timeout 또는 pending request 만료 계약
- `test`: 회귀 테스트 항목
- `non-goal`: 첫 구현에서 의도적으로 구현하지 않는 항목
- `existing-reference`: draft에서 비교나 예시를 위해 언급하지만 새로 구현하지 않는 기존 API 또는 기존 상수

`existing-reference` 행은 구현 대상이 아니다. 이 행은 draft에서 추출한 심볼이 누락된
것이 아니라 기존 계약 참조임을 표시하기 위해 둔다. `Implementation Owner`는
`existing`으로 적고 `Test ID`는 관련 테스트가 없으면 `N/A`로 둔다.

초기 matrix 작성 시 아래처럼 draft 예시나 비교 설명에서 잡히는 기존 심볼은
`existing-reference`로 분류한다. 단, 구현 중 실제 계약 변경이 필요하다고 판단되면
draft spec을 먼저 고쳐 `changed-api`나 `behavior`로 바꾼다.

- 기존 message API: `zlink_msg_init()`, `zlink_msg_close()`
- 기존 Spot API: `zlink_spot_destroy()`, `zlink_spot_dispatch_event_handler()`
- 기존 STREAM API: `zlink_stream_packet_handler()`
- 기존 dispatch event와 subject enum:
  `ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE`,
  `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE`,
  `ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE`,
  `ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE`,
  `ZLINK_SPOT_DISPATCH_SUBJECT_SPOT`,
  `ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER`,
  `ZLINK_SPOT_DISPATCH_SUBJECT_TIMER`
- 기존 send/recv/result 상수:
  `ZLINK_DONTWAIT`, `ZLINK_PART_MORE`, `ZLINK_PART_FINAL`,
  `ZLINK_RECV_OK`, `ZLINK_RECV_BUSY`, `ZLINK_RECV_NOT_SUPPORTED`,
  `ZLINK_SUBMIT_OK`, `ZLINK_SUBMIT_BACKPRESSURED`,
  `ZLINK_SUBMIT_INVALID_ARGUMENT`, `ZLINK_SUBMIT_INVALID_STATE`,
  `ZLINK_SUBMIT_NOT_CONNECTED`, `ZLINK_SUBMIT_NOT_FOUND`,
  `ZLINK_REQUEST_OK`
- 기존 macro 또는 option:
  `ZLINK_EXPORT`, `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC`

`new-*`, `changed-api`, `removed-api`, `behavior`, `ownership`, `timeout`, `test`
행은 실제 구현, 테스트, 문서 반영 대상이다. 이 분류가 애매하면 draft spec을 먼저
고쳐서 기존 참조인지 구현 대상인지 분리한다.

matrix 생성 시 draft spec에서 아래 항목을 빠짐없이 추출한다.

- [x] `zlink_`로 시작하는 모든 신규/변경/제거 API
- [x] `ZLINK_`로 시작하는 모든 신규/변경/제거 상수, option, enum 값
- [x] `typedef struct`와 `typedef enum`으로 정의된 모든 신규 타입
- [x] ownership 규칙
- [x] timeout 규칙
- [x] unchecked/checked ref 규칙
- [x] join/leave 상태 규칙
- [x] STREAM session Actor list 규칙
- [x] Discovery active route publish/cleanup 규칙
- [x] snapshot 규칙
- [x] 제거 대상 API
- [x] 모든 `ACT-*` 회귀 테스트
- [x] 비목표 항목

matrix 검증 명령은 최소한 아래 비교를 포함한다.

```bash
comm -23 \
  <(rg -o '(ACT-[A-Z]+|ENTRY(?:-[A-Z]+)?)-[0-9]+' doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) \
  <(rg -o '(ACT-[A-Z]+|ENTRY(?:-[A-Z]+)?)-[0-9]+' doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md | sort -u)

comm -23 \
  <(rg -o 'zlink_[A-Za-z0-9_]+\(' doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) \
  <(rg -o 'zlink_[A-Za-z0-9_]+\(' doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md | sort -u)

comm -23 \
  <(rg -o 'ZLINK_[A-Z0-9_]+' doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) \
  <(rg -o 'ZLINK_[A-Z0-9_]+' doc/plan/spot-actor-dispatch/logs/contract-matrix.ko.md | sort -u)
```

위 명령의 출력이 있으면 matrix가 draft spec을 모두 덮지 못한 것이다. 이 경우
구현을 진행하지 않고 matrix 또는 draft spec을 먼저 수정한다. 단, 위 명령은 기존
심볼도 함께 잡을 수 있으므로 출력된 항목을 새 구현 대상으로 바로 간주하지 않는다.
matrix에 `existing-reference`로 분류해도 되는지 확인한 뒤 닫는다.

구현 후에는 matrix를 코드와도 대조한다.

- [x] matrix의 모든 public API가 `core/include`에 있다.
- [x] matrix의 모든 enum/struct/option이 public header에 있다.
- [x] matrix의 모든 제거 대상 API가 public header와 bindings에서 사라졌다.
- [x] matrix의 `existing-reference` 행이 새 구현 작업으로 처리되지 않았다.
- [x] matrix의 구현 대상 행 중 `Implementation Owner`, `Test ID`, `Doc Impact`가
      비어 있는 행이 없다.
- [x] matrix의 모든 `ACT-*`가 테스트 파일명 또는 테스트 case 이름으로 추적된다.
- [x] matrix의 모든 binding impact가 언어별 작업 로그에 닫혀 있다.
- [x] matrix의 모든 doc impact가 정식 문서와 3회/5회 리뷰 로그에 닫혀 있다.
- [x] `Status`가 `reviewed`가 아닌 행이 하나도 없다.

이 matrix gate가 없으면 완료 판정을 하지 않는다. 새 누락 항목이 발견되면 draft spec,
plan traceability 표, contract matrix, 테스트 목록을 함께 갱신한다.

## 완료 조건

아래 조건이 모두 만족되어야 전체 작업을 완료로 본다.

- draft spec 전체를 덮는 contract matrix가 있고 모든 행의 상태가 `reviewed`다.
- draft spec의 첫 구현 범위에 있는 기능이 모두 구현되어 있다.
- draft spec의 C API, enum, option, struct, callback typedef가 공개 헤더와 일치한다.
- draft spec의 모든 회귀 테스트 항목이 자동 테스트로 닫혀 있다.
- 기존 SPOT, STREAM, Discovery, Registry 회귀 테스트가 통과한다.
- generic discovery route 제거 계획이 공개 헤더, binding, sample, 문서에 반영되어 있다.
- 구현 후 draft spec, 정식 spec, errno 문서, binding 문서, sample 문서를 코드와
  반복 대조했고 미반영 항목이 없다.
- 전체 코드를 대상으로 POSD 기반 리팩토링 루프를 반복했고 더 진행할 리팩토링 항목이 없다.
- 최종 작업트리에는 의도한 코드, 테스트, 문서 변경만 남아 있다.

## 진행 원칙

- 사용자에게 설계 결정을 다시 묻지 않는다. draft spec을 기준으로 결정한다.
- 구현 중 불명확한 점은 draft spec에 먼저 명확한 계약으로 추가한 뒤 코드에 반영한다.
- 호환성 유예는 두지 않는다. draft spec에 제거로 명시된 API는 첫 구현에서 제거한다.
- 단계마다 테스트를 추가하고 가능한 한 같은 단계 안에서 실패 테스트를 먼저 만든다.
- 변경 범위가 커져도 unrelated cleanup은 섞지 않는다. POSD 리팩토링은 기능 완료와
  문서-코드 검토가 끝난 뒤 별도 루프로 진행한다.
- destructive git 명령은 사용하지 않는다.
- core runtime을 바꾼 뒤 성능이나 C binding perf를 볼 때는 `core/build` 기준으로
  다시 빌드한다.

## 중단 없이 진행하는 규칙

Codex 에이전트는 아래 상황에서도 사용자 결정을 기다리지 않고 계획에 따라 진행한다.

- enum 숫자 충돌: 비어 있는 값으로 조정하고 draft spec과 errno 문서를 같이 고친다.
- 파일 위치 선택: 기존 SPOT, STREAM, Discovery 모듈의 ownership을 따른다.
- 테스트 위치 선택: 기존 core 테스트 구조와 가장 가까운 suite에 추가한다.
- binding 반영 순서: C API와 core 테스트를 먼저 닫은 다음 binding을 얇게 맞춘다.
- 문서 불일치 발견: draft spec을 먼저 고친 뒤 코드, 테스트, 정식 문서를 맞춘다.
- POSD 리팩토링 후보 발견: 기능 완료 전에는 기록만 하고 기능 완료 후 refactor loop에서 처리한다.

외부 인증, remote push 권한, 네트워크 장애처럼 Codex 에이전트가 해결할 수 없는
실행 환경 문제는 blocker log에 남기고 로컬에서 가능한 검증을 모두 끝낸다.

## 산출물

- core C API 구현
- core 내부 Actor table, queue, join, session mapping, remote control, discovery sync 구현
- C API 회귀 테스트
- 기존 테스트 보강
- sample 또는 최소 사용 예
- 정식 spec 문서 반영
- binding 문서와 필요한 binding 표면 반영
- sample과 perf smoke 검증 로그: `doc/plan/spot-actor-dispatch/logs/sample-perf-smoke-log.ko.md`
- 구현 후 리뷰 로그: `doc/plan/spot-actor-dispatch/logs/implementation-review-log.ko.md`
- POSD 리팩토링 로그: `doc/plan/spot-actor-dispatch/logs/posd-refactor-log.ko.md`
- 문서 3회 리뷰 로그: `doc/plan/spot-actor-dispatch/logs/final-doc-review-log.ko.md`
- blocker log: `doc/plan/spot-actor-dispatch/logs/blockers.ko.md`
- core release 로그: `doc/plan/spot-actor-dispatch/logs/core-release-log.ko.md`
- bindings 최신화 로그: `doc/plan/spot-actor-dispatch/logs/bindings-update-log.ko.md`
- bindings spec 5회 리뷰 로그:
  `doc/plan/spot-actor-dispatch/logs/bindings-spec-review-log.ko.md`
- bindings POSD 리팩토링 로그:
  `doc/plan/spot-actor-dispatch/logs/bindings-posd-refactor-log.ko.md`

## 단계 0. 기준 상태 고정

1. 현재 브랜치와 작업트리를 확인한다.
2. draft spec 최신 커밋을 확인한다.
3. `core/include/zlink.h`, `core/include/zlink_errno.h`, `core/include/zlink_enum.h`의
   현재 공개 표면을 기록한다.
4. 기존 SPOT, STREAM, Discovery 테스트 목록을 확인한다.
5. 기존 build/test 명령을 확인한다.
6. baseline build와 baseline test를 실행한다.
7. 실패가 이미 있으면 기존 실패로 기록하고 Actor 변경으로 새 실패를 만들지 않는다.

체크리스트:

- [x] `git status --short` 확인
- [x] 현재 branch 확인
- [x] baseline build 성공 또는 기존 실패 기록
- [x] baseline test 성공 또는 기존 실패 기록
- [x] draft spec 경로와 commit hash 기록
- [x] `doc/plan/spot-actor-dispatch/logs/` 디렉터리 생성
- [x] blocker log 파일 생성
- [x] implementation review log 파일 생성
- [x] POSD refactor log 파일 생성
- [x] final doc review log 파일 생성
- [x] sample/perf smoke log 파일 생성
- [x] core release log 파일 생성
- [x] bindings update log 파일 생성
- [x] bindings spec review log 파일 생성
- [x] bindings POSD refactor log 파일 생성
- [x] contract matrix 파일 생성
- [x] draft spec의 API, enum, struct, option, `ACT-*`, 제거 대상 API를 matrix에 추출
- [x] matrix와 draft spec의 `ACT-*` 목록 대조 결과가 비어 있음
- [x] matrix와 draft spec의 `zlink_*` API 목록 대조 결과가 비어 있음
- [x] matrix와 draft spec의 `ZLINK_*` 상수/enum/option 목록 대조 결과가 비어 있음

로그 파일에는 아래 공통 형식을 사용한다.

- 날짜:
- 대상:
- 수행한 명령:
- 발견한 문제:
- 수정한 파일:
- 남은 위험:
- 다음 확인:

## 단계 1. 공개 C 표면 추가

draft spec의 C API 변경 목록을 기준으로 공개 표면을 먼저 닫는다.

### 1.1 상수, option, enum

- [x] `ZLINK_ACTOR_ID_MAX` 추가
- [x] `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC` 추가
- [x] `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE` 추가
- [x] `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE` 추가
- [x] `ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR` 추가
- [x] `zlink_actor_create_status_t` 추가
- [x] `ZLINK_ACTOR_CREATE_CREATED` 추가
- [x] `ZLINK_ACTOR_CREATE_EXISTING` 추가
- [x] `zlink_actor_admission_result_t` 추가
- [x] `ZLINK_ACTOR_ADMISSION_ACCEPT` 추가
- [x] `ZLINK_ACTOR_ADMISSION_REJECT` 추가
- [x] 기존 `ZLINK_REQUEST_TIMED_OUT = 101` 유지 확인
- [x] 기존 `ZLINK_REQUEST_NOT_FOUND = 102` 유지 확인
- [x] `ZLINK_REQUEST_REJECTED` 추가
- [x] `ZLINK_REQUEST_CONFLICT` 추가
- [x] `ZLINK_REQUEST_BUSY` 추가
- [x] `ZLINK_REQUEST_NOT_CONNECTED` 추가
- [x] `ZLINK_REQUEST_INVALID_ARGUMENT` 추가
- [x] `ZLINK_REQUEST_INVALID_STATE` 추가
- [x] `ZLINK_REQUEST_NOT_SUPPORTED` 추가
- [x] enum 숫자 충돌 검사

### 1.2 struct

- [x] `zlink_actor_ref_t` 추가
- [x] `zlink_actor_recv_info_t` 추가
- [x] `zlink_actor_join_info_t` 추가
- [x] `zlink_actor_create_result_t` 추가
- [x] `zlink_actor_route_t` 추가
- [x] `zlink_spot_node_spot_entry_t` 추가
- [x] `zlink_spot_node_actor_entry_t` 추가
- [x] public ABI 크기와 alignment 검토
- [x] C/C++ 컴파일 호환성 검토

### 1.3 함수 선언

- [x] `zlink_spot_node_actor_new()`
- [x] `zlink_actor_destroy()`
- [x] `zlink_actor_get_ref()`
- [x] `zlink_spot_node_actor_lookup()`
- [x] `zlink_remote_actor_get_ref()`
- [x] `zlink_spot_node_create_remote_actor()`
- [x] `zlink_spot_node_destroy_remote_actor()`
- [x] `zlink_spot_node_actor_admission_handler()`
- [x] `zlink_discovery_resolve_actor()`
- [x] `zlink_spot_node_actor_join_spot()`
- [x] `zlink_spot_actor_join_recv()`
- [x] `zlink_spot_actor_join_reply()`
- [x] `zlink_actor_leave_spot()`
- [x] `zlink_spot_node_actor_leave_spot()`
- [x] `zlink_stream_bind_actor()`
- [x] `zlink_stream_unbind_actor()`
- [x] `zlink_stream_send_bound_actor_part()`
- [x] `zlink_actor_send_bound_session_msg()`
- [x] `zlink_actor_send_bound_session_packet()`
- [x] `zlink_actor_recv_part()`
- [x] `zlink_spot_node_spots_snapshot()`
- [x] `zlink_spot_node_actors_snapshot()`
- [x] `zlink_spot_actors_snapshot()`

## 단계 2. 내부 Actor 모델

Actor 내부 상태를 `SpotNode`가 소유하도록 만든다.

구현 항목:

- [x] `SpotNode` 내부 Actor table 추가
- [x] `actor_id` byte 비교 규칙 구현
- [x] 같은 `SpotNode` 안 live Actor id 중복 거부
- [x] 서로 다른 `SpotNode`의 같은 actor id 허용
- [x] live Actor generation 발급
- [x] `generation == 0` unchecked ref 의미 구현
- [x] `generation != 0` checked ref stale 검출 구현
- [x] Actor handle lifetime 관리
- [x] Actor queue unread part 저장
- [x] Actor joined Spot 상태 저장
- [x] Actor bound session ref 저장
- [x] Actor table lock 또는 event-loop ownership 정의
- [x] callback 중 destroy 금지 상태 검사

검증 항목:

- [x] Actor 생성 성공
- [x] 중복 local actor id 실패
- [x] 다른 node 중복 actor id 허용
- [x] Actor lookup 성공과 `ENOENT`
- [x] unchecked ref 생성
- [x] checked ref stale 검출

## 단계 3. Local Actor lifecycle

구현 항목:

- [x] `zlink_spot_node_actor_new()` 구현
- [x] `zlink_actor_get_ref()` 구현
- [x] `zlink_spot_node_actor_lookup()` 구현
- [x] `zlink_remote_actor_get_ref()` 구현
- [x] `zlink_actor_destroy()` 구현
- [x] joined Actor destroy 실패
- [x] bound session detach 후 destroy
- [x] detach 실패 시 destroy 실패와 Actor slot 유지
- [x] session owner provider 종료 또는 stale session ref cleanup 예외
- [x] destroy 성공 시 `*actor_p_ = NULL`
- [x] destroy 실패 시 handle 유지
- [x] destroy timeout 원자성
- [x] unread queue destroy cleanup

회귀 테스트:

- [x] ACT-LIFE-01
- [x] ACT-LIFE-02
- [x] ACT-LIFE-03
- [x] ACT-LIFE-04
- [x] ACT-LIFE-05
- [x] ACT-LIFE-06
- [x] ACT-LIFE-07
- [x] ACT-LIFE-08
- [x] ACT-LIFE-09
- [x] ACT-LIFE-10
- [x] ACT-LIFE-11
- [x] ACT-REMOTE-12
- [x] ACT-REMOTE-13

`ACT-REMOTE-12`와 `ACT-REMOTE-13`은 remote create/destroy 테스트 묶음에도
나오지만 구현 위치는 ref 생성 API가 들어가는 lifecycle 단계다. 이 단계에서 먼저
구현하고 remote control plane 단계에서 다른 remote 테스트와 함께 다시 검증한다.

## 단계 4. Actor queue와 dispatch event

draft spec에서 말하는 Actor queue는 Actor마다 별도 socket이나 독립적으로 설정 가능한
queue 객체를 만든다는 뜻이 아니다. `SpotNode` 내부 relay/dispatch 경로에서 Actor별
unread 상태를 구분하기 위한 논리적 표현이다. Actor 전용 HWM option은 만들지 않는다.
backpressure는 기존 relay 경로의 transport HWM, nonblocking send admission, timeout
규칙을 그대로 사용한다. Actor unread 상태에는 별도 HWM이나 조정 가능한 queue 한계를
두지 않는다.
Actor는 socket, inproc endpoint, transport endpoint를 소유하지 않는다. local relay와
remote relay 모두 최종적으로 target `SpotNode`의 Actor별 unread 상태에 part를 넣는
구조로 구현한다.

구현 항목:

- [x] Actor별 unread 상태에 `zlink_msg_t` part enqueue
- [x] Actor별 unread part FIFO 보존
- [x] multipart part flag 보존
- [x] incomplete multipart 상태 허용
- [x] Actor unread 상태와 dispatch event 연결
- [x] `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE` 발행
- [x] `subject_kind = ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR`
- [x] `subject = local Actor handle`
- [x] `zlink_actor_recv_part()` 구현
- [x] dispatch callback 안 nonblocking drain 지원
- [x] callback 밖 또는 blocking recv 제한
- [x] no data에서 `ZLINK_RECV_NO_DATA`
- [x] Actor destroy와 `SpotNode` shutdown에서 unread/incomplete part 폐기
- [x] Actor 전용 HWM 없이 기존 relay transport HWM 또는 nonblocking send admission을
      `ZLINK_SUBMIT_BACKPRESSURED` 계열 결과로 노출

회귀 테스트:

- [x] ACT-QUEUE-01
- [x] ACT-QUEUE-02
- [x] ACT-QUEUE-04
- [x] ACT-QUEUE-05
- [x] ACT-QUEUE-06
- [x] ACT-QUEUE-16
- [x] ACT-QUEUE-17

## 단계 5. Actor join과 leave

구현 항목:

- [x] 단일 `zlink_spot_node_actor_join_spot()` 구현
- [x] `dest_node_rid_`가 Actor owner node와 같으면 local join 처리
- [x] `dest_node_rid_`가 Actor owner node와 다르면 remote join handoff 처리
- [x] `dest_node_rid_` invalid argument 검증
- [x] target node 연결 없음 처리
- [x] target Spot 없음 `ZLINK_REQUEST_NOT_FOUND`
- [x] source Actor 없음 `ZLINK_REQUEST_NOT_FOUND`
- [x] `generation == 0` unchecked join
- [x] `generation != 0` checked join mismatch
- [x] same Spot 중복 join idempotent success
- [x] 다른 Spot join은 leave 없이 target Spot으로 이동
- [x] Entry Spot이 아닌 target Spot join은 bound session 필수
- [x] bound session 없음 join invalid-state
- [x] remote join prepare에서 bound session ref를 target pending Actor state에 복사
- [x] remote join commit에서 session owner의 session Actor list를 target Actor ref로 갱신
- [x] session Actor list 갱신 실패 또는 timeout 시 remote join 원자성 유지
- [x] request owner와 session owner 분리
- [x] backend service node가 join request owner인 경우 completion을 backend로 반환
- [x] target accept만으로 source Actor를 제거하지 않고 commit 성공 뒤 retire
- [x] session Actor list compare-and-swap으로 remote join visibility point 고정
- [x] visibility point 전 relay는 source Actor, 이후 새 relay는 target Actor로 전달
- [x] visible commit 전 target 도착 relay는 pending Actor state에 buffer
- [x] source node `JoinOp` 생성과 join epoch/reply path 저장
- [x] source Actor retire 뒤에도 `JoinOp`이 completion 전달
- [x] completion 전달 뒤 `JoinOp`과 source Actor tombstone 또는 operation reference 정리
- [x] 하나의 Spot에 N Actor join
- [x] dispatch handler 없는 Spot의 pending join
- [x] `zlink_spot_actor_join_recv()` 구현
- [x] `zlink_actor_join_info_t.request` opaque handle 구현
- [x] request handle one-shot 보장
- [x] `zlink_spot_actor_join_reply()` 구현
- [x] accepted/rejected reply message 전달
- [x] reply 실패 시 message ownership 보존
- [x] join timeout 원자성
- [x] late reply invalid-state
- [x] Spot destroy 또는 SpotNode shutdown pending join terminated
- [x] local `zlink_actor_leave_spot()` 구현
- [x] ref 기반 `zlink_spot_node_actor_leave_spot()` 구현
- [x] Entry Spot leave idempotent success
- [x] leave는 queue를 비우지 않음
- [x] leave 뒤 Entry Spot 메시지는 Entry Spot dispatch event로 drain
- [x] leave/join FIFO 보존
- [x] leave timeout 원자성
- [x] leave 성공 시 active route를 Entry Spot rid로 갱신하는 hook

회귀 테스트:

- [x] ACT-JOIN-01
- [x] ACT-JOIN-02
- [x] ACT-JOIN-03
- [x] ACT-JOIN-04
- [x] ACT-JOIN-05
- [x] ACT-JOIN-06
- [x] ACT-JOIN-07
- [x] ACT-JOIN-08
- [x] ACT-JOIN-09
- [x] ACT-JOIN-10
- [x] ACT-JOIN-11
- [x] ACT-JOIN-12
- [x] ACT-JOIN-13
- [x] ACT-JOIN-14
- [x] ACT-JOIN-15
- [x] ACT-JOIN-16
- [x] ACT-JOIN-17
- [x] ACT-JOIN-18
- [x] ACT-JOIN-19
- [x] ACT-JOIN-20
- [x] ACT-JOIN-21
- [x] ACT-JOIN-22
- [x] ACT-JOIN-23
- [x] ACT-JOIN-24
- [x] ACT-JOIN-25
- [x] ACT-JOIN-26
- [x] ACT-JOIN-27
- [x] ACT-JOIN-28
- [x] ACT-JOIN-29
- [x] ACT-JOIN-30
- [x] ACT-JOIN-31
- [x] ACT-JOIN-32
- [x] ACT-JOIN-33
- [x] ACT-JOIN-34
- [x] ACT-JOIN-35
- [x] ACT-JOIN-36
- [x] ACT-JOIN-37
- [x] ACT-JOIN-38
- [ ] ENTRY-ACTOR-30
- [ ] ENTRY-ACTOR-31
- [ ] ENTRY-ACTOR-32

세부 검증:

- [ ] ENTRY-ACTOR-30은 target Spot의 기존 `zlink_spot_dispatch_event_handler()`가
      `ACTOR_JOIN_READABLE` event를 받고 `zlink_spot_actor_join_recv()` /
      `zlink_spot_actor_join_reply()`로 승인하는지 확인한다.
- [ ] ENTRY-ACTOR-31은 caller가 `zlink_spot_node_create_remote_actor()`를 먼저 호출하지
      않아도 remote join prepare가 target pending Actor state를 만들고 commit 전까지
      live lookup과 active route에 노출하지 않는지 확인한다.
- [ ] ENTRY-ACTOR-32는 remote join prepare가
      `zlink_spot_node_actor_admission_handler()`를 호출하지 않고 target Spot join
      handler의 accept/reject로 생성과 join을 결정하는지 확인한다.
- [x] ACT-JOIN-37은 remote join 성공 commit 뒤 source Actor가 `RETIRED_PENDING_REPLY`
      상태가 되어도 `JoinOp`이 기존 A->Session/request owner reply path로 completion을
      한 번 전달하는지 확인한다.
- [x] ACT-JOIN-37은 completion 전달 전에 같은 session의 새 client relay가 B Actor로
      가는지 함께 확인한다.
- [x] ACT-JOIN-38은 completion 전달 뒤 `JoinOp`이 pending table에서 제거되고 source Actor
      tombstone 또는 operation reference가 정리되는지 확인한다.
- [x] ACT-JOIN-38은 cleanup 뒤 같은 join epoch의 late reply나 stale control frame이
      Actor를 다시 만들거나 completion을 중복 전달하지 않는지 확인한다.

## 단계 6. STREAM session Actor list

구현 항목:

- [x] session owner `SpotNode`에 `session -> actor_id -> Actor ref` 저장
- [x] 한 session에 여러 Actor bind 허용
- [x] 한 Actor는 한 STREAM session에만 bind 허용
- [x] same session same ref bind idempotent
- [x] same session different actor id bind 추가
- [x] same session same actor id different ref 교체
- [x] 새 Actor attach 실패 시 기존 항목 유지
- [x] 이전 Actor detach 실패 시 stale send 방어
- [x] session별 Actor list 분리
- [x] public lookup API 없음 유지
- [x] explicit unbind 구현
- [x] 없는 actor id unbind idempotent success
- [x] user Spot Actor explicit unbind busy/invalid-state
- [x] unbind not connected 실패와 기존 항목 유지
- [x] provider 종료 또는 stale Actor ref cleanup success
- [x] session disconnect cleanup 시 user Spot Actor를 Entry Spot으로 이동
- [x] bind timeout 원자성
- [x] unbind timeout 원자성
- [x] unchecked remote bind concrete generation 저장
- [x] checked remote bind mismatch 실패
- [x] bind 성공 시 Discovery 단계가 사용할 active route publish hook 준비
- [x] Discovery route sync가 아직 구현되지 않은 상태에서도 bind 자체 테스트가 통과

회귀 테스트:

- [x] ACT-STREAM-01
- [x] ACT-STREAM-02
- [x] ACT-STREAM-03
- [x] ACT-STREAM-04
- [x] ACT-STREAM-05
- [x] ACT-STREAM-06
- [x] ACT-STREAM-08
- [x] ACT-STREAM-10
- [x] ACT-STREAM-11
- [x] ACT-STREAM-12
- [x] ACT-STREAM-13
- [x] ACT-STREAM-14
- [x] ACT-STREAM-15
- [x] ACT-STREAM-16
- [x] ACT-STREAM-17
- [x] ACT-STREAM-18
- [x] ACT-STREAM-19
- [x] ACT-STREAM-20
- [x] ACT-STREAM-21
- [x] ACT-STREAM-22

## 단계 7. STREAM에서 Actor로 relay

구현 항목:

- [x] `zlink_stream_send_bound_actor_part()` 구현
- [x] target Actor 선택은 `actor_id_`
- [x] session Actor list에 target actor id 없으면 submit not found
- [x] actor id validation
- [x] local Actor queue enqueue
- [x] remote Actor forward
- [x] remote 연결 없음 submit not connected
- [x] remote target Actor 없음이면 target node에서 drop
- [x] multipart per-session in-progress 상태
- [x] `PART_MORE` 성공 시 target actor id 고정
- [x] in-progress 중 다른 actor id invalid-state
- [x] `PART_FINAL` 성공 시 in-progress 완료
- [x] `PART_MORE` 성공 후 final 실패 시 성공 part library-owned
- [x] final retry 가능
- [x] STREAM session disconnect/shutdown cleanup으로 sender in-progress 폐기
- [x] target Actor queue에 이미 들어간 part rollback 금지

회귀 테스트:

- [x] ACT-QUEUE-01
- [x] ACT-QUEUE-02
- [x] ACT-QUEUE-03
- [x] ACT-QUEUE-07
- [x] ACT-QUEUE-13
- [x] ACT-QUEUE-14
- [x] ACT-QUEUE-15
- [x] ACT-STREAM-07

## 단계 8. Actor에서 bound session으로 전송

구현 항목:

- [x] `zlink_actor_send_bound_session_msg()` 구현
- [x] `zlink_actor_send_bound_session_packet()` 구현
- [x] Actor local handle validation
- [x] bound session 없음 not found
- [x] session owner node 연결 없음 not connected
- [x] stale session cleanup
- [x] session owner node에서 sender Actor ref 검증
- [x] 같은 actor id rebind 뒤 이전 Actor send 차단
- [x] raw callback용 단일 message send
- [x] packet callback용 header/body send
- [x] packet send 부분 성공 금지
- [x] 실패 시 message/header/body ownership 보존

회귀 테스트:

- [x] ACT-QUEUE-08
- [x] ACT-QUEUE-09
- [x] ACT-QUEUE-10
- [x] ACT-QUEUE-11
- [x] ACT-QUEUE-12
- [x] ACT-OWN-05

## 단계 9. Remote Actor control plane

구현 항목:

- [x] `zlink_spot_node_create_remote_actor()` 구현
- [x] create-or-get request routing
- [x] 단일 create message 전달
- [x] admission handler 등록/해제
- [x] handler 없음 기본 reject
- [x] Actor가 없을 때만 admission 호출
- [x] Actor가 있으면 existing 반환과 admission 미호출
- [x] 같은 actor id 동시 create 직렬화
- [x] 다른 node 중복 actor id 허용
- [x] create timeout 재시도 수렴
- [x] create submit 후 message ownership 이전
- [x] `zlink_spot_node_destroy_remote_actor()` 구현
- [x] 없는 Actor destroy idempotent success
- [x] join 상태 destroy busy/invalid-state
- [x] unchecked remote destroy
- [x] checked destroy mismatch stale/conflict
- [x] remote destroy detach 실패 시 Actor slot 유지
- [x] remote destroy timeout 원자성
- [x] admission handler 재진입 금지

회귀 테스트:

- [x] ACT-REMOTE-01
- [x] ACT-REMOTE-02
- [x] ACT-REMOTE-03
- [x] ACT-REMOTE-04
- [x] ACT-REMOTE-05
- [x] ACT-REMOTE-06
- [x] ACT-REMOTE-07
- [x] ACT-REMOTE-08
- [x] ACT-REMOTE-09
- [x] ACT-REMOTE-10
- [x] ACT-REMOTE-11
- [x] ACT-REMOTE-14
- [x] ACT-REMOTE-15
- [x] ACT-REMOTE-16
- [x] ACT-REMOTE-17
- [x] ACT-REMOTE-18

## 단계 10. Discovery active route

구현 항목:

- [x] `ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC` set/get 구현
- [x] `zlink_discovery_resolve_actor()` 구현
- [x] `zlink_actor_route_t` 반환
- [x] Actor 생성 시 active route 미공개
- [x] remote create 시 active route 미공개
- [x] stream bind 전 active route 미공개
- [x] stream bind 성공 시 active route publish
- [x] 단계 6의 bind hook과 Discovery route sync를 연결
- [x] unchecked bind 후 concrete generation publish
- [x] bind 직후 current Spot이 Entry Spot이면 `joined = 1`, Entry Spot rid publish
- [x] user Spot join 시 active route가 해당 Actor ref를 가리키면 joined Spot rid 갱신
- [x] leave 시 active route가 해당 Actor ref를 가리키면 `joined = 1`, Entry Spot rid로 갱신
- [x] remote join commit 시 active route를 target node Actor ref로 갱신
- [x] remote join commit 시 active route의 joined Spot rid를 target Spot rid로 갱신
- [x] unbind는 active route 유지
- [x] session disconnect cleanup은 active route를 유지하되 Actor가 Entry Spot으로 돌아가면 joined Spot rid도 Entry Spot rid로 갱신
- [x] matching Actor destroy 시 route 제거
- [x] route가 다른 node로 이동한 뒤 이전 Actor destroy는 새 route 유지
- [x] SpotNode provider 종료 cleanup
- [x] stale route row 반환 금지
- [x] 기존 `zlink_discovery_resolve_spot()` 유지

회귀 테스트:

- [x] ACT-DISC-01
- [x] ACT-DISC-02
- [x] ACT-DISC-03
- [x] ACT-DISC-04
- [x] ACT-DISC-05
- [x] ACT-DISC-06
- [x] ACT-DISC-07
- [x] ACT-DISC-08
- [x] ACT-DISC-09
- [x] ACT-DISC-10
- [x] ACT-DISC-11
- [x] ACT-DISC-12
- [x] ACT-DISC-13
- [x] ACT-DISC-14
- [x] ACT-DISC-15
- [x] ACT-DISC-16
- [x] ACT-DISC-17
- [x] ACT-STREAM-09

세부 검증:

- [x] ACT-DISC-04는 Actor 생성 직후에는 `resolve_actor`가 실패하고 session bind 성공
      뒤에는 `joined = 1`, `joined_spot_rid = Entry Spot rid`로 조회되는지 확인한다.
- [x] ACT-DISC-14는 같은 SpotNode 안 user Spot join 성공 뒤 같은 Actor ref를 유지하면서
      `joined_spot_rid`만 user Spot rid로 갱신되는지 확인한다.
- [x] ACT-DISC-15는 leave 성공 뒤 route를 제거하거나 `joined = 0`으로 만들지 않고
      `joined = 1`, `joined_spot_rid = Entry Spot rid`로 갱신되는지 확인한다.
- [x] ACT-DISC-16은 remote join commit 성공 뒤 Actor ref의 node rid/generation과
      `joined_spot_rid`가 target node와 target Spot 기준으로 바뀌는지 확인한다.
- [x] ACT-DISC-17은 session disconnect cleanup이 user Spot Actor를 Entry Spot으로
      되돌릴 때 active route를 유지하고 `joined_spot_rid`를 Entry Spot rid로 갱신하는지
      확인한다.

## 단계 11. Generic discovery route 제거

구현 항목:

- [x] `zlink_discovery_bind_route()` 공개 헤더 제거
- [x] `zlink_discovery_unbind_route()` 공개 헤더 제거
- [x] `zlink_discovery_resolve_route()` 공개 헤더 제거
- [x] core 구현 제거
- [x] binding 표면 제거
- [x] sample에서 actor/spot 전용 API로 교체
- [x] stale 문서에서 제거 또는 historical 표시
- [x] build에서 symbol 누락 또는 dead declaration 없음 확인

회귀 테스트:

- [x] ACT-ROUTE-01
- [x] ACT-ROUTE-02
- [x] ACT-ROUTE-03

## 단계 12. Snapshot과 monitoring

구현 항목:

- [x] `zlink_spot_node_spots_snapshot()` 구현
- [x] local Spot facade 목록 반환
- [x] `dispatch_handler_attached`
- [x] `joined_actor_count`
- [x] `pending_actor_join_count`
- [x] `route_synced`
- [x] Spot row `last_changed_ms`
- [x] `zlink_spot_actors_snapshot()` 구현
- [x] 특정 Spot joined Actor ref 목록 반환
- [x] `zlink_spot_node_actors_snapshot()` 구현
- [x] `joined`
- [x] `joined_spot_rid`
- [x] `route_synced`
- [x] `pending_message_count`
- [x] Actor row `last_changed_ms`
- [x] in/out count 패턴 구현
- [x] snapshot 값은 진단용이며 flow control 계약으로 쓰지 않음

회귀 테스트:

- [x] ACT-SNAPSHOT-01
- [x] ACT-SNAPSHOT-02
- [x] ACT-SNAPSHOT-03
- [x] ACT-SNAPSHOT-04
- [x] ACT-SNAPSHOT-05
- [x] ACT-SNAPSHOT-06
- [x] ACT-SNAPSHOT-07
- [x] ACT-SNAPSHOT-08
- [x] ACT-SNAPSHOT-09
- [x] ACT-SNAPSHOT-10

## 단계 13. 소유권과 실패 경로

모든 API 구현 뒤 소유권 규칙을 독립적으로 검증한다.

구현 확인:

- [x] send 성공 시 message ownership 이전
- [x] send 실패 시 ownership 호출자 유지
- [x] recv 성공 시 ownership 호출자 이전
- [x] join submit 성공 시 request message ownership 이전
- [x] join submit 실패 시 ownership 호출자 유지
- [x] join recv 성공 시 message ownership 호출자 이전
- [x] join reply 성공 시 reply message ownership 이전
- [x] join reply 실패 시 ownership 호출자 유지
- [x] remote create submit 성공 시 message ownership 이전
- [x] remote create validation 실패 시 ownership 호출자 유지
- [x] actor packet send 실패 시 header/body 모두 호출자 유지
- [x] partial packet send 공개 계약 없음
- [x] request opaque handle one-shot
- [x] callback 안 같은 Actor destroy 금지

회귀 테스트:

- [x] ACT-OWN-01
- [x] ACT-OWN-02
- [x] ACT-OWN-03
- [x] ACT-OWN-04
- [x] ACT-OWN-05
- [x] ACT-OWN-06

## 단계 14. Core sample과 binding 영향 목록

C API와 core 동작이 안정된 뒤 core 기준 sample과 binding 영향 목록을 먼저 닫는다.
언어별 binding 구현은 이 단계에서 진행하지 않는다. full binding 반영은 core release와
`bindings/update_zlink_libs.sh` 실행 뒤 `Bindings 순차 적용, 문서 5회 리뷰, 검증`
단계에서 언어별로 하나씩 진행한다.

작업 항목:

- [x] Local Actor room server sample 추가 또는 갱신
- [x] gateway session에서 remote play server Actor로 relay하는 sample 추가 또는 갱신
- [x] single-player queue serialization sample 추가 또는 갱신
- [x] 새 sample이 core C API만으로 동작하는지 확인
- [x] generic route 제거가 sample에 미치는 영향 목록 작성
- [x] binding별로 추가, 변경, 제거해야 할 API 목록을 contract matrix의
  `Binding Impact`에 기록
- [x] binding에서 typed Actor object, codec, DI, async runtime을 자동 생성하지 않는다는
  non-goal을 binding 영향 목록에 기록
- [x] binding full 구현을 이 단계에서 수행하지 않았음을 implementation review log에 기록

## 단계 15. 정식 문서 반영

구현과 테스트가 완료된 뒤 draft spec을 core 정식 문서로 나누어 반영한다.
binding 정식 문서는 core release 뒤 native library를 최신화한 다음, bindings 전용
5회 리뷰 단계에서 반영한다.

반영 대상:

- [x] `doc/spec/core/service/spot.ko.md`
- [x] `doc/spec/core/socket/stream.ko.md`
- [x] `doc/spec/core/errno-map.ko.md`
- [x] 관련 guide 문서
- [x] 관련 internals 문서
- [x] sample 문서
- [x] binding 문서는 draft link와 추후 반영 위치만 남기고, 구현 전 계약처럼 섞어 쓰지 않음

규칙:

- spec에는 공개 계약만 넣는다.
- guide에는 사용 목적과 흐름만 넣는다.
- internals에는 내부 구조와 데이터 흐름만 넣는다.
- draft는 구현 완료 후 historical draft로 남기거나 정식 문서 링크를 단다.

이 단계는 기능 구현 직후 1차 반영만 수행한다. POSD 리팩토링이 끝나면 코드 구조와
문서가 다시 달라질 수 있으므로, `POSD 후 최종 문서 업데이트와 3회 리뷰` 단계를
반드시 다시 수행한다.

## 단계 16. 전체 검증 명령

구현 단계마다 가능한 작은 테스트를 돌리고, 큰 milestone마다 전체 검증을 수행한다.

필수 검증:

- [x] core build
- [x] core unit/integration tests
- [x] sample build
- [x] 기존 Discovery tests
- [x] 기존 SPOT tests
- [x] 기존 STREAM tests
- [x] sanitizer 또는 debug build 가능 시 실행
- [x] public header compile check
- [x] removed symbol reference check
- [x] `rg` 기반 stale API 이름 check
- [x] binding full 구현 전에는 binding compile failure를 core 완료 blocker로 처리하지 않음
- [x] binding full 구현 뒤에는 언어별 binding 단계의 검증 조건을 따른다

권장 검색:

- [x] `zlink_spot_node_remote_actor_ref`가 남아 있지 않음
- [x] `zlink_stream_lookup_actor`가 남아 있지 않음
- [x] `zlink_stream_send_actor_part`가 남아 있지 않음
- [x] `session_actor_key`가 남아 있지 않음
- [x] Actor HWM option이 남아 있지 않음
- [x] generic discovery route 공개 참조가 남아 있지 않음
- [x] `generation == 0`을 invalid로 처리하는 ref 기반 API가 남아 있지 않음
- [x] `RemoteActor`가 원격 객체 생성처럼 설명되지 않음

## 단계 17. Sample과 perf smoke 검증

구현 완료 뒤 sample과 perf가 실제로 동작하는지 별도로 확인한다. 이 단계는 전체
테스트가 통과하더라도 반드시 수행한다. sample은 사용자-facing 예제의 회귀를 잡기
위한 검증이고 perf smoke는 Actor 변경이 기존 single/multi benchmark runner와
core runtime 연결을 깨지 않았는지 확인하기 위한 검증이다.

### 사전 조건

- [x] `core/src` 또는 `core/include`를 바꾼 뒤 `cmake --build core/build`를 실행했다.
- [x] perf runner가 출력하는 `Perf runtime libzlink` 경로가 `core/build` 아래 runtime을 가리킨다.
- [x] perf runner가 stale runtime 오류 없이 시작한다.
- [x] sample/perf 실행 결과와 실패 원인을
  `doc/plan/spot-actor-dispatch/logs/sample-perf-smoke-log.ko.md`에 기록한다.

### sample smoke

core 구현 직후에는 core C API로 작성된 sample만 smoke 대상으로 삼는다. 언어별
binding sample은 core release와 native library 최신화 뒤, bindings 순차 단계에서
각 언어별로 실행한다.

- [x] core C API 기반 sample build 성공
- [x] core C API 기반 sample runner가 있으면 실행 성공
- [x] 새 Actor sample이 추가되었다면 core C API 기반 sample runner에 포함되어 실행된다.
- [x] Local Actor room server sample 성공
- [x] gateway session에서 remote play server Actor로 relay하는 sample 성공
- [x] single-player queue serialization sample 성공

### perf smoke

perf smoke는 수치 비교가 아니라 실행 성공 여부를 검증한다. 모든 smoke는 `runs=1`,
짧은 duration, 작은 client 수, 명시적인 message size sweep으로 실행한다. 기본
성능 판단은 별도 perf 계획에서 다루며 이 단계에서는 runner와 runtime이 깨지지
않았는지만 확인한다.

필수 message size sweep:

- [x] `64`
- [x] `1024`
- [x] `4096`
- [x] `65536`

single smoke:

```bash
cmake --build core/build
./bindings/c/perf/run_benchmarks.sh \
  --pattern ALL \
  --runs 1 \
  --duration 1 \
  --msg-sizes 64,1024,4096,65536 \
  --transports tcp \
  --results-tag actor_smoke_single
```

multi smoke:

```bash
cmake --build core/build
./bindings/c/perf/run_benchmarks_multi.sh \
  --runs 1 \
  --duration 1 \
  --clients 8 \
  --msg-sizes 64,1024,4096,65536 \
  --transports tcp \
  --results-tag actor_smoke_multi
```

검증 항목:

- [x] single perf smoke가 모든 지정 size에서 성공한다.
- [x] multi perf smoke가 모든 지정 size에서 성공한다.
- [x] single perf 결과 파일이 생성된다.
- [x] multi perf 결과 파일이 생성된다.
- [x] 결과 로그에 실패한 pattern, transport, size가 없다.
- [x] perf runner가 stale `core/build` runtime을 감지하지 않는다.
- [x] Actor 변경 뒤 기존 `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` perf smoke가 성공한다.
- [x] 실패가 있으면 코드 또는 runner 문제를 수정하고 sample/perf smoke를 처음부터 다시 실행한다.

## 구현 후 문서-코드 반복 리뷰 루프

기능 구현과 테스트가 끝나면 아래 루프를 반복한다. 한 번만 수행하지 않는다.
새 mismatch가 발견되지 않을 때까지 계속한다.

### 루프 입력

- draft spec
- 정식 spec 문서
- public header
- core 구현
- binding 영향 목록
- sample
- 회귀 테스트
- errno map

### 루프 절차

1. draft spec의 모든 API, struct, enum, option을 표로 추출한다.
2. public header에 같은 항목이 있는지 확인한다.
3. public header에 있지만 spec에 없는 항목을 찾는다.
4. spec에 있지만 public header에 없는 항목을 찾는다.
5. 각 API의 인자 순서, 반환 타입, ownership 규칙을 코드와 대조한다.
6. 각 result code의 성공/실패 경로를 테스트와 대조한다.
7. 각 회귀 테스트 ID가 실제 테스트에 매핑되어 있는지 확인한다.
8. 각 비목표 항목이 코드나 문서에 기능처럼 들어가지 않았는지 확인한다.
9. generic route 제거가 core 코드, sample, 문서에 반영됐는지 확인한다.
   binding 반영 여부는 release 뒤 bindings 순차 단계에서 닫는다.
10. Discovery active route의 publish timing이 문서와 같은지 확인한다.
11. unchecked ref와 checked ref 의미가 join, leave, bind, destroy에서 일관적인지 확인한다.
12. multipart incomplete 처리 계약이 relay, recv, destroy에서 일관적인지 확인한다.
13. session multi-actor bind 계약이 relay, actor-to-session send, unbind에서 일관적인지 확인한다.
14. mismatch가 있으면 먼저 문서 또는 코드를 고친다.
15. 관련 테스트를 추가하거나 수정한다.
16. 전체 검증 명령을 다시 실행한다.
17. mismatch 목록이 비어 있으면 루프를 한 번 더 실행해 재확인한다.
18. 두 번 연속 mismatch가 없으면 문서-코드 반복 리뷰를 종료한다.
19. contract matrix의 관련 행을 `reviewed`로 갱신한다.
20. `reviewed`가 아닌 matrix 행이 있으면 해당 행의 단계로 되돌아간다.

### 루프 종료 조건

- [x] contract matrix에 `reviewed`가 아닌 행이 없음
- [x] spec-only 항목 없음
- [x] code-only public API 없음
- [x] 테스트 없는 계약 없음
- [x] 문서와 다른 errno/result 없음
- [x] 문서와 다른 ownership 규칙 없음
- [x] draft 첫 구현 범위의 미구현 항목 없음
- [x] 정식 문서 미반영 항목 없음
- [x] 두 번 연속 mismatch 없음

## POSD 기반 전체 코드 리팩토링 루프

문서-코드 반복 리뷰가 끝난 뒤 전체 코드를 대상으로 POSD 기반 리팩토링을 진행한다.
기능 구현과 섞지 않는다. 리팩토링은 반복 수행하며 더 진행할 항목이 없을 때까지
멈추지 않는다.

### 대상

- [x] `core/include`
- [x] `core/src`
- [x] core tests
- [x] bindings
- [x] samples
- [x] scripts
- [x] docs와 codegen 도구

### 1회차 스캔 절차

1. Actor 구현과 인접 모듈의 public surface를 나열한다.
2. 각 모듈이 숨겨야 할 설계 결정을 적는다.
3. 아래 POSD 위험 신호를 찾는다.
4. 위험 신호마다 위반한 원칙과 근거를 기록한다.
5. 각 항목마다 최소 두 가지 개선안을 비교한다.
6. 호출자 복잡성이 줄어드는 쪽을 선택한다.
7. 작은 단위로 리팩토링한다.
8. 관련 테스트를 실행한다.
9. 전체 테스트를 실행한다.
10. 리팩토링 로그를 남긴다.

### POSD 위험 신호 체크리스트

- [x] 얕은 wrapper 또는 pass-through 함수
- [x] 호출자가 내부 state machine을 알아야 하는 API
- [x] 같은 Actor table 지식이 여러 모듈에 중복됨
- [x] session Actor list 지식이 relay, bind, discovery에 흩어짐
- [x] generation/stale 판단이 여러 곳에 복제됨
- [x] message ownership 규칙이 API마다 다르게 구현됨
- [x] timeout 원자성 처리가 request마다 중복됨
- [x] join pending request lifecycle이 Spot dispatch와 분리되어 이해하기 어려움
- [x] Discovery route publish timing이 여러 곳에 암묵적으로 흩어짐
- [x] special case가 일반 경로로 흡수될 수 있는데 조건문으로 남아 있음
- [x] 구현 순서 기준으로 파일이나 class가 나뉜 temporal decomposition
- [x] low-level module에 application-specific Actor 개념이 새어 나옴
- [x] public getter가 내부 표현을 노출함
- [x] 한 변경을 위해 여러 파일에서 같은 상수를 수정해야 함
- [x] 코멘트가 코드 동작을 반복 설명하고 있음
- [x] 테스트 helper가 production protocol 지식을 중복 보유함

### 리팩토링 후보별 필수 기록

각 후보는 아래 형식으로 기록한다.

- 위치:
- 위험 신호:
- 위반 원칙:
- 대안 A:
- 대안 B:
- 선택:
- 선택 이유:
- 호출자 복잡성 변화:
- 테스트:
- 결과:

### 리팩토링 반복 절차

1. repo 전체를 다시 스캔한다.
2. 새 위험 신호 목록을 만든다.
3. 이미 처리한 항목이 재발했는지 확인한다.
4. 남은 항목이 있으면 우선순위를 정한다.
5. 가장 높은 우선순위 항목을 리팩토링한다.
6. 관련 테스트와 전체 테스트를 실행한다.
7. 문서와 sample이 영향을 받으면 즉시 반영한다.
8. 다시 1번으로 돌아간다.

### 리팩토링 종료 조건

아래 조건이 모두 참이어야 종료한다.

- [x] repo 전체 스캔에서 새 POSD 위험 신호가 없다.
- [x] 이미 기록된 위험 신호가 모두 해결되었거나 명시적으로 유지 사유가 있다.
- [x] 유지 사유가 있는 항목은 public 계약, ABI, 성능, 안전성 중 하나로 설명된다.
- [x] 두 번 연속 전체 스캔에서 새 리팩토링 후보가 없다.
- [x] 전체 테스트가 통과한다.
- [ ] 리팩토링 후 문서-코드 반복 리뷰를 다시 한 번 수행했고 mismatch가 없다.

## POSD 후 최종 문서 업데이트와 3회 리뷰

POSD 기반 리팩토링이 끝나면 `doc/guide`, `doc/internals`, `doc/spec` 문서를
최종 코드 구조와 공개 계약에 맞게 다시 업데이트한다. 이 단계는 정식 문서 1차
반영과 다르다. 리팩토링으로 모듈 경계, 내부 구조, 예제 흐름, public API 설명이
달라졌는지 다시 확인하고 문서에 반영한다.

### 업데이트 대상

- [ ] `doc/spec/core/service/spot.ko.md`
- [x] `doc/spec/core/socket/stream.ko.md`
- [ ] `doc/spec/core/errno-map.ko.md`
- [x] `doc/spec/bindings`: core release 전에는 draft link와 binding 영향 요약만 반영.
  언어별 full binding spec은 release 뒤 5회 리뷰 단계에서 반영
- [x] `doc/guide`
- [x] `doc/internals`
- [ ] `doc/site/docs` 또는 site 생성 원본이 따로 있으면 해당 원본
- [x] sample README와 사용 예
- [x] historical draft 상태와 정식 문서 링크

### 업데이트 규칙

- spec에는 공개 API 계약, 반환값, errno/result, ownership, threading 제한만 둔다.
- guide에는 사용 시나리오, API 선택 기준, 최소 예제를 둔다.
- internals에는 Actor table, session Actor list, join pending, dispatch event,
  Discovery route sync, cleanup 흐름 같은 내부 구조를 둔다.
- guide에 내부 socket 배선이나 lock 구조를 넣지 않는다.
- internals에 사용자 사용법을 넣지 않는다.
- spec에 구현 배경 설명을 길게 넣지 않는다.

### 1차 문서 리뷰

1. public header와 core `doc/spec`의 API 목록을 대조한다.
2. core 구현과 `doc/internals`의 내부 흐름을 대조한다.
3. sample과 `doc/guide`의 사용 흐름을 대조한다.
4. errno/result mapping을 `doc/spec/core/errno-map.ko.md`와 대조한다.
5. mismatch가 있으면 문서나 코드를 수정한다.
6. 수정 내용과 확인 결과를
   `doc/plan/spot-actor-dispatch/logs/final-doc-review-log.ko.md`에 `1차`로 기록한다.

### 2차 문서 리뷰

1. 1차 수정 뒤 같은 대조를 처음부터 다시 수행한다.
2. stale API 이름, 제거된 generic route API, `zlink_spot_node_remote_actor_ref`,
   Actor HWM option, `generation == 0` invalid 설명이 남았는지 검색한다.
3. guide, internals, spec 사이에 같은 내용을 서로 다른 의미로 설명한 부분이 있는지
   확인한다.
4. mismatch가 있으면 문서나 코드를 수정한다.
5. 수정 내용과 확인 결과를 final doc review log에 `2차`로 기록한다.

### 3차 문서 리뷰

1. 2차 수정 뒤 다시 전체 문서 대조를 수행한다.
2. `doc/guide`, `doc/internals`, `doc/spec` 각각의 목적에 맞지 않는 내용이 섞였는지
   확인한다.
3. draft spec의 첫 구현 범위와 core 정식 문서의 반영 상태를 다시 대조한다.
4. 회귀 테스트 ID와 정식 문서의 계약이 서로 어긋나지 않는지 확인한다.
5. mismatch가 있으면 문서나 코드를 수정하고 3차를 다시 시작한다.
6. mismatch 없이 3차가 끝나면 final doc review log에 `3차 완료`를 기록한다.

### 종료 조건

- [ ] guide, internals, core spec 문서가 모두 최종 코드와 맞다.
- [x] `doc/spec/bindings`에는 release 뒤 full 반영을 위한 draft link와 영향 요약이 있다.
- [x] 1차, 2차, 3차 문서 리뷰 로그가 남아 있다.
- [ ] 3차 리뷰에서 mismatch가 없다.
- [x] 3차 리뷰 중 mismatch가 발견되어 수정했다면, 3차를 처음부터 다시 수행했다.
- [ ] stale API 이름과 제거 대상 API 설명이 정식 문서에 남아 있지 않다.
- [x] draft 문서가 historical draft 또는 정식 문서 링크 상태로 정리되어 있다.

## Core release와 bindings 최신화

최종 문서 업데이트와 3회 리뷰가 끝나면 core 변경을 커밋하고 push한 뒤 GitHub
Actions를 이용해 core release를 만든다. release가 완료된 뒤에만 bindings native
library 최신화를 진행한다.

### Core version, commit, push

1. release할 core version `X.Y.Z`를 정한다.
2. `VERSION` 파일의 `LIBZLINK_VERSION_MAJOR`, `LIBZLINK_VERSION_MINOR`,
   `LIBZLINK_VERSION_PATCH`, `LIBZLINK_VERSION`을 `X.Y.Z`로 맞춘다.
3. `core/include/zlink.h`의 `ZLINK_VERSION_MAJOR`, `ZLINK_VERSION_MINOR`,
   `ZLINK_VERSION_PATCH`를 `X.Y.Z`와 맞춘다.
4. `core/CMakeLists.txt`의 `project(zlink VERSION ...)`와 target `VERSION` 값을
   `X.Y.Z`와 맞춘다.
5. `VERSION`, `core/include/zlink.h`, `core/CMakeLists.txt`가 같은 version을
   가리키는지 검색으로 확인한다.
6. 필요하면 `core/packaging/conan/conandata.yml`과 vcpkg overlay metadata를
   새 release source와 version에 맞게 갱신한다.
7. 전체 build/test, sample smoke, perf smoke, 문서 3회 리뷰 종료 조건을 다시 확인한다.
8. `git status --short`로 의도하지 않은 파일이 섞이지 않았는지 확인한다.
9. core 구현, 테스트, 문서, version 변경을 commit한다.
10. commit을 `origin`의 작업 branch에 push한다.
11. push로 실행된 GitHub Actions를 GitHub CLI로 확인한다.

권장 명령:

```bash
rg -n 'LIBZLINK_VERSION|ZLINK_VERSION_(MAJOR|MINOR|PATCH)|project\(zlink VERSION|VERSION "' \
  VERSION core/include/zlink.h core/CMakeLists.txt
git status --short
git add <intended files>
git commit -m "feat: implement spot actor dispatch"
git push origin <branch>
gh run list --branch <branch> --limit 10
gh run watch <run-id> --exit-status
```

### Core tag와 release

core release tag는 `core/vX.Y.Z` 형식을 사용한다.

```bash
git tag core/vX.Y.Z
git push origin core/vX.Y.Z
```

tag push 뒤 아래 workflow를 GitHub CLI로 모니터링한다.

- `.github/workflows/build.yml`
- `.github/workflows/core-conan-release.yml`

필수 확인:

- [x] `core/vX.Y.Z` tag push 성공
- [x] `Build libzlink Core Libraries` workflow 성공
- [x] `Release Core Conan Package` workflow 성공 또는 optional upload skip 사유 기록
- [x] `gh release view core/vX.Y.Z`가 성공
- [x] release asset에 core native archive가 모두 존재
- [x] `bindings/update_zlink_libs.sh`가 요구하는 필수 asset이 모두 존재
- [x] 실패한 workflow가 있으면 수정 후 commit, push, tag 재처리 또는 새 patch version으로 재시도

권장 명령:

```bash
gh run list --workflow "Build libzlink Core Libraries" --limit 10
gh run list --workflow "Release Core Conan Package" --limit 10
gh run watch <run-id> --exit-status
gh release view core/vX.Y.Z --json tagName,url,assets
```

진행 내용은 `doc/plan/spot-actor-dispatch/logs/core-release-log.ko.md`에 기록한다.

### Bindings native library 최신화

core release가 완료되면 아래 스크립트로 bindings에 포함된 native library를 최신
release asset으로 교체한다.

```bash
bindings/update_zlink_libs.sh core/vX.Y.Z --expect-version X.Y.Z
```

규칙:

- [x] `gh` CLI 인증이 되어 있어야 한다.
- [x] core release 완료 전에는 이 스크립트를 실행하지 않는다.
- [x] 스크립트가 요구하는 release asset 누락이 있으면 core release 문제로 되돌아가 수정한다.
- [x] 스크립트 실행 뒤 `git status --short`로 변경된 binding native library와 version marker를 확인한다.
- [x] bindings update 결과를 `doc/plan/spot-actor-dispatch/logs/bindings-update-log.ko.md`에 기록한다.

## Bindings 순차 적용, 문서 5회 리뷰, 검증

bindings 작업은 동시에 진행하지 않는다. 언어별로 하나씩 끝까지 진행하고 해당
언어가 spec, sample, perf, POSD 검토까지 완료된 뒤 다음 언어로 넘어간다.

진행 순서는 아래와 같이 고정한다.

1. C
2. C++
3. Rust
4. Go
5. Java
6. Node
7. Python
8. .NET

각 언어별 반복 절차:

1. `doc/spec/draft/spot-entry-transport-queues.ko.md`와 해당 언어 binding spec을 비교한다.
2. core C API 중 해당 binding에 노출해야 할 Actor API를 표로 뽑는다.
3. binding 코드가 spec 표면을 모두 노출하는지 확인한다.
4. binding 코드가 spec의 ownership, error/result, timeout, unchecked/checked ref
   의미를 지키는지 확인한다.
5. binding sample을 추가하거나 갱신한다.
6. binding perf smoke를 추가하거나 갱신한다.
7. binding 문서와 sample을 실행한다.
8. binding 테스트와 perf smoke를 실행한다.
9. binding 대상 코드에 POSD 기반 리팩토링을 수행한다.
10. 해당 언어에서 mismatch가 없을 때 다음 언어로 넘어간다.

### Bindings spec 문서 5회 리뷰

`doc/spec/bindings` 아래 문서는 draft spec과 5번 비교 리뷰한다. 5회 리뷰는 전체
bindings spec을 대상으로 수행하며 각 회차에서 발견한 mismatch를 수정한 뒤 다음
회차로 넘어간다.

각 회차 공통 절차:

1. draft spec의 C API, enum, struct, ownership, result code를 추출한다.
2. `doc/spec/bindings/README.md`와 언어별 README를 비교한다.
3. 언어별 문서에 Actor lifecycle, remote Actor ref, create-or-get, join/leave,
   STREAM binding, relay, actor-to-session send, discovery route, snapshot, 제거 API가
   반영됐는지 확인한다.
4. C binding 문서가 core C API와 정확히 맞는지 확인한다.
5. C++/Rust/Go/Java/Node/Python/.NET 문서가 각 binding 코드와 맞는지 확인한다.
6. `generation == 0` unchecked ref 의미가 모든 언어 문서에 반영됐는지 확인한다.
7. `zlink_spot_node_remote_actor_ref` 같은 제거된 이름이 남아 있지 않은지 검색한다.
8. mismatch가 있으면 문서 또는 binding 코드를 수정한다.
9. 수정 뒤 해당 언어 테스트와 sample smoke를 다시 실행한다.
10. 회차 결과를
    `doc/plan/spot-actor-dispatch/logs/bindings-spec-review-log.ko.md`에 기록한다.

종료 조건:

- [x] 1차 bindings spec 리뷰 완료
- [x] 2차 bindings spec 리뷰 완료
- [x] 3차 bindings spec 리뷰 완료
- [x] 4차 bindings spec 리뷰 완료
- [x] 5차 bindings spec 리뷰 완료
- [x] 5차에서 mismatch가 발견되면 수정 후 5차를 처음부터 다시 수행
- [x] draft spec에 있는 binding 적용 대상이 `doc/spec/bindings`에 모두 반영됨
- [x] 특히 언어별 README가 각 언어 API 표면과 일치함

### Bindings 코드와 spec 반복 리뷰

bindings spec 5회 리뷰가 끝난 뒤에도, spec 문서에 적힌 내용이 실제 bindings
라이브러리에 모두 적용되었는지 반복 리뷰한다.

반복 절차:

1. 언어별 spec에서 API 표면을 추출한다.
2. 해당 언어 binding 코드에서 실제 노출 API를 추출한다.
3. spec-only API와 code-only API를 찾는다.
4. ownership과 error mapping을 테스트와 대조한다.
5. mismatch가 있으면 코드, 문서, 테스트를 수정한다.
6. 해당 언어 sample과 perf smoke를 다시 실행한다.
7. mismatch가 없으면 같은 언어를 한 번 더 반복 검토한다.
8. 두 번 연속 mismatch가 없으면 해당 언어를 완료로 본다.

모든 언어가 완료되기 전에는 다음 release 또는 최종 종료 단계로 넘어가지 않는다.

### Bindings sample과 perf 검증

각 언어별로 sample과 perf도 동일한 방식으로 확인한다. 한 언어의 sample/perf가
완료되기 전에는 다음 언어로 넘어가지 않는다.

공통 sample 조건:

- [x] 기존 sample runner가 성공한다.
- [x] Actor room server sample이 성공한다.
- [x] gateway session에서 remote play server Actor로 relay하는 sample이 성공한다.
- [x] single-player queue serialization sample이 성공한다.

공통 perf 조건:

- [x] single perf smoke 성공
- [x] multi perf smoke 성공
- [x] size sweep `64,1024,4096,65536` 성공
- [x] `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` 관련 smoke 성공
- [x] 실패하면 원인 수정 뒤 해당 언어의 sample/perf 검증을 처음부터 다시 수행

언어별 perf runner가 없는 경우에는 그 이유를 bindings update log에 기록하고
해당 언어의 테스트와 sample smoke를 더 엄격하게 실행한다. perf runner가 있는데
runtime 환경이 없어 실행하지 못한 경우에는 blocker log에 환경 사유를 기록한다.

### Bindings POSD 리팩토링

bindings 쪽 라이브러리도 POSD 기반 리팩토링을 진행한다. 전체 bindings를 동시에
바꾸지 않고 언어별로 하나씩 진행한다.

언어별 POSD 체크리스트:

- [x] binding이 core C API의 얕은 pass-through만 늘리지 않는지 확인
- [x] 언어별 idiom에 맞는 깊은 모듈로 감쌌는지 확인
- [x] ownership과 close/dispose 규칙이 호출자에게 과하게 새지 않는지 확인
- [x] unchecked/checked ref 의미가 여러 helper에 중복 구현되지 않는지 확인
- [x] error/result mapping이 한 곳에서 관리되는지 확인
- [x] sample이 내부 구현 세부 사항에 의존하지 않는지 확인
- [x] perf wrapper가 core perf 지식을 불필요하게 중복하지 않는지 확인

반복 종료 조건:

- [x] 해당 언어에서 두 번 연속 새 POSD 리팩토링 후보가 없다.
- [x] 해당 언어 tests/sample/perf smoke가 통과한다.
- [x] 해당 언어 spec 문서와 코드 mismatch가 없다.
- [x] 결과를 `doc/plan/spot-actor-dispatch/logs/bindings-posd-refactor-log.ko.md`에 기록했다.

모든 언어가 위 조건을 만족하면 bindings 전체 POSD 리팩토링 완료로 본다.

## 최종 종료 절차

1. 전체 build/test를 실행한다.
2. contract matrix의 모든 행이 `reviewed`인지 확인한다.
3. 문서-코드 반복 리뷰 종료 조건을 확인한다.
4. POSD 리팩토링 종료 조건을 확인한다.
5. POSD 후 최종 문서 업데이트와 3회 리뷰 종료 조건을 확인한다.
6. sample과 perf smoke 검증 종료 조건을 확인한다.
7. core release와 bindings native library 최신화 종료 조건을 확인한다.
8. bindings spec 5회 리뷰 종료 조건을 확인한다.
9. bindings 코드와 spec 반복 리뷰 종료 조건을 확인한다.
10. bindings sample/perf 검증 종료 조건을 확인한다.
11. bindings POSD 리팩토링 종료 조건을 확인한다.
12. public API diff를 확인한다.
13. removed API reference가 남아 있지 않은지 검색한다.
14. draft spec이 정식 문서와 연결되어 있는지 확인한다.
15. 회귀 테스트 ID 전체가 테스트 파일에 매핑되어 있는지 확인한다.
16. `git diff --stat`으로 변경 범위를 확인한다.
17. bindings 최신화와 문서 반영 변경을 별도 commit으로 정리하고 push한다.
18. 최종 커밋 메시지에는 기능 구현, 문서 반영, core release, bindings 최신화,
    POSD 리팩토링을 분리해서 적는다.

## 최종 보고 형식

최종 보고에는 아래 항목만 간결하게 남긴다.

- 구현된 API 요약
- 제거된 API 요약
- contract matrix 완료 결과
- 테스트 결과
- sample smoke 결과
- perf single/multi size smoke 결과
- 문서 반영 결과
- 문서 3회 리뷰 결과
- core release 결과와 tag
- bindings native library 최신화 결과
- bindings spec 5회 리뷰 결과
- bindings 언어별 sample/perf 결과
- bindings 언어별 POSD 리팩토링 결과
- POSD 리팩토링 결과
- 남은 위험 또는 환경상 실행하지 못한 검증
