# MeshNode — 공통 스펙

[스펙 목차](../README.ko.md) · [이전: SPOT 메시징](20-spot-messaging.ko.md) ·
[다음: Actor 모델](22-actor-model.ko.md) · [.NET 인터페이스](languages/dotnet/interfaces/03-configuration-topology.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 RouteMesh에 참여하는 MeshNode의 공통 공개 계약을 정의한다.
MeshNode는 물리 연결, 논리 channel membership, Node·Channel 메시징, Spot·Actor와 STREAM session이
공유하는 server-side runtime이다.

## 2. Identity와 membership

MeshNode 하나는 다음 identity를 가진다.

| 항목 | 계약 |
|---|---|
| MeshName | 물리 RouteMesh와 RID namespace를 구분하는 immutable 이름 하나 |
| Routing ID | 같은 RouteMesh에서 MeshNode를 식별하는 값 하나 |
| Endpoint | peer가 연결할 ROUTER endpoint 하나 |
| ChannelName set | 0개 이상의 immutable Server membership |
| Instance Spot type set | startup 전에 등록한 0개 이상의 immutable factory type |
| Lifecycle generation | 같은 RID로 다시 시작한 MeshNode를 구분하는 단조 증가 값 |
| Descriptor revision | 같은 lifecycle 안에서 channel weight snapshot 변경을 구분하는 단조 증가 값 |

같은 process에는 같은 MeshName의 MeshNode를 하나만 등록할 수 있다. 서로 다른 MeshName의 MeshNode는
여러 개 등록할 수 있으며 mesh 사이의 자동 relay는 없다. `ChannelName`은 별도 socket이나 endpoint를
만들지 않는다. Descriptor를 게시한 뒤 Server membership이나 Instance Spot type을 추가하거나 제거할 수 없다. Node direct 호출이나
다른 process의 ChannelName target 호출만 수행하는 MeshNode는 Server membership 없이 등록할 수 있다.

## 3. 등록과 startup

Framework는 다음 순서로 MeshNode를 시작한다.

1. MeshName, RID, endpoint, channel set, handler와 Spot·Actor 등록을 검증한다. Instance factory type 중복,
   Actor capability, type별 limit과 activation timeout도 이 단계에서 검사한다.
2. Redis location store를 사용하는 경우 routing ID allocation과 lease를 확보한다.
3. MeshNode ROUTER를 bind하고 실제 endpoint를 확정한다.
4. Service runtime이 manual peer intent 또는 같은 MeshName의 location descriptor를 peer connection intent로 조정한다.
5. peer admission, local subscription과 handler 준비가 끝난 뒤 channel 선택 대상으로 공개한다.

자동 discovery, 분산 Spot·Actor 주소, InstanceSpotAddress와 Actor transfer를 사용하는 host는 공식 Redis location store를
명시적으로 등록해야 한다. 등록하지 않으면 startup이 실패한다. Manual mode는 endpoint 또는 expected RID와
endpoint를 application이 모두 제공한다. 두 mode는 연결 방법만 다르고 admission 이후 메시징 의미는 같다.

## 4. Peer admission

연결된 peer는 MeshName, RID, lifecycle generation, descriptor revision, immutable Server ChannelName set,
channel별 weight와 security identity를 handshake에서 교환한다. ChannelName set은 비어 있을 수 있다.
MeshName 또는 trust profile이 다르거나 같은 generation의 RID가
중복되면 admission하지 않는다. 더 높은 generation은 해당 RID의 이전 pipe를 drain한 뒤 선택 대상에
포함한다.

Channel weight를 실행 중 바꾸면 lifecycle generation은 유지하고 descriptor revision만 증가시킨다.
MeshNode는 같은 revision의 descriptor를 location store와 admitted peer에 게시한다. peer는 더 큰 revision의
전체 weight snapshot만 적용하며 중간 revision을 받지 못해도 다음 snapshot으로 수렴한다. weight 변경은
connection 재생성이나 application message replay를 일으키지 않는다.

양쪽에서 동시에 연결을 시도해도 같은 RID와 lifecycle generation의 peer는 ready 연결 하나로 수렴한다.
Application은 내부 initiator 선택이나 중복 pipe를 관찰하거나 설정하지 않는다.

## 5. 메시징

| 대상 | 선택과 전달 |
|---|---|
| Node direct | 같은 MeshName의 target RID 하나로 전송 |
| Channel | process-local ChannelName route가 이 MeshNode를 선택한 경우, 같은 MeshName의 ready Server member 하나를 positive weight round-robin으로 선택 |
| Logical Multicast | target ChannelName의 ready member 전체와 조건부 local Spot subscription에 전달 |
| Spot direct | location runtime이 확인한 owner MeshNode와 Spot RID로 전송 |
| Instance Spot direct | address의 MeshName·Instance type·Spot RID를 유지하고 location claim이 정한 owner로 전송 |
| Actor direct | ActorRef의 owner route와 generation을 검증해 Actor application queue로 전송 |

선택과 submit은 한 operation이다. 선택한 RID 목록을 application에 반환한 뒤 별도 send를 요구하지 않는다.
Node·Channel·Spot·Actor의 send/request는 같은 MeshNode ROUTER를 사용한다. classic fanout은 별도
PUB/SUB socket 계약이며 MeshNode membership과 합치지 않는다.

## 6. Handler와 dispatch

ChannelName handler와 RID direct route handler는 서로 다른 namespace를 사용한다. Channel handler context는
ChannelName과 reply에 필요한 source identity를 내부에 보존하되 MeshName이나 물리 route 선택을 업무 코드에
노출하지 않는다. Route handler context는 direct route의 MeshName과 source RID를 제공한다.

각 언어 service runtime은 application과 infrastructure domain을 독립적으로 진행한다. Node, Spot과 Actor
payload는 각 owner의 application turn에서 직렬로 처리하고 completion, send-ready와 transfer control은
infrastructure task에서 계속 진행한다. Transport readiness event에서 application callback을 직접 실행하지 않는다.

## 7. Spot과 Actor

Entry Spot, Domain Spot factory, Instance Spot factory와 maintenance policy를 포함한 Actor factory는 MeshNode
registration에 속한다. Spot
생성·조회·종료, Actor lifecycle, location transparency와 handler 의미는 각각
[20 SPOT 메시징](20-spot-messaging.ko.md), [22 Actor 모델](22-actor-model.ko.md),
[23 Spot Actor](23-spot-actor.ko.md)이 소유한다.

Instance factory는 actor-free lifecycle만 구현하며 direct packet과 timer handler를 등록할 수 있다. Type별
option을 생략하면 local MeshNode와 type마다 `MaxActiveInstances=4096`, `ActivationTimeout=3초`를
사용한다. Option을 명시하면 두 값은 모두 0보다 커야 하며 0을 sentinel로 사용하지 않는다. Instance
pending message·byte budget과 activation deadline은 MeshNode service runtime이 소유하며 type별 public option으로
반복하지 않는다.

Automatic discovery descriptor는 immutable Instance Spot type set을 게시한다. Placement는 같은 MeshName에서
serving 상태이고 drain 중이 아니며 요청 type을 제공하는 node만 후보로 사용한다. Candidate selection
algorithm은 public 계약이 아니며 correctness는 Location Store CAS가 확정한 owner 하나와 target activation
barrier가 보장한다.

Spot Logical Multicast는 `(ChannelName, topic filter)` subscription을 node-local로 검사한다. 송신 MeshNode는
target channel의 remote node마다 routed message를 한 번 제출한다. 수신 MeshNode는 local match마다 같은
immutable message storage의 reference를 확보해 Spot queue에 넣는다.

## 8. Drain과 종료

`Retire` 또는 `Shutdown`이 `Draining`으로 전환하면 새 Channel·Logical Multicast 선택과 Instance placement
후보에서 MeshNode를 즉시 제외한다. 이미 수용한 message, active application claim, completion, Actor transfer와
STREAM session barrier는 deadline까지 진행한다. 종료 순서는
[54 Graceful Drain](54-graceful-drain-handoff.ko.md)이 소유한다.

`Shutdown`은 새 stateful transfer를 시작하지 않는다. Drain 전에 수락한 Instance activation을 deadline까지
처리하고 Framework admission seal과 current location authority를 확인해 `Closing`과 release를 수행한다.
`Retire`는 existing Actor·Instance owner에 type별 maintenance policy를 적용하고 target activation이 commit된 뒤
source resource를 정리한다. Public local create나 stale handle 요청을 remote materialization으로 바꾸지 않는다.

## 9. 관측

status와 event는 MeshName, RID, lifecycle generation, local endpoint, channel membership, peer admission,
application/infrastructure backlog, multicast submit·drop과 drain state를 제공한다. topic, Actor ID와 Spot RID
같은 고카디널리티 값은 metric label로 사용하지 않는다.

## 10. 검증 요구

- 같은 process의 중복 MeshName과 잘못된 Client·Server 역할 구성이 startup에서 실패한다.
- Server membership이 없는 MeshNode가 Node direct와 outbound Channel 호출을 수행할 수 있다.
- 서로 다른 MeshName 사이에 peer와 channel member가 섞이지 않는다.
- channel select-one이 weight와 drain을 반영하고 RID direct에는 영향을 주지 않는다.
- Logical Multicast가 remote node당 한 번 전송되고 node-local Spot queue가 storage를 공유한다.
- Application turn의 single-owner 실행, infrastructure progress와 lost-wakeup 방지가 검증된다.
- classic fanout-only와 STREAM-only host는 MeshNode를 만들지 않는다.
- Instance factory type set이 descriptor와 일치하고 startup 뒤 변경되지 않는다.
- 같은 Instance type·class의 중복 등록, Actor-capable factory와 잘못된 limit이 startup에서 실패한다.
- draining node가 새 Instance placement target이 되지 않고 accepted activation만 deadline까지 진행한다.
