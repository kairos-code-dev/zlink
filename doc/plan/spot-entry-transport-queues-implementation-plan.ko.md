# SPOT Entry Transport Queues 구현 실행 계획

## 상태

이 문서는 `doc/spec/draft/spot-entry-transport-queues.ko.md`를 실제 구현, 테스트,
정식 문서 반영, release, bindings 배포까지 옮기기 위한 Codex 에이전트 실행 계획이다.

기존 `doc/plan/spot-actor-dispatch-implementation-plan.ko.md`는 형식과 gate 구성만
참고한다. 그 문서의 완료 상태, 오래된 API 목록, 과거 contract matrix 항목은 이 계획에
복사하지 않는다. 이 계획의 단일 기준은 활성 draft spec인
`doc/spec/draft/spot-entry-transport-queues.ko.md`다.

이 계획에서 작업자는 항상 Codex 에이전트다. Codex 에이전트는 사용자에게 중간 설계
결정을 묻지 않고 draft spec, contract matrix, 이 plan의 gate를 기준으로 끝까지 진행한다.
draft spec과 plan이 충돌하면 draft spec을 먼저 고친 뒤 plan, 코드, 테스트, 문서를
그 순서로 맞춘다.

체크박스는 실제 구현과 검증이 끝났을 때만 완료로 바꾼다. 단순히 코드가 존재하거나
테스트가 일부 통과했다는 이유로 완료 처리하지 않는다.

## 기준 문서

- 활성 draft spec: [`doc/spec/draft/spot-entry-transport-queues.ko.md`](../spec/draft/spot-entry-transport-queues.ko.md)
- POSD 원칙: [`doc/principal/software-design-principles.md`](../principal/software-design-principles.md)
- 기존 공개 C header: [`core/include/zlink.h`](../../core/include/zlink.h)
- 기존 enum/result header: [`core/include/zlink_enum.h`](../../core/include/zlink_enum.h),
  [`core/include/zlink_errno.h`](../../core/include/zlink_errno.h)
- 내부 구조 참고: [`doc/internals/spot-internals.ko.md`](../internals/spot-internals.ko.md),
  [`doc/internals/stream-socket.ko.md`](../internals/stream-socket.ko.md),
  [`doc/internals/discovery-internals.ko.md`](../internals/discovery-internals.ko.md)
- release 후 bindings native library 갱신 스크립트:
  [`bindings/update_zlink_libs.sh`](../../bindings/update_zlink_libs.sh)

## Draft Spec 확인 규칙

Codex 에이전트는 각 단계 시작 전에 아래 표의 draft spec 절을 반드시 열고 해당 절의
계약을 확인한다. plan 체크리스트는 순서와 gate를 나누기 위한 도구이고 세부 계약은
항상 draft spec이 우선한다.

