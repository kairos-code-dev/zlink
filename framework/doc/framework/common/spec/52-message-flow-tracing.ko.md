# Message flow tracing

[공통 스펙 목차](README.ko.md) · [Runtime monitoring](50-runtime-monitoring.ko.md) ·
[Runtime metrics](51-runtime-metrics.ko.md) · [Flow correlation](53-flow-correlation.ko.md)

## 1. 이 문서가 정의하는 범위

이 문서는 ZLink Framework 11.0.0에서 개별 message의 admission, dispatch, reply, backpressure와 drop을
구조화 event로 관찰하는 공통 공개 계약을 정의한다. Node, Channel, Spot, Logical
Multicast, Actor와 STREAM 경계를 지나는 message가 어느 단계에서 수락되거나
실패했는지를 같은 field 집합으로 추적할 수 있어야 한다.

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
| `received` | Message가 Framework의 dispatch 경계에 도착했다. |
| `admitted` | Target application queue 또는 선택된 remote target 집합이 message를 수락했다. |
| `dispatched` | Framework가 typed application handler 실행을 시작했다. |
| `completed` | One-way handler가 더 이상 실행 결과를 만들지 않는 terminal 상태로 끝났다. |
| `replied` | Request handler가 response 또는 error reply를 만들었다. |
| `sent` | Outbound submit을 local transport가 수락했다. |
| `reply_received` | Outbound request가 terminal reply를 받았다. |
| `backpressured` | 수락할 공간이 부족하여 message를 받지 못했거나 제한 시간까지 기다렸다. |
| `dropped` | 정책에 따라 message를 전달 대상에서 제외했다. |

`sent`는 source의 local transport가 message를 받았다는 뜻이고, `admitted`는 기록
대상 queue나 target 집합이 message를 받았다는 뜻이다. 두 phase 모두 remote
handler가 실행을 끝냈다는 보장은 아니다.
어떤 phase도 기존 message delivery guarantee를 확대하지 않는다.

## 3. Surface와 message kind

| 필드 | 닫힌 값 |
|---|---|
| `surface` | Message가 통과한 표면을 `node`, `channel`, `spot`, `instance_spot`, `actor`, `stream`, `actor_relocation` 가운데 하나로 기록한다. |
| `message_kind` | Message 종류를 `send`, `request`, `response`, `error`, `control` 가운데 하나로 기록한다. |
| `flow_origin` | Flow를 처음 만든 원인을 `inbound`, `timer`, `application`, `lifecycle` 가운데 하나로 기록한다. |

Logical Multicast와 classic fanout publish는 message-flow event를 만들지 않는다.

## 4. Event fields

§2와 §3의 message-flow event는 다음 공통 field name을 사용한다. 언어별 property casing은 달라도 structured output key는
표에 적힌 문자열로 고정한다.

