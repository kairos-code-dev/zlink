# Message Flow Tracing — 공통 스펙

[스펙 목차](../README.ko.md) · [Runtime monitoring](50-runtime-monitoring.ko.md) ·
[Runtime metrics](51-runtime-metrics.ko.md) · [Flow correlation](53-flow-correlation.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 개별 message의 admission, dispatch, reply, backpressure와 drop을
구조화 event로 관찰하는 공통 공개 계약을 정의한다. 이 문서는 “Node·Channel·Spot·Logical
Multicast·Actor·STREAM 경계를 지나는 한 message가 어디에서 수락되거나 실패했는지 어떤 필드로
추적하는가?”라는 질문에 답한다.

runtime 상태 변화는 [50 Runtime monitoring](50-runtime-monitoring.ko.md), 집계 계기는
[51 Runtime metrics](51-runtime-metrics.ko.md), correlation ID와 causal flow의 생성·전파는
[53 Flow correlation](53-flow-correlation.ko.md)이 소유한다. trace event는 payload와 transport frame을
공개하지 않는다.

## 2. Event identifiers와 phase

공통 event identifier는 아래 세 문자열로 고정한다.

| Identifier | 의미 |
|---|---|
| `zlink.message_flow` | message의 정상·backpressure·drop phase |
| `zlink.dispatch_error` | decode, handler, reply route와 protocol dispatch failure |
| `zlink.runtime_error` | observer callback 실패를 messaging 경로와 분리해 보고하는 runtime error |

`zlink.message_flow`의 phase는 아래 닫힌 값이다.

| Phase | 의미 |
|---|---|
| `received` | Framework dispatch 경계에 message가 도착함 |
| `admitted` | target application queue 또는 remote target set이 message를 수락함 |
| `dispatched` | typed application handler 실행을 시작함 |
| `completed` | one-way handler가 terminal result로 끝남 |
| `replied` | request handler의 response 또는 error reply가 생성됨 |
| `sent` | outbound submit이 local transport admission을 통과함 |
| `reply_received` | outbound request의 terminal reply를 받음 |
| `backpressured` | admission이 message를 수락하지 못하거나 제한 시간까지 기다림 |
| `dropped` | 정책상 message 또는 Logical Multicast target을 drop함 |

phase는 delivery guarantee를 확대하지 않는다. `sent`와 `admitted`는 각 호출 계약의 admission을 뜻하며
remote handler completion을 뜻하지 않는다.

## 3. Surface와 message kind

| 필드 | 닫힌 값 |
|---|---|
| `surface` | `node`, `channel`, `spot`, `instance_spot`, `logical_multicast`, `actor`, `stream`, `classic_fanout`, `actor_relocation` |
| `message_kind` | `send`, `request`, `response`, `error`, `publish`, `control` |
| `flow_origin` | `inbound`, `timer`, `application`, `lifecycle` |

Logical Multicast operation은 origin event 하나와 remote MeshNode target event를 기록할 수 있다. 같은 node의
local Spot delivery를 payload 수만큼 encode한 별도 outbound event로 표현하지 않는다. local match 수와
remote target 수를 count 필드로 기록한다.

## 4. Event fields

모든 event는 다음 공통 field name을 사용한다. 언어별 property casing은 달라도 structured output key는
표에 적힌 문자열로 고정한다.

| Field | 필수 여부 | 의미 |
|---|---|---|
| `event_id` | 필수 | §2의 identifier |
| `timestamp` | 필수 | event 관찰 시각 |
| `phase` | flow event 필수 | §2의 phase |
| `surface` | 필수 | §3의 surface |
| `message_kind` | 필수 | §3의 kind |
| `outcome` | 필수 | `succeeded`, `failed`, `backpressured`, `dropped`, `cancelled`, `shutdown` |
| `reason` | 조건부 | failure·backpressure·drop reason |
| `action` | dispatch error에서 필수 | 실패를 reply, caller completion 또는 drop으로 마무리한 방법 |
| `channel_name` | 조건부 | ChannelName 논리 주소 |
| `channel_route_kind` | Channel surface에서 필수 | `route_mesh` 또는 `client_server` |
| `mesh_name` | RouteMesh에서 조건부 | Node direct 또는 선택된 RouteMesh의 물리 scope |
| `server_rid` | ClientServer에서 조건부 | 선택된 ClientServer server identity |
| `source_rid`, `target_rid` | 조건부 | routed hop의 source와 target |
| `packet_name` | 조건부 | typed dispatch key |
| `topic`, `spot_rid`, `actor_id` | 조건부 | 해당 surface의 논리 target |
| `instance_spot_type`, `activation_state` | Instance Spot에서 조건부 | startup 등록 type과 `activating`, `ready`, `closing` state |
| `correlation_id` | 조건부 | request와 terminal reply의 operation key |
| `flow_id`, `flow_origin` | 조건부 pair | causal flow와 최초 origin |
| `remote_snapshot_count`, `remote_admitted_count`, `remote_dropped_count` | 조건부 | Logical Multicast remote target의 snapshot·admission·drop count |
| `local_snapshot_count`, `local_admitted_count`, `local_dropped_count` | 조건부 | Logical Multicast local Spot의 snapshot·admission·drop count |
| `target_count`, `drop_count` | 조건부 | classic fanout 등 다른 fan-out 표면의 집계 count |
| `message_size_bytes` | verbose에서만 | payload를 포함한 관찰 대상 message 크기 |
| `duration_seconds` | terminal event에서 선택 | operation 또는 handler 경과 시간 |

`channel_route_kind`, `mesh_name`과 `server_rid`는 관측 필드이며 Channel handler context의 dispatch key나
대상 선택 인자로 사용하지 않는다. `flow_id`와 `flow_origin`은 함께 존재하거나 함께 없다. Payload body,
application metadata value, native
socket handle, raw frame와 exception object는 trace event에 포함하지 않는다. error diagnostic은 bounded
문자열이며 secret과 payload를 복사하지 않는다.

`zlink.message_flow`에서 `reason`이 존재하면 `backpressure`, `stale_target`, `target_closed`, `shutdown`,
`location_unavailable`, `activation_rejected`, `activation_timeout` 가운데 하나다. 마지막 세 값은 각각
Instance location을 확인하거나 claim할 수 없음, Instance factory·Ready 처리 거부, activation deadline
도달을 뜻한다. Instance close와 lease fencing은 `target_closed`를 사용한다.

`zlink.dispatch_error`의 `outcome`은 `failed`로 고정한다. `reason`은 아래 닫힌 값이다.

`no_handler`, `decode_error`, `handler_exception`, `invalid_frame`, `reply_path_missing`,
`unexpected_reply`, `backpressure`, `stale_target`, `shutdown`.

`zlink.dispatch_error`의 `action`은 `reply_error`, `fail_caller`, `drop`의 닫힌 값이다.
reply path가 있는 request 실패는 `reply_error`, reply frame을 만들지 않는 local call의
terminal 실패는 `fail_caller`, one-way operation은 `drop`을 사용한다.

### 4.1 Runtime error event

`zlink.runtime_error`는 message-flow observer 실패를 다시 같은 observer에 전달하지 않고
별도 runtime error sink에 전달한다. 필드와 닫힌 값은 아래와 같다.

| Field | 값·의미 |
|---|---|
| `event_id` | `zlink.runtime_error` |
| `timestamp` | 실패를 관찰한 시각 |
| `kind` | `observer_failed` |
| `source` | `message_flow_observer` |
| `reason` | exception type과 bounded message를 결합한 문자열. payload·metadata·stack trace를 포함하지 않음 |

Runtime error event에 exception object, native handle, callback 참조를 넣지 않는다. 이 event는
observer 실패를 관찰하는 계약이며 application handler 실패나 dispatch error를 대체하지 않는다.

## 5. Log mode

Message flow log mode는 아래 순서의 닫힌 값이다.

| Mode | 구조화 로그 출력 |
|---|---|
| `off` | message flow와 dispatch error 로그를 출력하지 않음 |
| `errors_only` | `zlink.dispatch_error`, `backpressured`, `dropped`만 출력 |
| `key_transitions` | errors와 모든 §2 phase 출력 |
| `verbose` | key transitions와 `message_size_bytes`, `duration_seconds` 출력 |

기본값은 `errors_only`다. log mode는 metric 기록과 명시적으로 등록한 observer event를 끄지 않는다.
runtime에서 mode를 thread-safe하게 바꿀 수 있으며 host restart를 요구하지 않는다.

Framework 기본 structured logger가 있으면 해당 logger로 출력한다. logger가 없으면 bounded fallback sink를
사용할 수 있다. 어느 경우에도 stdout 형식 parsing을 유일한 public 관측 표면으로 요구하지 않는다.

fallback text를 제공할 때 prefix는 `zlink flow:`이고 key는 다음 문자열을 사용한다.

`event`, `phase`, `surface`, `kind`, `mesh`, `channel`, `channel_route`, `source_rid`, `target_rid`, `server_rid`, `packet`, `topic`,
`spot`, `instance_type`, `activation_state`, `actor`, `corr`, `flow`, `origin`, `outcome`, `reason`, `remote_snapshot`, `remote_admitted`,
`remote_dropped`, `local_snapshot`, `local_admitted`, `local_dropped`, `targets`, `drops`, `size`.

## 6. Observer

Message flow observer는 immutable event snapshot을 받는 관측 callback이다. observer는 routing, handler
selection, reply와 drop 결정을 바꿀 수 없다.

- receive와 application mailbox worker에서 observer user code를 직접 실행하지 않는다.
- bounded observer queue가 가득 차면 새 trace event를 drop하고
  `zlink.observability.events.overflow`를 증가시킨다.
- observer exception과 rejected completion은 runtime error sink에 기록하고 다음 event 처리를 계속한다.
- terminal host termination event는 message flow observer가 아니라 runtime monitoring event가 소유한다.
- observer가 없으면 trace event object를 만들기 위한 payload-independent allocation을 피한다.

Runtime error sink는 immutable `zlink.runtime_error` snapshot을 받는 별도 public callback이다. Framework는
message-flow observer를 실행하는 queue와 runtime error sink를 실행하는 경로를 분리한다.
sink callback 실패는 bounded fallback logger에만 기록하고 다시 runtime error event를 만들지 않는다.
언어별 exact interface는 message-flow observer와 runtime error sink를 모두 startup dispatch 설정에서
한 번 등록할 수 있어야 한다.

## 7. Sampling

정상 flow sampling은 `flow_id` hash로 일관되게 결정한다. 같은 flow는 모든 hop과 Logical Multicast branch가
함께 남거나 함께 빠져야 한다. `zlink.dispatch_error`, `backpressured`와 `dropped` event는 sampling을
우회한다.

`flow_id`가 없는 독립 event는 source MeshNode generation과 local sequence로 안정적인 sampling 결정을
내린다. sampling rate가 0보다 작거나 1보다 크면 startup 오류다.

## 8. Hook coverage

다음 public 의미의 경계에서 event를 기록한다.

- Node direct와 RouteMesh·ClientServer ChannelName select-one submit·receive·dispatch·reply
- Spot direct application queue admission과 handler completion
- Instance Spot resolve·claim·activation barrier, application admission과 post-submit one-way drop
- Logical Multicast origin admission, remote target submit, local match와 target drop
- Actor queue admission, handler completion과 relocation terminal result
- STREAM session receive, Actor dispatch, reply와 bound-session send
- classic fanout publish·receive와 Framework가 원인을 확인한 drop
- request timeout, cancellation, shutdown과 dispatch error

같은 operation을 wrapper와 하위 transport에서 중복 terminal event로 기록하지 않는다. 각 request에는
surface별 terminal event가 하나만 있어야 한다.

## 9. 검증 요구

- event identifier, phase, surface, message kind, outcome, dispatch reason·action과 field key가 모든
  언어에서 같다.
- publish operation의 backpressure와 target별 loss는 서로 다른 event로 구분되며 같은 operation에 함께 나타날 수 있다.
- Actor payload trace가 Spot dispatch phase로 기록되지 않는다.
- observer·logger failure가 message dispatch와 reply를 바꾸지 않는다.
- observer failure는 `observer_failed`/`message_flow_observer` runtime error event 하나로 보고되고
  sink 실패는 재귀 event를 만들지 않는다.
- flow sampling이 Logical Multicast branch 전체에 일관되게 적용된다.
- payload와 application metadata value가 event나 fallback log에 나타나지 않는다.
- 각 request surface가 terminal event를 정확히 한 번 기록한다.
- Instance one-way activation 실패가 `surface=instance_spot`, `phase=dropped`로 한 번 기록되고 숨은 request나
  replay event를 만들지 않는다.
- 같은 ChannelName이라도 RouteMesh와 ClientServer 물리 경로를 `channel_route_kind`로 구분하며 application
  handler에는 이 물리 경로를 dispatch key로 요구하지 않는다.
