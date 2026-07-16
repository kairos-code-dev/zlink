# Message Flow Tracing — 공통 스펙

[스펙 목차](../README.ko.md) · [Runtime monitoring](50-runtime-monitoring.ko.md) ·
[Runtime metrics](51-runtime-metrics.ko.md) · [Flow correlation](53-flow-correlation.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0에서 개별 message의 admission, dispatch, reply, backpressure와 drop을
구조화 event로 관찰하는 공통 공개 계약을 정의한다. 이 문서는 “Node·Channel·Spot·Logical
Multicast·Actor·STREAM 경계를 지나는 한 message가 어디에서 수락되거나 실패했는지 어떤 필드로
추적하는가?”라는 질문에 답한다.

runtime 상태 변화는 [50 Runtime monitoring](50-runtime-monitoring.ko.md), 집계 계기는
[51 Runtime metrics](51-runtime-metrics.ko.md), correlation ID와 causal flow의 생성·전파는
[53 Flow correlation](53-flow-correlation.ko.md)이 소유한다. trace event는 payload와 transport frame을
공개하지 않는다.

## 2. Event identifiers와 phase

공통 event identifier는 아래 두 문자열로 고정한다.

| Identifier | 의미 |
|---|---|
| `zlink.message_flow` | message의 정상·backpressure·drop phase |
| `zlink.dispatch_error` | decode, handler, reply route와 protocol dispatch failure |

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
| `surface` | `node`, `channel`, `spot`, `logical_multicast`, `actor`, `stream`, `classic_fanout`, `actor_transfer` |
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
| `mesh_name`, `channel_name` | 조건부 | MeshNode와 ChannelName scope |
| `source_rid`, `target_rid` | 조건부 | routed hop의 source와 target |
| `packet_name` | 조건부 | typed dispatch key |
| `topic`, `spot_rid`, `actor_id` | 조건부 | 해당 surface의 논리 target |
| `correlation_id` | 조건부 | request와 terminal reply의 operation key |
| `flow_id`, `flow_origin` | 조건부 pair | causal flow와 최초 origin |
| `target_count`, `local_match_count`, `drop_count` | 조건부 | multicast·fanout의 집계 count |
| `message_size_bytes` | verbose에서만 | payload를 포함한 관찰 대상 message 크기 |
| `duration_seconds` | terminal event에서 선택 | operation 또는 handler 경과 시간 |

`flow_id`와 `flow_origin`은 함께 존재하거나 함께 없다. payload body, application metadata value, native
socket handle, raw frame와 exception object는 trace event에 포함하지 않는다. error diagnostic은 bounded
문자열이며 secret과 payload를 복사하지 않는다.

`zlink.dispatch_error`의 `reason`은 아래 닫힌 값이다.

`no_handler`, `decode_error`, `handler_exception`, `invalid_frame`, `reply_path_missing`,
`unexpected_reply`, `backpressure`, `stale_target`, `shutdown`.

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

`event`, `phase`, `surface`, `kind`, `mesh`, `channel`, `source_rid`, `target_rid`, `packet`, `topic`,
`spot`, `actor`, `corr`, `flow`, `origin`, `outcome`, `reason`, `targets`, `local_matches`, `drops`, `size`.

## 6. Observer

Message flow observer는 immutable event snapshot을 받는 관측 callback이다. observer는 routing, handler
selection, reply와 drop 결정을 바꿀 수 없다.

- receive와 application claim thread에서 observer user code를 직접 실행하지 않는다.
- bounded observer queue가 가득 차면 새 trace event를 drop하고
  `zlink.observability.events.overflow`를 증가시킨다.
- observer exception과 rejected completion은 runtime error sink에 기록하고 다음 event 처리를 계속한다.
- terminal drain event는 message flow observer가 아니라 runtime monitoring event가 소유한다.
- observer가 없으면 trace event object를 만들기 위한 payload-independent allocation을 피한다.

## 7. Sampling

정상 flow sampling은 `flow_id` hash로 일관되게 결정한다. 같은 flow는 모든 hop과 Logical Multicast branch가
함께 남거나 함께 빠져야 한다. `zlink.dispatch_error`, `backpressured`와 `dropped` event는 sampling을
우회한다.

`flow_id`가 없는 독립 event는 source MeshNode generation과 local sequence로 안정적인 sampling 결정을
내린다. sampling rate가 0보다 작거나 1보다 크면 startup 오류다.

## 8. Hook coverage

다음 public 의미의 경계에서 event를 기록한다.

- Node direct와 ChannelName select-one submit·receive·dispatch·reply
- Spot direct application queue admission과 handler completion
- Logical Multicast origin admission, remote target submit, local match와 target drop
- Actor queue admission, handler completion과 transfer terminal result
- STREAM session receive, Actor dispatch, reply와 bound-session send
- classic fanout publish·receive와 Framework가 원인을 확인한 drop
- request timeout, cancellation, shutdown과 dispatch error

같은 operation을 wrapper와 하위 transport에서 중복 terminal event로 기록하지 않는다. 각 request에는
surface별 terminal event가 하나만 있어야 한다.

## 9. 검증 요구

- event identifier, phase, surface, message kind와 field key가 모든 언어에서 같다.
- `NoDrop = true` publish는 backpressured event, `NoDrop = false` target loss는 dropped event로 구분된다.
- Actor payload trace가 Spot dispatch phase로 기록되지 않는다.
- observer·logger failure가 message dispatch와 reply를 바꾸지 않는다.
- flow sampling이 Logical Multicast branch 전체에 일관되게 적용된다.
- payload와 application metadata value가 event나 fallback log에 나타나지 않는다.
- 각 request surface가 terminal event를 정확히 한 번 기록한다.
