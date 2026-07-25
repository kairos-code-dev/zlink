# Flow correlation

[공통 스펙 목차](README.ko.md) · [Message model](03-message-model.ko.md) ·
[Message flow tracing](52-message-flow-tracing.ko.md) · [Session Actor Dispatch](31-session-actor-dispatch.ko.md)

## 1. 이 문서가 정의하는 범위

이 문서는 ZLink Framework 11.0.0에서 request correlation과 여러 hop에 걸친 causal flow를 식별하는 공통
공개 계약을 정의한다. Request와 reply 한 쌍을 식별하는 값과 Logical Multicast,
Actor, Spot과 STREAM으로 이어지는 전체 causal flow를 식별하는 값을 구분한다.

Application metadata의 ownership과 크기는 [메시지 모델](03-message-model.ko.md), trace event field와
sampling은 [52 Message flow tracing](52-message-flow-tracing.ko.md)이 소유한다. correlation field는
Framework가 소유하는 reserved context이며 application metadata key로 표현하지 않는다.

## 2. 두 식별자

| 식별자 | 범위 | 생성 | terminal 의미 |
|---|---|---|---|
| `correlation_id` | request와 그 response·error 한 쌍 | request origin | request terminal completion 뒤 종료 |
| `flow_id` | 한 causal root에서 파생된 여러 message와 fan-out branch | causal root | branch가 끝날 때까지 전파 |

Framework는 reply가 어느 request에 속하는지 판단할 때 `correlation_id`만
사용한다. `flow_id`는 서로 이어진 message를 관측할 때 사용하는 값이다. Reply
matching, 중복 제거, idempotency 또는 현재 object owner 검증에는 사용하지 않는다.

같은 flow에서 여러 request를 만들면 request마다 다른 `correlation_id`를 가지면서 같은 `flow_id`를 가질
수 있다. one-way message는 correlation ID 없이 flow ID만 가질 수 있다.

## 3. 형식

`flow_id`는 lowercase hyphenated UUIDv7 문자열이며 정확히 36 ASCII byte다. `flow_origin`은
`inbound|timer|application|lifecycle`의 닫힌 값이고 root에서 한 번 정한 뒤 hop에서 바꾸지 않는다.

`correlation_id`는 Framework가 생성하는 opaque ASCII identifier다. 길이는 1~64 byte이며 request를 만든
MeshNode, ClientServer client 또는 STREAM runtime의 lifecycle 안에서 동시에 pending인 request 사이에
unique해야 한다.
application은 형식을 해석하거나 새 값을 조립하지 않는다.

허용하지 않는 `flow_id`, 빈 correlation ID, `flow_id`와 `flow_origin` 중 하나만 존재하는 message는
protocol error다.

Framework message envelope에서 이 오류를 발견하면 `RequestProtocolError`로 완료한다. STREAM
frame에서 발견하면 connector 종료 사유 `ProtocolError`로 연결을 종료한다.

## 4. Flow 생성

Framework-managed inbound에 valid flow ID가 있으면 새 값을 만들지 않고 그대로 사용한다. flow ID가 없는
다음 causal root는 flow tracking이 활성일 때 새 값을 만든다.

- STREAM, Node direct, ChannelName, Spot direct, Instance Spot direct와 Actor inbound
- timer callback과 lifecycle callback
- Framework callback 밖의 application code가 시작한 첫 outbound operation

host의 trace log mode가 `off`이고 observer도 없으면 새 flow ID를 만들지 않는다. 다만 inbound에 이미
flow ID가 있으면 mode와 관계없이 보존하고 다음 관련 hop에 전파한다. client connector가 시작하는 outbound
request는 client와 server trace를 연결할 수 있도록 flow ID를 생성한다.

각 callback은 시작할 때 current flow context를 설정하고 terminal completion에서 이전 context를
복원한다. 관련 없는 다음 callback으로 flow가 누출되지 않아야 한다.

## 5. Propagation

Framework는 인과 관계가 있는 다음 operation에 flow ID와 root origin을 함께 전달한다.

