# Channel 메시징 — 공통 스펙

[스펙 목차](../README.ko.md) · [Channel topology](10-channel-topology.ko.md) ·
[ClientServer Channel](12-client-server-channel.ko.md) ·
[MeshNode](21-mesh-node.ko.md) · [.NET 인터페이스](languages/dotnet/interfaces/04-channel-messaging.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 Node direct와 ChannelName select-one 메시징의 공통 공개 계약을
정의한다. 이 문서는 “호출자가 특정 node 또는 논리 channel을 대상으로 메시지를 보내면
어떤 대상을 선택하고 어떤 실행 문맥에서 처리하는가?”라는 질문에 답한다.

물리 연결과 membership은 [21 MeshNode](21-mesh-node.ko.md), payload와 metadata는
[03 메시지 모델](../03-message-model.ko.md), submit·request·실행 순서는
[04 비동기 실행 정책](../04-async-execution-policy.ko.md)이 소유한다. 이 문서는 언어별 타입 이름과
시그니처를 정하지 않는다.

## 2. 대상 모델

| 메시징 축 | 호출자가 지정하는 값 | 대상 선택 |
|---|---|---|
| Node direct | MeshName과 target RID | 같은 MeshName에서 해당 RID를 가진 ready MeshNode 하나 |
| ChannelName select-one | ChannelName | process-local 등록이 가리키는 RouteMesh의 positive-weight ready member 또는 ClientServer의 ready server 하나 |

Node direct는 지정한 RouteMesh MeshNode를 사용한다. ChannelName은 소켓·endpoint 이름이 아니며 process의
송신 경로 등록을 통해 RouteMesh MeshNode 또는 ClientServer client 하나를 가리킨다. RouteMesh
ChannelName을 추가해도 ROUTER나 peer 연결을 별도로 만들지 않는다.

한 process에 서로 다른 MeshName의 MeshNode와 ClientServer client를 여러 개 등록할 수 있다. 같은
ChannelName을 물리 topology 둘 이상에 등록할 수 없다. 같은 ClientServer ChannelName의 Client와 Server를
각각 한 번 등록한 구성은 Client 송신 경로 하나와 그 경로가 선택할 수 있는 local Server 하나를 함께
만드는 것이며 두 송신 경로로 계산하지 않는다. 등록하지 않은 ChannelName을 호출해도 다른 MeshNode나
ClientServer client로 자동 전달하거나 fallback하지 않는다.

## 3. 선택과 submit

Node direct는 target RID를 다른 node로 바꾸지 않는다. target RID가 member가 아니거나 ready 상태가 되지
않으면 해당 호출의 target 오류 또는 timeout으로 끝난다.

ChannelName select-one은 다음 기준을 하나의 원자적 operation으로 적용한다.

1. ChannelName에 대응하는 process-local 송신 경로 하나를 찾는다.
2. RouteMesh 경로이면 같은 MeshName의 Server membership만, ClientServer 경로이면 해당 ChannelName의 ready
   server만 선택 대상으로 삼는다.
3. Weight가 0인 target과 drain 중인 target을 새 선택에서 제외한다.
4. Positive weight 선택으로 한 target을 고르고 즉시 submit한다.

ClientServer의 local Server도 remote Server와 같은 후보다. Listener와 ClientServer service admission이
ready이고 weight가 양수이며 draining이 아닐 때만 후보에 포함한다. 같은 process라는 이유로 먼저 선택하거나
제외하지 않는다. 선택되면 local handler를 직접 호출하지 않고 Client DEALER에서 Server ROUTER로 실제
ClientServer record를 전송한다. Codec, admission, HWM, timeout, correlation과 reply 처리를 생략하는 local
transport bypass는 제공하지 않는다.

Framework는 선택한 RID를 중간 결과로 호출자에게 반환하지 않고 같은 operation에서 submit한다. request를 보낸
뒤 연결 종료나 timeout이 발생해도 다른 member에 자동 재전송하지 않는다. 이미 실행된 request를 중복
실행할 수 있기 때문이다.

one-way submit의 수락 의미와 request의 단일 terminal completion은
[04 비동기 실행 정책](../04-async-execution-policy.ko.md)이 정한다.

## 4. Handler와 실행 문맥

Node direct handler와 ChannelName handler는 서로 다른 namespace다.

| Handler 종류 | 식별 범위 | 실행 소유자 |
|---|---|---|
| Node direct | MeshName, message kind, packet name | target MeshNode의 Node application turn |
| ChannelName | ChannelName, message kind, packet name | 선택된 RouteMesh member 또는 ClientServer server의 channel application turn |

같은 namespace에서 같은 message kind와 packet name을 둘 이상 등록하면 startup 오류다. 서로 다른
ChannelName이나 Node direct namespace에서는 같은 packet name을 사용할 수 있다.

Channel handler와 handler filter context는 ChannelName, message kind, packet name, metadata와 correlation
정보를 제공하며 MeshName을 요구하지 않는다. RouteMesh 또는 ClientServer 종류, MeshName, source·target
RID와 endpoint는 monitoring과 message-flow 관측에 남긴다. Node direct handler context는 MeshName과
source·target RID를 유지한다.

Node direct payload를 Spot callback이나 Actor handler로 전달하지 않는다. Spot과 Actor를 대상으로 하는
메시지는 각각의 명시적인 대상 표면을 사용한다. reply route와 request correlation은 Framework가
보존하며 application handler가 source endpoint나 내부 frame을 조립하지 않는다.

## 5. Classic fanout과의 경계

classic fanout channel은 별도 PUB/SUB socket을 사용하는 독립 메시징 축이다. 현재 연결과 구독 준비가
완료된 subscriber에게 event를 전달하며 저장·acknowledgement·replay를 제공하지 않는다. classic fanout의
subscriber 집합을 MeshNode ChannelName membership이나 Spot Logical Multicast 대상 집합으로 해석하지
않는다.

Fanout public publish의 topic은 내부 liveness용 exact byte `01 5A 4C 46 31`을 사용할 수 없다. 이 값은
호출 인자 오류이며 다른 topic prefix를 제한하지 않는다. Reserved topic으로 받은 exact beacon은 subscriber
handler와 message-flow publish에 전달하지 않는다. Beacon의 wire와 timeout은
[Transport liveness](55-transport-liveness.ko.md)가 소유한다.

Spot의 channel-scoped Logical Multicast는
[20 SPOT 메시징](20-spot-messaging.ko.md)이 소유한다.

## 6. 실패와 종료

- Node direct target이 같은 MeshName의 member가 아니면 target-not-found 오류다.
- ChannelName이 process-local 송신 경로에 등록되지 않았거나 eligible target snapshot이 없으면
  `RequestTargetNotFound`다.
- 알려진 target이나 선택 대상의 연결이 제한 시간 안에 ready가 되지 않으면 route-not-connected 또는
  timeout으로 끝난다.
- handler를 찾지 못하거나 payload를 decode할 수 없는 request는 reply route를 복원할 수 있을 때
  error reply로 끝난다. one-way 메시지는 drop하고 runtime 관측 경로에 기록한다.
- drain 중인 member는 새 ChannelName 선택에서 제외하지만 RID direct의 의미를 바꾸지 않는다.
- host가 신규 submit을 받지 않는 상태가 되면 새 호출은 해당 언어의 종료 오류로 실패한다.
- ClientServer client가 제출한 request와 일치하지 않는 server message는 `RequestProtocolError`로 관측하며
  application handler에 전달하지 않는다.

오류 이름과 언어별 표현은 언어별 공개 인터페이스 문서가 정한다. 각 service runtime은 transport 오류를
공통 Framework 결과로 변환하며 transport 전용 result를 public call에 노출하지 않는다. Graceful drain의
전체 순서는 [54 Graceful Drain](54-graceful-drain-handoff.ko.md)이 소유한다.

## 7. Metadata와 관측

Node direct와 ChannelName 메시지는 [03 메시지 모델](../03-message-model.ko.md)의 immutable metadata
snapshot을 그대로 사용한다. 이 문서에서는 metadata key, 크기, reply 복사 규칙을 반복해서 정의하지
않는다.

관측 정보는 ChannelName, 송신 경로 종류, 조건부 MeshName, source·target RID 또는 ClientServer server
identity, 선택 결과, submit 결과, dispatch 결과와 drain state를 구분할 수 있어야 한다. Application
handler context에 이 물리 식별자를 추가하지 않으며 packet payload나 고카디널리티 업무 식별자를 metric
label로 사용하지 않는다.

## 8. 검증 요구

- Node direct가 지정한 RID 이외의 node로 전달되지 않는다.
- ChannelName이 process-local 송신 경로 하나만 선택하고 미등록 이름을 다른 경로로 fallback하지 않는다.
- ChannelName select-one이 RouteMesh membership 또는 ClientServer server의 weight, ready와 drain 상태를 함께 반영한다.
- 같은 process의 ClientServer Server도 remote와 같은 후보 집합과 transport 경로를 사용하며 local 우선 선택,
  local 제외와 direct handler 호출이 없다.
- RouteMesh의 Node direct와 ChannelName이 같은 MeshNode ROUTER를 사용하고 ChannelName별 소켓을 만들지
  않으며, ClientServer 송신 경로는 별도 client transport를 사용한다.
- 서로 다른 MeshName 사이에 target이나 handler namespace가 섞이지 않는다.
- request 실패 뒤 다른 channel member로 자동 재전송하지 않는다.
- Node direct payload가 Spot callback이나 Actor handler에 들어가지 않는다.
- 다른 송신 경로의 request completion이 원래 call을 한 번만 완료하고 reply를 application packet으로 다시 dispatch하지 않는다.
- Fanout reserved liveness topic을 public publish가 거부하고 exact beacon이 application handler로 전달되지 않는다.