| Field | 필수 여부 | 의미 |
|---|---|---|
| `event_id` | 모든 message-flow event에 반드시 기록한다. | §2에서 정한 identifier로 event 종류를 나타낸다. |
| `timestamp` | 모든 message-flow event에 반드시 기록한다. | Framework가 event를 관찰한 시각을 나타낸다. |
| `phase` | Flow event에 반드시 기록한다. | §2에서 정한 phase로 message 처리 단계를 나타낸다. |
| `surface` | 모든 message-flow event에 반드시 기록한다. | §3에서 정한 surface로 message가 통과한 표면을 나타낸다. |
| `message_kind` | 모든 message-flow event에 반드시 기록한다. | §3에서 정한 kind로 message 종류를 나타낸다. |
| `outcome` | 모든 message-flow event에 반드시 기록한다. | 결과를 `succeeded`, `failed`, `backpressured`, `dropped`, `cancelled`, `shutdown` 가운데 하나로 나타낸다. |
| `reason` | Failure, backpressure 또는 drop에 reason이 있을 때 기록한다. | Failure, backpressure 또는 drop이 발생한 이유를 나타낸다. |
| `action` | Dispatch error에는 반드시 기록한다. | 실패를 reply, caller completion 또는 drop 가운데 어떤 방법으로 마무리했는지 나타낸다. |
| `channel_name` | Channel 논리 주소가 있는 event에 기록한다. | Message가 사용하는 `ChannelName` 논리 주소를 나타낸다. |
| `channel_route_kind` | Channel surface에는 반드시 기록한다. | Channel 경로가 `route_mesh`인지 `client_server`인지 나타낸다. |
| `mesh_name` | RouteMesh scope가 있는 event에 기록한다. | Node direct 또는 선택된 [RouteMesh](01-glossary.ko.md#routemesh)의 물리 scope를 나타낸다. |
| `server_rid` | ClientServer target이 있는 event에 기록한다. | 선택된 ClientServer server identity를 나타낸다. |
| `source_rid`, `target_rid` | Routed hop에 source 또는 target이 있을 때 기록한다. | Routed hop의 source와 target을 나타낸다. |
| `packet_name` | Typed dispatch key가 있는 event에 기록한다. | Handler를 선택하는 typed packet 이름을 나타낸다. |
| `topic`, `spot_id`, `actor_id` | 해당 surface가 이 논리 target을 사용할 때 기록한다. | Topic, Spot ID 또는 Actor ID로 논리 target을 나타낸다. |
| `instance_spot_type`, `activation_state` | Instance Spot event에 해당 값이 있을 때 기록한다. | Startup에 등록한 type과 `activating`, `ready`, `closing` 가운데 현재 state를 나타낸다. |
| `correlation_id` | Request와 terminal reply를 연결해야 할 때 기록한다. | Request와 terminal reply가 같은 operation임을 나타낸다. |
| `flow_id`, `flow_origin` | Causal flow를 기록할 때 두 field를 함께 기록한다. | Causal flow와 이 flow를 처음 만든 원인을 나타낸다. |
| `message_size_bytes` | `verbose` mode에서만 기록한다. | Payload를 포함한 관찰 대상 message 크기를 byte 단위로 나타낸다. |
| `duration_seconds` | Terminal event에서 경과 시간을 제공할 때 기록한다. | Operation 또는 handler가 끝날 때까지 걸린 시간을 초 단위로 나타낸다. |

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
| `event_id` | Runtime error event임을 나타내도록 `zlink.runtime_error`를 기록한다. |
| `timestamp` | Framework가 observer 실패를 관찰한 시각을 기록한다. |
| `kind` | Observer가 실패했음을 나타내도록 `observer_failed`를 기록한다. |
| `source` | 실패한 source가 message flow observer임을 나타내도록 `message_flow_observer`를 기록한다. |
| `reason` | Exception type과 bounded message를 결합한 문자열을 기록하며 payload·metadata·stack trace는 포함하지 않는다. |

Runtime error event에 exception object, native handle, callback 참조를 넣지 않는다. 이 event는
observer 실패를 관찰하는 계약이며 application handler 실패나 dispatch error를 대체하지 않는다.

## 5. Log mode

Message flow log mode는 아래 순서의 닫힌 값이다.

| Mode | 구조화 로그 출력 |
|---|---|
| `off` | Message flow와 dispatch error 로그를 출력하지 않는다. |
| `errors_only` | `zlink.dispatch_error`, `backpressured`, `dropped` event만 출력한다. |
| `key_transitions` | Error와 §2에서 정한 모든 phase를 출력한다. |
| `verbose` | Key transition과 `message_size_bytes`, `duration_seconds`를 출력한다. |

기본값은 `errors_only`다. log mode는 metric 기록과 명시적으로 등록한 observer event를 끄지 않는다.
runtime에서 mode를 thread-safe하게 바꿀 수 있으며 host restart를 요구하지 않는다.

Framework 기본 structured logger가 있으면 해당 logger로 출력한다. logger가 없으면 bounded fallback sink를
사용할 수 있다. 어느 경우에도 stdout 형식 parsing을 유일한 public 관측 표면으로 요구하지 않는다.

fallback text를 제공할 때 prefix는 `zlink flow:`이고 key는 다음 문자열을 사용한다.

`event`, `phase`, `surface`, `kind`, `mesh`, `channel`, `channel_route`, `source_rid`, `target_rid`, `server_rid`, `packet`, `topic`,
`spot`, `instance_type`, `activation_state`, `actor`, `corr`, `flow`, `origin`,
`outcome`, `reason`, `size`.

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

다음 C# 발췌는 observer 등록과 callback을 이해하기 위한 .NET 표현이다. 다른 언어에
같은 signature를 요구하지 않으며, 정확한 전체 계약은
[.NET topology monitoring interface](server/languages/dotnet/interfaces/10-topology-monitoring.ko.md)가
정의한다.

```csharp
public interface IZLinkRuntimeMessageFlowObserver
{
    ValueTask OnMessageFlowAsync(
        ZLinkRuntimeMessageFlowEvent flow,
        CancellationToken cancellationToken);
}

public interface IZLinkDispatchOptions
{
    IZLinkDispatchOptions SetRuntimeMessageFlowObserver<TObserver>()
        where TObserver : class, IZLinkRuntimeMessageFlowObserver;

    IZLinkDispatchOptions MessageFlow(
        ZLinkRuntimeMessageFlowMode mode);
}
```

```csharp
options.ConfigureDispatch()
    .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
    // Message phase를 구조화 event로 만들 범위를 선택한다.
    .SetRuntimeMessageFlowObserver<FlowObserver>();
    // Observer는 event를 읽지만 message 처리 결과를 변경하지 않는다.
```

## 7. Sampling

정상 flow sampling은 `flow_id` hash로 일관되게 결정한다. 같은 flow는 모든 hop과 Logical Multicast branch가
함께 남거나 함께 빠져야 한다. `zlink.dispatch_error`, `backpressured`와 `dropped` event는 sampling을
우회한다.

`flow_id`가 없는 독립 event는 source [MeshNode](01-glossary.ko.md#meshnode) generation과 local sequence로 안정적인 sampling 결정을
내린다. sampling rate가 0보다 작거나 1보다 크면 startup 오류다.

## 8. Hook coverage

다음 public 의미의 경계에서 event를 기록한다.

- [Node direct](01-glossary.ko.md#node-direct)와 RouteMesh·ClientServer ChannelName select-one submit·receive·dispatch·reply
- Spot direct application queue admission과 handler completion
- [Instance Spot](01-glossary.ko.md#entry-user-instance-spot) source resolve·activation-envelope submit, target-owned claim·activation barrier, application
  admission과 post-submit one-way drop
- Actor queue admission, handler completion과 relocation terminal result
- STREAM session receive, Actor dispatch, reply와 bound-session send
- request timeout, cancellation, shutdown과 dispatch error

같은 operation을 wrapper와 하위 transport에서 중복 terminal event로 기록하지 않는다. 각 request에는
surface별 terminal event가 하나만 있어야 한다.

## 9. 구현 및 contract test 검증 요구

- event identifier, phase, surface, message kind, outcome, dispatch reason·action과 field key가 모든
  언어에서 같다.
- Actor payload trace가 Spot dispatch phase로 기록되지 않는다.
- observer·logger failure가 message dispatch와 reply를 바꾸지 않는다.
- observer failure는 `observer_failed`/`message_flow_observer` runtime error event 하나로 보고되고
  sink 실패는 재귀 event를 만들지 않는다.
- payload와 application metadata value가 event나 fallback log에 나타나지 않는다.
- Logical Multicast와 classic fanout publish가 message-flow event, publish 전용
  metric 또는 runtime event를 만들지 않는다.
- 각 request surface가 terminal event를 정확히 한 번 기록한다.
- Instance one-way activation 실패가 `surface=instance_spot`, `phase=dropped`로 한 번 기록되고 숨은 request나
  replay event를 만들지 않는다.
- 같은 [ChannelName](01-glossary.ko.md#channelname)이라도 RouteMesh와 ClientServer 물리 경로를 `channel_route_kind`로 구분하며 application
  handler에는 이 물리 경로를 dispatch key로 요구하지 않는다.