| 경계 | 규칙 |
|---|---|
| [Node direct](01-glossary.ko.md#node-direct)·[ChannelName](01-glossary.ko.md#channelname) | 선택된 RouteMesh 또는 ClientServer target의 handler context까지 보존 |
| [Spot direct](01-glossary.ko.md#spot-direct) | target [Spot](01-glossary.ko.md#spot) application turn까지 보존 |
| [Instance Spot](01-glossary.ko.md#entry-spot-user-spot과-instance-spot) direct | source resolve·activation envelope, target-owned claim·activation barrier를 지나 first Spot application turn까지 보존 |
| [Logical Multicast](01-glossary.ko.md#logical-multicast) | 모든 remote [MeshNode](01-glossary.ko.md#meshnode)와 local matching Spot이 같은 flow ID 사용 |
| Actor direct·STREAM Actor dispatch | target Actor queue와 request reply까지 보존 |
| Actor relocation | relocation control과 target Actor의 관련 lifecycle 작업에 보존 |
| bound-session push | 현재 Actor operation에서 파생된 push에 보존 |
| classic fanout | 각 subscriber branch가 같은 flow ID 사용 |

Logical Multicast와 [classic fanout](01-glossary.ko.md#classic-fanout)은 tree를 만든다. 각 branch에 새 flow ID를 발급하지 않으며 branch를
구분하려면 trace event의 target RID, Spot ID 또는 local sequence를 사용한다.

request를 다른 Framework surface로 그대로 relay하면 correlation ID도 reply까지 보존한다. handler가
새로운 downstream request를 시작하면 새 correlation ID를 만들고 current flow ID만 이어받는다.

Instance Spot의 첫 message가 도착한 target이 생성 권한을 얻지 못하면, 생성 권한을
얻어 Ready가 된 [owner](01-glossary.ko.md#owner)로 같은 message를 한 번 전달할 수 있다. 이때 새 flow ID나
correlation ID를 만들지 않는다. Activation이 끝날 때까지 기다린 최초 message도
원래 ID를 유지한다.

Target queue가 message를 수락한 뒤 실패하면 다른 owner를 선택하거나 새 correlation
ID를 만들어 같은 message를 자동으로 다시 보내지 않는다.

### 5.1 Handler의 다른 송신 경로 호출

Spot, Actor, [RouteMesh](01-glossary.ko.md#routemesh) Channel 또는 ClientServer handler가 process의 다른 ChannelName 송신 경로로 request를
시작하면 [downstream request](01-glossary.ko.md#downstream-request)에는 새 correlation ID를 발급한다. Downstream terminal result는 원래 handler가
속한 activation의 completion으로 전달하며, 원래 inbound request를 handler dispatch에 다시 넣지 않는다.

원래 activation이 Spot 또는 Actor에 속하면 completion은 같은 Spot 또는 Actor generation에만 전달한다.
Generation이 바뀌었거나 owner가 종료된 completion은 stale 결과로 끝내고 새 instance의 queue에 넣지 않는다.
Downstream timeout, cancellation 또는 늦은 reply는 원래 request를 정확히 한 번만 완료하며 자동 retry나 다른
ChannelName route 재선택을 일으키지 않는다.

## 6. Async context

Framework가 handler를 호출하고 await하는 continuation은 current flow context를 보존한다. application이
detached task, 별도 executor 또는 Framework가 관리하지 않는 callback을 만들면 암묵적인 전파를
보장하지 않는다. 그 작업에서 시작한 outbound는 명시적인 context 전달이 없으면 새 application flow다.

언어에 안전한 async-local context가 없으면 inbound message에서 flow context를 명시적으로 capture하여
관련 outbound call에 전달하는 표면을 제공한다. process-global 변수, thread ID 또는 connector instance의
mutable current-flow 필드로 추정하지 않는다.

## 7. Reply와 failure

- response와 error는 request의 correlation ID와 flow ID를 보존한다.
- request timeout·cancellation 뒤 도착한 reply를 다른 pending request에 연결하지 않는다.
- stale binding token의 STREAM reply와 push를 새 session flow에 연결하지 않는다.
- dispatch error event는 실패한 message의 correlation ID와 flow ID를 가능한 범위에서 보존한다.
- invalid frame으로 ID를 읽을 수 없으면 새 ID를 만들어 원래 request처럼 보이게 하지 않는다.

flow ID는 retry 허가가 아니다. request 재시도 여부와 새 correlation ID 발급은 해당 messaging surface의
정책을 따른다.

## 8. 관측 정보와 privacy

trace event는 `correlation_id`, `flow_id`, `flow_origin` field로 값을 기록한다. fallback text key는 각각
`corr`, `flow`, `origin`이다. metric label에는 세 값을 모두 사용하지 않는다.

Framework는 flow ID와 correlation ID에 user ID, Actor ID, [Spot ID](01-glossary.ko.md#spot-id), endpoint 또는 payload 값을 encode하지
않는다. application이 외부 trace context와 연결할 때는 별도 observability adapter가 같은 event를
참조하며 Framework ID 형식을 바꾸지 않는다.

## 9. 구현 및 contract test 검증 요구

- request와 terminal reply가 같은 correlation ID를 사용하고 한 번만 완료된다.
- 같은 causal flow의 Node·Channel·Spot·Actor·STREAM hop이 같은 flow ID를 사용한다.
- Instance Spot의 source 조회, [activation envelope](01-glossary.ko.md#activation-envelope), target-owned claim,
  [activation barrier](01-glossary.ko.md#activation-barrier)와 최초 handler가 같은 flow·correlation을 유지한다. 생성
  권한을 얻지 못한 target이 [Ready](01-glossary.ko.md#ready) owner로 message를
  전달해도 새 ID를 만들지 않는다.
- Logical Multicast와 classic fanout의 모든 branch가 root flow ID를 보존한다.
- tracing off node도 inbound flow ID를 다음 관련 hop에 전달한다.
- callback 종료 뒤 관련 없는 callback에 flow context가 남지 않는다.
- stale session binding과 늦은 reply가 새 correlation에 연결되지 않는다.
- handler가 다른 ChannelName route로 시작한 downstream request는 새 correlation ID를 사용하고, 원래
  Spot·Actor activation에는 terminal completion이 한 번만 전달된다.
- correlation ID와 flow ID가 metric label이나 application metadata value로 사용되지 않는다.