| plan 단계 | 반드시 확인할 draft spec 절 |
|-----------|------------------------------|
| 단계 0 | [목적](../spec/draft/spot-entry-transport-queues.ko.md#목적), [Public C API 변경 요약](../spec/draft/spot-entry-transport-queues.ko.md#public-c-api-변경-요약), [구현 순서](../spec/draft/spot-entry-transport-queues.ko.md#구현-순서), [비목표](../spec/draft/spot-entry-transport-queues.ko.md#비목표) |
| 단계 1 | [Public C API 변경 요약](../spec/draft/spot-entry-transport-queues.ko.md#public-c-api-변경-요약), [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경) |
| 단계 2 | [핵심 모델](../spec/draft/spot-entry-transport-queues.ko.md#핵심-모델), [Entry Spot](../spec/draft/spot-entry-transport-queues.ko.md#entry-spot), [Spot 조회](../spec/draft/spot-entry-transport-queues.ko.md#spot-조회) |
| 단계 3 | [Actor ref](../spec/draft/spot-entry-transport-queues.ko.md#actor-ref), [Actor 생성](../spec/draft/spot-entry-transport-queues.ko.md#actor-생성), [Actor 조회](../spec/draft/spot-entry-transport-queues.ko.md#actor-조회), [Remote Actor create-or-get](../spec/draft/spot-entry-transport-queues.ko.md#remote-actor-create-or-get), [Actor destroy](../spec/draft/spot-entry-transport-queues.ko.md#actor-destroy) |
| 단계 4 | [Actor message 처리 위치](../spec/draft/spot-entry-transport-queues.ko.md#actor-message-처리-위치), [Dispatch event 통합](../spec/draft/spot-entry-transport-queues.ko.md#dispatch-event-통합), [Queue와 backpressure](../spec/draft/spot-entry-transport-queues.ko.md#queue와-backpressure) |
| 단계 5 | [Actor join](../spec/draft/spot-entry-transport-queues.ko.md#actor-join), [Local join process](../spec/draft/spot-entry-transport-queues.ko.md#local-join-process), [Remote join process](../spec/draft/spot-entry-transport-queues.ko.md#remote-join-process), [Actor leave](../spec/draft/spot-entry-transport-queues.ko.md#actor-leave) |
| 단계 6 | [STREAM session과 Actor 연결](../spec/draft/spot-entry-transport-queues.ko.md#stream-session과-actor-연결), [Session과 local Actor](../spec/draft/spot-entry-transport-queues.ko.md#session과-local-actor), [Session과 remote Actor](../spec/draft/spot-entry-transport-queues.ko.md#session과-remote-actor) |
| 단계 7 | [Spot socket 제거 모델](../spec/draft/spot-entry-transport-queues.ko.md#spot-socket-제거-모델), [Routed request 처리](../spec/draft/spot-entry-transport-queues.ko.md#routed-request-처리), [Pub/sub 처리](../spec/draft/spot-entry-transport-queues.ko.md#pubsub-처리), [Channel dealer 처리](../spec/draft/spot-entry-transport-queues.ko.md#channel-dealer-처리) |
| 단계 8 | [Snapshot과 monitoring](../spec/draft/spot-entry-transport-queues.ko.md#snapshot과-monitoring) |
| 단계 9 | [Actor channel API 여부](../spec/draft/spot-entry-transport-queues.ko.md#actor-channel-api-여부), [Channel router에서 Actor로 직접 messaging](../spec/draft/spot-entry-transport-queues.ko.md#channel-router에서-actor로-직접-messaging), [비목표](../spec/draft/spot-entry-transport-queues.ko.md#비목표) |
| 단계 10 | [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) 전체 |
| 단계 11 | [Actor와 Entry Spot 흐름](../spec/draft/spot-entry-transport-queues.ko.md#actor와-entry-spot-흐름), [Gateway/session 흐름](../spec/draft/spot-entry-transport-queues.ko.md#gatewaysession-흐름), [Game room 흐름](../spec/draft/spot-entry-transport-queues.ko.md#game-room-흐름), [Single-player 흐름](../spec/draft/spot-entry-transport-queues.ko.md#single-player-흐름) |
| 문서-코드 반복 리뷰 | [draft spec 전체](../spec/draft/spot-entry-transport-queues.ko.md), 특히 [Public C API 변경 요약](../spec/draft/spot-entry-transport-queues.ko.md#public-c-api-변경-요약), [Public API 변경](../spec/draft/spot-entry-transport-queues.ko.md#public-api-변경), [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) |
| POSD 리팩토링 | [draft spec 전체](../spec/draft/spot-entry-transport-queues.ko.md)와 [software-design-principles.md](../principal/software-design-principles.md) |
| Core release와 bindings 최신화 | [Public C API 변경 요약](../spec/draft/spot-entry-transport-queues.ko.md#public-c-api-변경-요약), [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) |
| Bindings 순차 적용 | [Public C API 변경 요약](../spec/draft/spot-entry-transport-queues.ko.md#public-c-api-변경-요약), 각 API 상세 계약 절, [회귀 테스트](../spec/draft/spot-entry-transport-queues.ko.md#회귀-테스트) |

각 단계 시작 전 implementation review log에 아래 형식으로 기록한다.

- 날짜:
- 단계:
- 확인한 draft spec 절:
- 이번 단계에서 구현할 계약:
- 이번 단계에서 구현하지 않는 계약:
- 관련 회귀 테스트 ID:
- 남은 위험:

## Contract Matrix Gate

구현 전에 draft spec에서 구현 단위를 추출해 contract matrix를 새로 만든다. 기존
`spot-actor-dispatch` matrix는 참고하지 않는다.

matrix 경로:

- `doc/plan/spot-entry-transport-queues/logs/contract-matrix.ko.md`

matrix는 최소한 아래 컬럼을 가진다.

| 컬럼 | 의미 |
|------|------|
| Contract ID | 고정 ID. 예: `ENTRY-API-001`, `ACTOR-BEH-001`, `QUEUE-PUB-001` |
| Contract Kind | 아래 허용 값 중 하나 |
| Draft Link | draft spec의 정확한 절 링크 |
| Contract Text | 구현해야 하는 계약 요약 |
| Public API / Enum / Struct | 관련 공개 표면. 없으면 `N/A` |
| Implementation Owner | 구현 파일 또는 모듈. 구현 전에는 예상 경로 |
| Test ID | 관련 `ENTRY-*`, `ENTRY-ACTOR-*`, `QUEUE-*` 테스트. 없으면 draft에 먼저 추가 |
| Binding Impact | `none`, `all`, 또는 언어 목록 |
| Doc Impact | `spec`, `guide`, `internals`, `bindings`, `sample` 중 해당 항목 |
| Status | `planned`, `implemented`, `tested`, `documented`, `reviewed` |

허용 `Contract Kind`:

- `new-api`
- `changed-api`
- `removed-api`
- `new-type`
- `new-enum`
- `new-option`
- `behavior`
- `ownership`
- `timeout`
- `test`
- `non-goal`
- `existing-reference`

matrix 생성 시 반드시 추출할 항목:

- [x] 모든 신규, 변경, 제거 public C API
- [x] 모든 신규, 변경, 제거 enum 값, result code, constant, option
- [x] 모든 public struct, enum, callback typedef
- [x] `generation == 0` unchecked ref와 checked ref generation 계약
- [x] Actor 생성, 조회, destroy 계약
- [x] remote create-or-get 계약
- [x] join, leave, remote handoff, JoinOp, request owner 계약
- [x] join recv/reply message ownership, duplicate reply, late reply 계약
- [x] STREAM session과 Actor binding, local/remote relay 계약
- [x] Discovery active route publish, update, cleanup 시점
- [x] Actor readable dispatch subject lifetime 계약
- [x] Spot socket 제거, routed queue, pub/sub fanout, channel reply queue 계약
- [x] snapshot과 monitoring 계약
- [x] 제거 대상 API
- [x] 모든 `ENTRY-*`, `ENTRY-ACTOR-*`, `QUEUE-*` 회귀 테스트
- [x] 비목표 항목

초기 matrix 작성 때 아래 심볼은 draft spec에서 비교, 기존 동작 변경, drain 대상,
또는 비목표 설명으로 등장할 수 있다. 새 구현 대상인지 기존 참조인지 바로 단정하지
말고 draft의 해당 절을 확인해 `changed-api`, `removed-api`, `non-goal`,
`existing-reference` 중 하나로 분류한다.

- 기존 common/Spot API:
  `zlink_spot_destroy()`, `zlink_set_routing_id()`, `zlink_get_routing_id()`,
  `zlink_spot_dispatch_event_handler()`
- 기존 routed, publish, subscribe, channel API:
  `zlink_spot_recv()`, `zlink_spot_publish_part()`, `zlink_spot_subscribe_part()`,
  `zlink_set_subscription()`, `zlink_unset_subscription()`,
  `zlink_subscription_at()`, `zlink_spot_subscription_event_recv()`,
  `zlink_spot_request_channel()`, `zlink_spot_channel_reply_progress_from()`
- 기존 dispatch drain 대상:
  `zlink_timer_recv()`
- 기존 result와 flag:
  `ZLINK_EXPORT`, `ZLINK_DONTWAIT`, `ZLINK_PART_MORE`, `ZLINK_PART_FINAL`,
  `ZLINK_CLOSE_BUSY`, `ZLINK_CONFIG_INVALID_HANDLE`,
  `ZLINK_CONFIG_INVALID_ARGUMENT`, `ZLINK_RECV_INVALID_HANDLE`,
  `ZLINK_REQUEST_OK`, `ZLINK_REQUEST_TIMED_OUT`, `ZLINK_REQUEST_NOT_FOUND`,
  `ZLINK_REQUEST_BUSY`, `ZLINK_REQUEST_INVALID_ARGUMENT`,
  `ZLINK_REQUEST_INVALID_STATE`, `ZLINK_SUBMIT_OK`,
  `ZLINK_SUBMIT_NOT_FOUND`, `ZLINK_SUBMIT_BACKPRESSURED`,
  `ZLINK_SUBMIT_INVALID_HANDLE`, `ZLINK_SUBMIT_INVALID_ARGUMENT`,
  `ZLINK_SUBMIT_INVALID_STATE`
- internal subscription option reference:
  `ZLINK_INTERNAL_OPT_SUBSCRIBE`, `ZLINK_INTERNAL_OPT_UNSUBSCRIBE`
- 비목표 API 이름:
  `zlink_spot_node_actor_request_channel_part()`,
  `zlink_spot_node_actor_send_channel_part()`

matrix 검증 명령:

```bash
comm -23 \
  <(rg -o '(ENTRY-ACTOR|ENTRY|QUEUE-[A-Z]+)-[0-9]+' doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) \
  <(rg -o '(ENTRY-ACTOR|ENTRY|QUEUE-[A-Z]+)-[0-9]+' doc/plan/spot-entry-transport-queues/logs/contract-matrix.ko.md | sort -u)

comm -23 \
  <(rg -o 'zlink_[A-Za-z0-9_]+\(' doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) \
  <(rg -o 'zlink_[A-Za-z0-9_]+\(' doc/plan/spot-entry-transport-queues/logs/contract-matrix.ko.md | sort -u)

comm -23 \
  <(rg -o 'ZLINK_[A-Z0-9_]+' doc/spec/draft/spot-entry-transport-queues.ko.md | sort -u) \
  <(rg -o 'ZLINK_[A-Z0-9_]+' doc/plan/spot-entry-transport-queues/logs/contract-matrix.ko.md | sort -u)
```

위 명령의 출력이 있으면 구현을 진행하지 않는다. 출력 항목을 구현 대상, 비목표,
또는 `existing-reference`로 분류해 matrix와 draft spec을 먼저 닫는다.

## 완료 조건

아래 조건이 모두 만족되어야 전체 작업을 완료로 본다.

- [x] contract matrix의 모든 행이 `reviewed` 상태다.
- [x] draft spec의 첫 구현 범위가 core에 모두 구현되어 있다.
- [x] public C API, enum, result code, constant, struct, callback typedef가 draft spec과
      public header에서 일치한다.
- [x] draft spec의 제거 API가 core, bindings, samples, 문서에서 사라졌다.
- [x] 모든 `ENTRY-*`, `ENTRY-ACTOR-*`, `QUEUE-*` 회귀 테스트가 자동 테스트로 닫혔다.
- [x] 기존 SPOT, STREAM, Discovery, Registry 회귀 테스트가 통과한다.
- [x] sample smoke와 perf smoke가 통과한다.
- [x] POSD 기반 리팩토링 루프를 반복했고 더 진행할 항목이 없다.
- [x] draft 내용을 정식 spec, guide, internals, bindings 문서에 목적별로 나누어 반영했다.
- [x] 문서 3회 리뷰에서 mismatch가 없다.
- [x] core release tag push와 GitHub Actions release를 확인했다.
- [x] `bindings/update_zlink_libs.sh`로 bindings native library를 최신화했다.
- [x] bindings를 언어별로 순차 적용하고 각 언어의 spec, sample, perf, POSD gate를 닫았다.
- [x] 최종 작업트리에는 의도한 변경만 남아 있다.

## 진행 원칙

- 사용자에게 중간 설계 결정을 묻지 않는다.
- draft spec에 없는 public 계약은 구현하지 않는다.
- 구현 중 계약 공백이 발견되면 draft spec을 먼저 수정한 뒤 plan, matrix, 코드, 테스트를
  맞춘다.
- 호환성 유예는 두지 않는다. draft spec이 제거로 분류한 API는 첫 구현에서 제거한다.
- Actor HWM option, Actor 전용 channel API, channel router에서 Actor로 직접 보내는
  protocol은 만들지 않는다.
- `generation == 0` unchecked remote ref는 invalid로 처리하지 않는다.
- Actor readable dispatch subject는 `void *actor` handle이 아니라 callback lifetime의
  `const zlink_actor_ref_t *`다.
- perf는 `core/build` runtime을 기준으로만 판단한다.
- core release는 version bump, commit, push, `core/vX.Y.Z` tag push 뒤 GitHub Actions를
  CLI로 확인한다.
- bindings 작업은 `c`, `cpp`, `dotnet`, `go`, `java`, `node`, `python`, `rust` 순서로
  하나씩 순차 진행한다. repo에 없는 언어는 blocker가 아니라 해당 없음으로 로그에 남긴다.

## 산출물과 로그

로그 디렉터리:

- `doc/plan/spot-entry-transport-queues/logs/`

필수 로그:

- `contract-matrix.ko.md`
- `blockers.ko.md`
- `implementation-review-log.ko.md`
- `sample-perf-smoke-log.ko.md`
- `posd-refactor-log.ko.md`
- `final-doc-review-log.ko.md`
- `core-release-log.ko.md`
- `bindings-update-log.ko.md`
- `bindings-spec-review-log.ko.md`
- `bindings-posd-refactor-log.ko.md`

로그 공통 형식:

- 날짜:
- 대상:
- 수행한 명령:
- 확인한 draft spec 절:
- 발견한 문제:
- 수정한 파일:
- 검증 결과:
- 남은 위험:
- 다음 확인:

## 단계 0. 기준 상태와 matrix 고정

작업트리, baseline, draft spec, public surface를 먼저 고정한다.

- [x] `git status --short` 확인
- [x] 현재 branch 확인
- [x] 활성 draft spec 경로와 hash 기록
- [x] 기존 public header의 API, enum, result code 목록 기록
- [x] 기존 core build/test 명령 확인
- [x] baseline build 실행 또는 기존 실패 기록
- [x] baseline test 실행 또는 기존 실패 기록
- [x] 로그 디렉터리 생성
- [x] 필수 로그 파일 생성
- [x] contract matrix 생성
- [x] matrix와 draft spec의 test ID 목록 대조 결과가 비어 있음
- [x] matrix와 draft spec의 `zlink_*` 목록 대조 결과가 비어 있음
- [x] matrix와 draft spec의 `ZLINK_*` 목록 대조 결과가 비어 있음

## 단계 1. Public C Surface 반영

draft spec의 `Public C API 변경 요약`과 `Public API 변경`을 기준으로 public header를
먼저 닫는다.

### 1.1 새 상수, enum, result code

- [x] `ZLINK_ACTOR_ID_MAX` 추가
- [x] `ZLINK_ACTOR_JOIN_INFO_REMOTE` 추가
- [x] `ZLINK_ACTOR_ADMISSION_ACCEPT` 추가
- [x] `ZLINK_ACTOR_ADMISSION_REJECT` 추가
- [x] `ZLINK_ACTOR_CREATE_CREATED` 추가
- [x] `ZLINK_ACTOR_CREATE_EXISTING` 추가
- [x] `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE` 추가 또는 기존 값 확인
- [x] `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE` 추가 또는 기존 값 확인
- [x] `ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR` 추가 또는 기존 값 확인
- [x] `ZLINK_CONFIG_INVALID_STATE = 705` 추가
- [x] `ZLINK_CONFIG_NOT_FOUND = 706` 추가
- [x] enum 숫자 충돌 없음 확인
- [x] Actor HWM option이 추가되지 않았음 확인

### 1.2 새 public type

- [x] `zlink_actor_ref_t`
- [x] `zlink_actor_recv_info_t`
- [x] `zlink_actor_join_info_t`
- [x] `zlink_actor_admission_result_t`
- [x] `zlink_actor_admission_handler_fn`
- [x] `zlink_actor_create_status_t`
- [x] `zlink_actor_create_result_t`
- [x] C/C++ header compile 확인
- [x] ABI 크기와 alignment 검토

### 1.3 신규, 변경, 제거 API

- [x] `zlink_spot_node_entry_spot()`
- [x] `zlink_spot_node_spot_lookup()`
- [x] `zlink_spot_node_actor_recv_part()`
- [x] `zlink_spot_node_actor_destroy()`
- [x] `zlink_spot_node_actor_send_bound_session_msg()`
- [x] `zlink_spot_node_actor_close_bound_session()`
- [x] `zlink_spot_node_actor_admission_handler()` 유지와 contract 변경 확인
- [x] `zlink_spot_node_create_remote_actor()` 유지와 contract 변경 확인
- [x] `zlink_spot_actor_join_recv()` 유지와 contract 변경 확인
- [x] `zlink_spot_actor_join_reply()` 유지와 contract 변경 확인
- [x] `zlink_spot_node_actor_new()` 시그니처 변경
- [x] `zlink_spot_node_actor_join_spot()` 시그니처 변경
- [x] `zlink_spot_node_actor_leave_spot()` 시그니처 변경
- [x] `zlink_actor_destroy()` 제거
- [x] `zlink_actor_get_ref()` 제거
- [x] `zlink_actor_join_spot()` 제거
- [x] `zlink_actor_leave_spot()` 제거
- [x] `zlink_actor_recv_part()` 제거
- [x] `zlink_spot_node_destroy_remote_actor()` 제거
- [x] 제거 API 참조가 core build에 남아 있지 않음

## 단계 2. SpotNode Logical Spot과 Entry Spot

`Spot` facade에서 physical socket ownership을 분리하기 전에 logical Spot state와 Entry
Spot lifecycle을 고정한다.

- [x] logical Spot state를 `SpotNode` 소유 table로 분리
- [x] `SpotNode` 생성 시 Entry Spot logical state 생성
- [x] Entry Spot은 node destroy 전까지 제거 불가
- [x] Entry Spot facade lookup 구현
- [x] Entry Spot multiple facade가 같은 logical state를 가리킴
- [x] 일반 Spot facade reference count 구현
- [x] `zlink_spot_node_spot_lookup()` 구현
- [x] lookup 성공 시 owned facade 반환
- [x] lookup not found 시 `ZLINK_CONFIG_NOT_FOUND` 반환
- [x] 일반 Spot 마지막 facade close에서 joined Actor나 pending join이 있으면 busy
- [x] Entry Spot rid 설정과 조회 지원
- [x] Entry Spot rid configuration phase lock 구현
- [x] 일반 Spot rid 변경 시 lookup index 원자 갱신
- [x] Spot snapshot에 Entry Spot 포함

회귀 테스트:

- [x] ENTRY-01
- [x] ENTRY-02
- [x] ENTRY-03
- [x] ENTRY-04
- [x] ENTRY-05
- [x] ENTRY-06
- [x] ENTRY-07
- [x] ENTRY-08
- [x] ENTRY-09
- [x] ENTRY-10
- [x] ENTRY-11
- [x] ENTRY-12
- [x] ENTRY-13
- [x] ENTRY-14
- [x] ENTRY-15
- [x] ENTRY-16

## 단계 3. Actor Ref와 Lifecycle

Actor public handle을 제거하고 `node + zlink_actor_ref_t` 모델로 전환한다.

- [x] Actor table을 `SpotNode`가 소유
- [x] `actor_id` 최대 255 bytes와 NUL 종료 검증
- [x] 같은 node 안 live Actor id 중복 거부
- [x] 서로 다른 node의 같은 Actor id 허용
- [x] checked generation을 node-local monotonic non-zero 값으로 발급
- [x] `generation == 0` unchecked ref 지원
- [x] stale checked ref 검출
- [x] Actor 생성 성공 시 Entry Spot membership 자동 설정
- [x] `zlink_spot_node_actor_new()` 구현
- [x] `zlink_spot_node_actor_lookup()` checked ref 조회 구현
- [x] `zlink_remote_actor_get_ref()` unchecked ref 생성 유지
- [x] `zlink_spot_node_actor_admission_handler()` 구현과 handler 등록/해제
- [x] remote create-or-get 구현
- [x] `zlink_spot_node_create_remote_actor()` 구현
- [x] 이미 있는 Actor는 `EXISTING` 반환하고 current Spot 유지
- [x] Actor가 없을 때만 admission handler 호출
- [x] remote create 성공만으로 active route publish하지 않음
- [x] `zlink_spot_node_actor_destroy()` 구현
- [x] destroy는 Entry Spot에서만 허용
- [x] user Spot Actor destroy 실패
- [x] join pending 중 destroy 실패
- [x] bound session cleanup 뒤 destroy
- [x] destroy는 STREAM client connection을 직접 닫지 않음

회귀 테스트:

- [x] ENTRY-ACTOR-01
- [x] ENTRY-ACTOR-08
- [x] ENTRY-ACTOR-09
- [x] ENTRY-ACTOR-11
- [x] ENTRY-ACTOR-12
- [x] ENTRY-ACTOR-13
- [x] ENTRY-ACTOR-33
- [x] ENTRY-ACTOR-42
- [x] ENTRY-ACTOR-45

## 단계 4. Actor Message Dispatch와 Recv

Actor는 transport socket이나 독립 dispatch context를 소유하지 않는다. Actor message는
Actor unread state에 쌓이고 current Spot dispatch에서 drain한다.

- [x] Actor unread state 구현
- [x] multipart part order와 final flag 보존
- [x] `ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE` 발행
- [x] `zlink_spot_dispatch_event_handler()`가 Actor readable event를 전달
- [x] dispatch `subject_kind = ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR`
- [x] dispatch `subject = const zlink_actor_ref_t *`
- [x] subject pointer callback lifetime 보장
- [x] application이 복사한 Actor ref로 drain 가능
- [x] `zlink_spot_node_actor_recv_part()` 구현
- [x] non-owner node recv 실패
- [x] NULL output pointer recv failure 정책 구현
- [x] no-data 처리
- [x] Actor queue FIFO가 join/leave 전후 보존
- [x] Spot/Actor logical queue HWM option 없음 유지

회귀 테스트:

- [x] ENTRY-ACTOR-02
- [x] ENTRY-ACTOR-10
- [x] ENTRY-ACTOR-46
- [x] ENTRY-ACTOR-50

## 단계 5. Actor Join, Leave, Remote Handoff

join은 current Spot에서 target Spot으로 가는 이동이다. leave는 Entry Spot으로 돌아가는
축약 동작이다.

- [x] `zlink_spot_node_actor_join_spot()` submit 단계와 async completion 분리
- [x] `dest_node_rid_`가 Actor owner와 같으면 local join
- [x] `dest_node_rid_`가 다르면 remote join handoff
- [x] session 없는 Actor는 Entry Spot 밖으로 join 불가
- [x] same target Spot idempotent async success
- [x] pending join 중 새 join busy
- [x] target Spot join request queue 구현
- [x] `zlink_spot_actor_join_recv()` 구현
- [x] `zlink_spot_actor_join_reply()` 구현
- [x] join reply accepted 값 검증
- [x] join request/reply message ownership 구현
- [x] duplicate reply와 late reply invalid-state
- [x] `zlink_actor_join_info_t.request` opaque lifetime 보장
- [x] local join accept atomic switch
- [x] local join reject/timeout source 유지
- [x] remote join pending Actor state 생성
- [x] remote join prepare는 remote create admission handler를 호출하지 않음
- [x] remote join target pending Actor는 live lookup과 active route에 노출하지 않음
- [x] source node JoinOp과 reply path 유지
- [x] session Actor list compare-and-swap visibility point 구현
- [x] visibility point 전 relay는 source Actor
- [x] visibility point 뒤 relay는 target pending 또는 active Actor
- [x] commit visible OK 뒤 source Actor retire
- [x] target accept만으로 source Actor 제거하지 않음
- [x] session disconnect before visibility abort
- [x] session disconnect after visibility target cleanup
- [x] `zlink_spot_node_actor_leave_spot()` 구현
- [x] leave는 current Spot stale check 수행
- [x] join pending 중 leave busy
- [x] leave 성공 뒤 Entry Spot readable event
- [x] leave는 Actor queue를 비우지 않음

회귀 테스트:

- [x] ENTRY-ACTOR-03
- [x] ENTRY-ACTOR-04
- [x] ENTRY-ACTOR-05
- [x] ENTRY-ACTOR-06
- [x] ENTRY-ACTOR-07
- [x] ENTRY-ACTOR-14
- [x] ENTRY-ACTOR-15
- [x] ENTRY-ACTOR-16
- [x] ENTRY-ACTOR-17
- [x] ENTRY-ACTOR-18
- [x] ENTRY-ACTOR-19
- [x] ENTRY-ACTOR-20
- [x] ENTRY-ACTOR-23
- [x] ENTRY-ACTOR-24
- [x] ENTRY-ACTOR-25
- [x] ENTRY-ACTOR-26
- [x] ENTRY-ACTOR-27
- [x] ENTRY-ACTOR-28
- [x] ENTRY-ACTOR-29
- [x] ENTRY-ACTOR-30
- [x] ENTRY-ACTOR-31
- [x] ENTRY-ACTOR-32
- [x] ENTRY-ACTOR-34
- [x] ENTRY-ACTOR-35
- [x] ENTRY-ACTOR-36
- [x] ENTRY-ACTOR-37
- [x] ENTRY-ACTOR-38
- [x] ENTRY-ACTOR-39
- [x] ENTRY-ACTOR-40
- [x] ENTRY-ACTOR-41
- [x] ENTRY-ACTOR-47
- [x] ENTRY-ACTOR-49

## 단계 6. STREAM Session과 Actor 연결

session owner node와 Actor owner node를 분리해서 local Actor와 remote Actor를 같은
public ref 모델로 처리한다.

- [x] session owner가 `session -> actor_id -> Actor ref` mapping 유지
- [x] 한 session에 여러 Actor bind 허용
- [x] 한 Actor는 하나의 bound STREAM session만 가짐
- [x] Actor active route는 Actor 생성이 아니라 STREAM bind 성공 시 publish
- [x] bind 후 Entry Spot rid 또는 current Spot rid가 route에 반영
- [x] remote join commit 성공 시 session mapping이 target Actor ref로 갱신
- [x] session disconnect cleanup은 Actor를 Entry Spot으로 이동
- [x] user Spot Actor explicit unbind는 실패
- [x] `zlink_spot_node_actor_send_bound_session_msg()` 구현
- [x] remote Actor send는 fire-and-forget submit semantics 준수
- [x] stale remote send drop과 protocol drop counter 내부 처리
- [x] `zlink_spot_node_actor_close_bound_session()` 구현
- [x] close 성공 뒤 Actor는 Entry Spot으로 이동
- [x] close 성공 뒤 unread message가 있으면 Entry Spot readable event 발행

회귀 테스트:

- [x] ENTRY-ACTOR-21
- [x] ENTRY-ACTOR-22
- [x] ENTRY-ACTOR-43
- [x] ENTRY-ACTOR-44

## 단계 7. Spot Socket 제거와 Queue/Fanout

Spot facade는 physical socket을 직접 소유하지 않고 logical queue와 handler reference만
가진다. physical transport는 `SpotNode`가 소유한다.

- [x] Spot facade에서 physical pub/sub/routed socket pointer 제거
- [x] `zlink_spot_destroy()`가 Entry Spot과 일반 Spot reference count 계약을 따른다
- [x] routed ingress를 target Spot logical routed queue에 enqueue
- [x] `zlink_spot_recv()`를 logical routed queue drain으로 변경
- [x] publish path를 SpotNode node-owned transport와 local fanout에 연결
- [x] `zlink_spot_publish_part()`를 node-owned publish path에 연결
- [x] subscriber 없음 publish success
- [x] publish dead Spot 또는 shutdown failure 구현
- [x] node-level subscription registry 구현
- [x] `zlink_set_subscription()`은 node-level subscription registry를 갱신
- [x] `zlink_unset_subscription()`은 node-level subscription registry를 갱신
- [x] `zlink_subscription_at()`은 logical Spot filter set을 조회
- [x] `zlink_spot_subscribe_part()`는 logical subscribe queue를 drain
- [x] `zlink_spot_subscription_event_recv()`는 peer subscription event queue를 drain
- [x] duplicate subscribe/unsubscribe no-op
- [x] same filter union subscribe와 ref-count 구현
- [x] physical SUB greedy drain 제어 구현
- [x] fanout shared message block과 ref-count 구현
- [x] single target fast path도 public 동작 동일성 유지
- [x] mutable returned message의 copy-on-write 또는 독립 handle 보장
- [x] exact와 pattern 중복 match dedupe
- [x] channel reply completion을 logical Spot queue로 이동
- [x] channel dealer shared transport에서 completion 분리
- [x] `zlink_spot_request_channel()` completion이 요청 Spot의 channel reply queue로 들어감
- [x] `zlink_spot_channel_reply_progress_from()`으로 channel reply queue를 drain

회귀 테스트:

- [x] QUEUE-ROUTED-01
- [x] QUEUE-ROUTED-02
- [x] QUEUE-ROUTED-03
- [x] QUEUE-PUB-01
- [x] QUEUE-PUB-02
- [x] QUEUE-PUB-03
- [x] QUEUE-PUB-04
- [x] QUEUE-PUB-05
- [x] QUEUE-PUB-06
- [x] QUEUE-PUB-07
- [x] QUEUE-SUB-01
- [x] QUEUE-SUB-02
- [x] QUEUE-SUB-03
- [x] QUEUE-SUB-04
- [x] QUEUE-SUB-05
- [x] QUEUE-SUB-06
- [x] QUEUE-SUB-07
- [x] QUEUE-CHAN-01
- [x] QUEUE-CHAN-02
- [x] QUEUE-SOCKET-01

## 단계 8. Snapshot과 Monitoring

새 detail snapshot API는 만들지 않는다. 기존 snapshot API의 의미를 draft spec에 맞춘다.

- [x] `zlink_spot_node_spots_snapshot()`에 Entry Spot 포함
- [x] Spot row `joined_actor_count`가 Entry Spot Actor 수를 반환
- [x] Spot row `pending_actor_join_count`가 join request queue를 반영
- [x] `zlink_spot_node_actors_snapshot()` live local Actor 목록 반환
- [x] Actor row `joined`는 live Actor에서 항상 1
- [x] Actor row `joined_spot_rid`는 current Spot rid
- [x] `zlink_spot_actors_snapshot()`이 특정 Spot Actor ref 목록 반환
- [x] Entry Spot facade로 Entry Spot Actor 목록 조회 가능
- [x] queue backlog, protocol drop, transport backpressure count는 새 public snapshot에 넣지 않음

회귀 테스트:

- [x] ENTRY-ACTOR-48
- [x] 단계 2의 Entry Spot snapshot 테스트를 snapshot 단계에서 다시 실행한다

## 단계 9. 비목표와 제거 대상 검증

첫 구현 범위 밖 항목이 코드나 문서에 새 public 계약처럼 들어가지 않았는지 검증한다.

- [x] Actor 전용 dispatch context 없음
- [x] Actor 전용 recv callback 없음
- [x] Actor 전용 channel request public API 없음
- [x] `zlink_spot_node_actor_request_channel_part()` public API 없음
- [x] `zlink_spot_node_actor_send_channel_part()` public API 없음
- [x] channel router에서 Actor로 직접 보내는 protocol 없음
- [x] bound STREAM session 없이 user Spot에 머무는 backend-only Actor 없음
- [x] reliable pub/sub protocol 없음
- [x] Actor placement 자동 정책 없음
- [x] Entry Spot application policy 없음
- [x] framework typed Actor 객체 생성 없음
- [x] 제거 API public header, core samples, 정식 문서에 남아 있지 않음

## 단계 10. Core 회귀 테스트와 전체 검증

draft spec 회귀 테스트와 기존 core 테스트를 모두 닫는다.

- [x] 모든 `ENTRY-*` 테스트 자동화
- [x] 모든 `ENTRY-ACTOR-*` 테스트 자동화
- [x] 모든 `QUEUE-*` 테스트 자동화
- [x] core build 성공
- [x] core unit/integration tests 성공
- [x] 기존 SPOT tests 성공
- [x] 기존 STREAM tests 성공
- [x] 기존 Discovery tests 성공
- [x] 기존 Registry tests 성공
- [x] public header compile check 성공
- [x] removed symbol reference check 성공
- [x] stale API name `rg` check 성공(core/header/core samples/정식 문서 범위)
- [x] `generation == 0` invalid 처리 잔존 없음
- [x] Actor HWM option 잔존 없음

## 단계 11. Sample과 Perf Smoke

core runtime을 바꾼 뒤에는 반드시 `core/build` 기준 runtime을 다시 빌드하고 sample/perf를
검증한다.

- [x] `cmake --build core/build` 실행
- [x] perf runner가 `core/build`의 `libzlink.so` 경로를 출력
- [x] stale runtime이면 perf를 중단
- [x] Entry Spot 기반 Actor 생성/초기 dispatch sample 추가 또는 갱신
- [x] local room join/leave sample 추가 또는 갱신
- [x] session과 remote Actor relay sample 추가 또는 갱신
- [x] single-player queue serialization sample 추가 또는 갱신
- [x] sample build 성공
- [x] sample runner 성공
- [x] `bindings/c/perf/run_benchmarks_multi.sh` 실행
- [x] single perf smoke 성공
- [x] multi perf smoke 성공
- [x] `--transports tcp --msg-sizes 64` 패턴별 smoke 성공
- [x] `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` 관련 smoke 성공
- [x] 실패 시 원인 수정 뒤 sample/perf smoke를 처음부터 다시 실행

## 구현 후 문서-코드 반복 리뷰

core 구현과 테스트가 통과한 뒤 draft spec, core header, core 구현, tests, sample,
정식 문서를 반복 대조한다.

반복 절차:

1. draft spec의 public API, enum, type, option 목록을 추출한다.
2. `core/include`와 대조한다.
3. contract matrix의 구현 대상 행과 코드 owner를 대조한다.
4. 회귀 테스트 표와 실제 test 이름을 대조한다.
5. 제거 API 이름을 repo 전체에서 검색한다.
6. ownership, timeout, checked/unchecked ref, join/leave, remote handoff, queue/fanout
   계약을 코드와 비교한다.
7. mismatch가 있으면 draft spec을 먼저 고치고 코드와 테스트를 맞춘 뒤 처음부터 다시
   리뷰한다.

종료 조건:

- [x] contract matrix에 `reviewed`가 아닌 행이 없음
- [x] spec-only public API 없음
- [x] code-only public API 없음
- [x] 테스트 없는 계약 없음
- [x] 문서와 다른 errno/result 없음
- [x] 문서와 다른 ownership 규칙 없음
- [x] draft 첫 구현 범위의 미구현 항목 없음
- [x] 두 번 연속 mismatch 없음

## POSD 기반 전체 리팩토링 루프

기능 완료 뒤 repo 전체에 POSD 원칙을 적용한다. 단순 cleanup이 아니라 복잡성을 줄이는
리팩토링만 수행한다.

대상:

- [x] `core/include`
- [x] `core/src`
- [x] core tests
- [x] bindings pre-release 영향 범위 확인. 언어별 POSD는 bindings gate에서 수행
- [x] samples
- [x] scripts
- [x] docs와 codegen 도구

절차:

1. 위험 신호를 먼저 열거한다.
2. 각 위험 신호가 어떤 POSD 원칙을 위반하는지 기록한다.
3. 각 항목마다 두 가지 이상 수정 방향을 검토한다.
4. 호출자 인터페이스 복잡성이 줄어드는 방향을 선택한다.
5. 수정 뒤 테스트를 실행한다.
6. 리팩토링 로그에 결과를 기록한다.
7. 새 후보가 없을 때까지 반복한다.

종료 조건:

- [x] repo 전체 스캔에서 새 POSD 위험 신호가 없다.
- [x] 이미 기록된 위험 신호가 모두 해결되었거나 유지 사유가 있다.
- [x] 유지 사유는 public 계약, ABI, 성능, 안전성 중 하나로 설명된다.
- [x] 두 번 연속 전체 스캔에서 새 리팩토링 후보가 없다.
- [x] 전체 테스트가 통과한다.
- [x] 리팩토링 뒤 문서-코드 반복 리뷰를 다시 수행했고 mismatch가 없다.

## 정식 문서 반영과 3회 리뷰

구현 전 draft 계약을 정식 spec에 섞지 않는다. 구현과 테스트가 끝난 뒤에만 정식 문서로
나누어 반영한다.

반영 대상:

- [x] `doc/spec/core/service/spot.ko.md`
- [x] `doc/spec/core/socket/stream.ko.md`
- [x] `doc/spec/core/errno-map.ko.md`
- [x] 관련 `doc/guide`
- [x] 관련 `doc/internals`
- [x] `doc/spec/bindings`
- [x] sample policy와 사용 예

리뷰:

- [x] 1차 문서 리뷰 완료
- [x] 2차 문서 리뷰 완료
- [x] 3차 문서 리뷰 완료
- [x] 3차에서 mismatch가 나오면 수정 뒤 3차를 처음부터 다시 수행
- [x] guide, internals, core spec 문서가 모두 최종 코드와 맞다
- [x] stale API 이름과 제거 대상 API 설명이 정식 문서에 남아 있지 않다

## Core Release와 Bindings Native Library 최신화

Core 구현, 테스트, 문서 gate가 끝난 뒤 release를 진행한다. GitHub Actions release
흐름은 CLI로 확인한다.

Core release:

- [x] version bump 대상 확인
- [x] release commit 생성
- [x] `git status --short`로 의도한 변경만 확인
- [x] commit push
- [x] `core/vX.Y.Z` tag 생성
- [x] tag push
- [x] `gh run list --workflow "Build libzlink Core Libraries"`로 build workflow 시작 확인
- [x] `gh run watch` 또는 `gh run view`로 build workflow 성공 확인
- [x] `gh run list --workflow "Release Core Conan Package"`로 release workflow 시작 확인
- [x] `gh run watch` 또는 `gh run view`로 release workflow 성공 또는 optional skip 사유 확인
- [x] core native archive release asset 확인
- [x] `gh release view core/vX.Y.Z` 성공
- [x] 실패 시 원인 수정 후 새 patch version 또는 tag 재처리

Bindings native library:

- [x] core release 완료 전에는 `bindings/update_zlink_libs.sh`를 실행하지 않음
- [x] `gh` CLI 인증 확인
- [x] release asset 누락이 없음을 확인
- [x] `bindings/update_zlink_libs.sh` 실행
- [x] 변경된 native library와 version marker 확인
- [x] 결과를 `bindings-update-log.ko.md`에 기록

## Bindings 순차 적용, 5회 리뷰, 검증

bindings는 언어별로 하나씩 순차 진행한다. 한 언어의 spec, code, sample, perf, POSD gate를
닫기 전 다음 언어로 넘어가지 않는다.

진행 순서:

1. `bindings/c`
2. `bindings/cpp`
3. `bindings/dotnet`
4. `bindings/go`
5. `bindings/java`
6. `bindings/node`
7. `bindings/python`
8. `bindings/rust`

언어별 절차:

- [x] draft spec과 해당 언어 binding spec 1차 비교
- [x] 2차 비교
- [x] 3차 비교
- [x] 4차 비교
- [x] 5차 비교
- [x] 5차에서 mismatch가 나오면 수정 뒤 5차를 처음부터 다시 수행
- [x] 새 API, 변경 API, 제거 API가 binding spec에 반영됨
- [x] unchecked/checked ref 의미가 binding spec에 반영됨
- [x] ownership, timeout, join/leave, remote handoff 계약이 binding spec에 반영됨
- [x] binding code가 spec과 일치함
- [x] 제거 API가 binding public surface에서 사라짐
- [x] sample build 성공
- [x] sample runner 성공
- [x] perf smoke 성공
- [x] binding POSD 리팩토링 완료
- [x] 두 번 연속 새 POSD 후보가 없음

## 최종 종료 절차

- [x] `git status --short` 확인
- [x] contract matrix 모든 행 `reviewed`
- [x] implementation review log 닫힘
- [x] sample/perf smoke log 닫힘
- [x] POSD refactor log 닫힘
- [x] final doc review log 닫힘
- [x] core release log 닫힘
- [x] bindings update log 닫힘
- [x] bindings spec review log 닫힘
- [x] bindings POSD refactor log 닫힘
- [x] blocker log에 미해결 blocker 없음
- [x] 최종 테스트 명령과 결과 기록
- [x] 최종 보고 작성
