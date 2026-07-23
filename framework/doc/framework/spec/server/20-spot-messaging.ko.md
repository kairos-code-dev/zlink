# SPOT 메시징 — 공통 스펙

[스펙 목차](../README.ko.md) · [Channel 메시징](11-channel-messaging.ko.md) ·
[MeshNode](21-mesh-node.ko.md) · [Spot 주소 메시징](24-spot-address-messaging.ko.md)

## 1. 범위

이 문서는 ZLink Framework 11.0.0에서 Spot direct 메시징과 channel-scoped Logical Multicast의 공통
공개 계약을 정의한다. 이 문서는 “논리 Spot 하나 또는 같은 channel의 여러 Spot에 메시지를 보낼 때
대상과 실행 순서를 어떻게 결정하는가?”라는 질문에 답한다.

Spot은 room, stage, zone처럼 주소와 상태를 가진 논리 인스턴스다. 물리 연결은
[21 MeshNode](21-mesh-node.ko.md), 위치 투명한 직접 호출은
[24 Spot 주소 메시징](24-spot-address-messaging.ko.md), payload와 metadata는
[03 메시지 모델](../03-message-model.ko.md), callback의 실행 규칙은
[04 비동기 실행 정책](../04-async-execution-policy.ko.md)이 소유한다.

## 2. Spot과 MeshNode

User·Instance Spot은 Location Store namespace 전체에서 전역인 logical Spot RID로 식별된다. RID는 RoutingId의
1..255-byte exact value이며 MeshName은 최초 placement attribute이지 identity key가 아니다. 같은 RID를 서로
다른 MeshName, Spot kind 또는 stable type에 중복 사용할 수 없다. Entry Spot RID는 Framework가 발급하며 caller가
create 대상으로 지정하지 않는다.

Spot factory, Entry Spot과 Spot lifecycle 등록은 Object Server role의 MeshNode만 제공한다. Object Client는
logical create·lookup·message를 시작할 수 있지만 factory나 Entry Spot을 게시하지 않는다. Server는 Client
capability를 포함한다. Client·Server role은 Location Store를 요구하며, role이 `None`이면 manager, factory와
hidden local object runtime을 제공하지 않는다.

Spot direct와 Logical Multicast는 Node·Channel 메시징과 같은 MeshNode ROUTER를 사용하며 Spot 전용 ROUTER 또는
PUB/SUB mesh를 만들지 않는다. Framework가 logical create에서 eligible remote server를 선택할 수 있지만
application은 target RID, endpoint와 owner generation을 지정하지 않는다. Instance Spot의 Missing authority는
Spot direct fluent call이 명시한 Instance intent로 cold activation할 수 있다. 별도 create operation은 제공하지
않는다. 자세한 identity, creation과 cold activation은
[24 Spot 주소 메시징](24-spot-address-messaging.ko.md)이 소유한다.

classic fanout은 별도 PUB/SUB socket 계약이다. 서비스 event fanout과 Spot Logical Multicast는 서로
다른 기능이며 어느 한쪽이 다른 쪽의 연결이나 구독 상태를 공유하지 않는다.

## 3. Spot direct

Spot direct send/request의 시작 method는 global Spot RID만 대상으로 받는다. Framework는 positive route cache
또는 Location Store에서 current Ready incarnation과 owner route를 resolve하고, 선택한 ObjectGeneration과 owner
fence를 target admission에 고정한다. 수신 MeshNode는 target Spot의 application queue에 payload를 제출한다.

Spot 전용 call builder가 Instance intent를 갖지 않으면 Missing Spot에서 target-not-found로 끝난다. Instance
intent를 명시하면 optional stable type과 최초 Mesh·placement option을 사용해 cold activation할 수 있다. Type을
생략한 경우 선택한 Mesh에 등록된 distinct Instance type이 하나일 때만 자동 선택한다. 여러 MeshNode가 같은
type을 등록한 것은 distinct type 하나다. 여러 type이 있으면 caller가 stable type을 명시해야 한다. Existing
authority에는 Location Store가 보유한 kind·type과 current Mesh를 사용하며 MeshName을 messaging target으로
요구하지 않는다.

Spot direct send는 비동기 submit 하나만 제공한다. Immediate-only 동기 terminator는 제공하지 않으며, queue가
일시적으로 가득 차면 owner MeshNode ROUTER의 유한한 send timeout까지 admission을 기다린다. 완료 결과는
local outbound admission만 나타내고 target Spot handler 실행은 기다리지 않는다. `Submitted`,
`Backpressured`, `TimedOut`, `TargetNotFound`, `RouteNotConnected`, `Shutdown`의 의미와 cancellation·local
오류 경계는 [04 비동기 실행 정책 §1.3](../04-async-execution-policy.ko.md#13-one-way-submit)을 따른다.
Cold activation을 포함하는 submit도 source outbound admission에서 같은 결과를 완료하며 target application queue의
handler 실행을 기다리지 않는다. Target runtime은 location owner claim을 새로 만들지 않고 committed authority를
검증한다.

- local Spot과 remote Spot은 같은 handler 및 실행 의미를 가진다.
- 호출자는 owner RID, endpoint 또는 내부 route frame을 조립하지 않는다.
- Spot direct request를 다른 Spot으로 자동 재전송하지 않는다.
- owner 변경, route cache와 stale route 처리 규칙은
  [24 Spot 주소 메시징](24-spot-address-messaging.ko.md)이 정한다.
- Instance Spot의 별도 create request는 없다. Fluent call의 최초 application message는 actor-free lifecycle의
  Configure와 initialize, location `Ready` commit과 activation barrier가 끝난 뒤 일반 direct payload로 한 번
  dispatch된다.

### 3.1 Spot에서 Channel 호출

Spot handler와 timer가 시작하는 Channel send/request는 ChannelName으로 process-local 송신 경로를 고른다.
현재 Spot을 소유한 MeshNode에 대상 ChannelName이 없더라도 다른 RouteMesh 송신 경로 또는 ClientServer
client가 같은 process에 등록되어 있으면 해당 경로를 사용할 수 있다. 대상 송신 경로가 없으면 다른
process나 MeshNode를 relay로 사용하지 않고 `RequestTargetNotFound`로 끝낸다.

Framework는 다른 송신 경로의 request correlation과 원래 Spot activation·generation을 함께 보존한다.
`Async`는 원래 turn을 유지하고, `Yield`는 turn을 반환한 뒤 completion이 확정되면 원래 Spot queue에 실행
재개 record 하나를 넣는다. Reply를 Spot packet으로 다시 dispatch하지 않는다. Spot shutdown, timeout,
cancellation과 reply가 경쟁해도 terminal completion은 하나이며 이전 Spot generation의 늦은 reply를 새
activation에 전달하지 않는다.

이 동작은 Framework process 안의 송신 경로 선택이다. Service runtime은 등록되지 않은 다른 RouteMesh를
찾거나 RouteMesh 사이를 relay하지 않으며, ClientServer transport가 원래 Spot RID를 물리 source identity로
사용하지 않는다.

## 4. Channel-scoped Logical Multicast

Logical Multicast는 `(ChannelName, topic)`으로 대상 범위를 정한다. Process-local channel index는
ChannelName이 속한 RouteMesh의 owner MeshNode를 선택하며 호출자에게 MeshName이나 endpoint를
요구하지 않는다. 같은 ChannelName을 다른 RouteMesh나 ClientServer 송신 경로에 중복 등록하면
host startup이 실패한다. ChannelName은 물리 socket이 아니라 MeshNode membership이며, topic은
해당 channel 안에서 local Spot subscription을 고르는 key다.

publish는 다음 단계를 하나의 Framework operation으로 수행한다.

1. target ChannelName의 positive-weight ready remote MeshNode마다 routed message를 한 번 제출한다.
2. origin MeshNode도 target ChannelName에 참여하면 node-local subscription을 검사한다.
3. 각 수신 MeshNode는 local subscription만 검사한다.
4. 일치하는 각 Spot application queue에 동일한 immutable message storage의 reference를 제출한다.

같은 node의 여러 Spot에 전달할 때 payload를 Spot 수만큼 다시 encode하거나 복사하지 않는다. 각 queue가
message reference를 보유하고 마지막 consumer가 반환하면 storage를 회수한다. 이 reference 관리 방식은
application API에 노출하지 않는다.

remote node의 Spot 목록이나 peer별 queue를 호출자에게 반환하지 않는다. 호출자가 공개 Node direct
send를 반복해서 Logical Multicast를 구성하는 방식은 공통 계약이 아니다.

### 4.1 Target별 수락

Logical Multicast는 publish 전용 전달 정책 option을 제공하지 않는다. Framework의 bounded I/O executor는
대기 queue 없이 worker slot을 direct handoff한다. 즉시 사용할 slot이 없으면 publish transaction을 시작하지
않고 `Backpressured`로 완료한다. Handoff에 성공하면 service runtime은 snapshot의 각 remote target에 한 번
제출하고 target별 send timeout을 적용한다. Local Spot queue는 target별로 즉시 수락하며, 용량이 없으면 해당
target을 drop 수에 기록하고 기다리지 않는다.

Publish transaction이 시작되면 snapshot operation은 commit된 것이다. 이후 cancellation이나 shutdown으로
남은 target 제출을 중단하지 않는다. 뒤 target에서 backpressure가 발생해도 앞에서 성공한 제출을 취소하지 않는다.
Executor direct handoff가 성공하지 못했거나 remote capacity가 부족하면 `Backpressured`,
snapshot target이 모두 0이면 `TargetNotFound`다. Target별 send timeout 뒤의 용량 실패는 `TimedOut`으로
다시 분류하지 않는다. Remote 연결 불가와 local Spot queue drop은 top-level status를 바꾸지 않고 publish
detail에 기록한다. Remote target이 모두 연결 불가여서 admitted count가 0이어도 remote capacity drop이
없으면 `Submitted`다.

publish 수락은 subscriber handler 실행이나 업무 처리를 확인하는 acknowledgement가 아니다. 특히 remote
ROUTER의 송신 수락은 수신 MeshNode의 local Spot queue 수락을 보장하지 않으며, durable 저장·재생·
exactly-once 전달을 뜻하지 않는다.

publish 결과는 remote target의 snapshot, admitted, dropped, unreachable 수와 local Spot match의 snapshot,
admitted, dropped 수를 제공한다. remote target 하나 이상이 용량 때문에 수락되지 않으면 backpressure
결과를 반환하되 이미 수락된 target은 유지한다.

## 5. Subscription과 dispatch

Spot subscription은 ChannelName과 topic으로 범위를 고정하고 packet name으로 typed handler를 선택한다.
등록한 Spot이 해당 ChannelName 범위에 속하지 않으면 startup 오류다. 같은 Spot에서 같은 ChannelName,
topic, message kind와 packet name을 중복 등록하면 startup 오류다.

Spot control claim은 Actor membership과 lifecycle control에 따라 Spot 소유 상태를 바꾸는 작업이다.
이 claim은 target Spot turn에서 다른 Spot-owned callback과 직렬화되며 Actor 업무 payload를 포함하지 않는다.
control 작업의 닫힌 범위와 Actor control claim과의 순서는
[22 Actor 모델 §4](22-actor-model.ko.md#4-spot-control-claim)가 정한다.

Spot application queue에는 다음 Spot 소유 작업을 추가한다.

- Spot direct payload
- Logical Multicast에서 일치한 payload
- Spot timer callback
- Actor join·leave와 lifecycle control callback (Spot control claim)

Instance Spot queue에는 direct payload와 timer callback만 추가한다. Actor control과 Logical Multicast
subscription은 Instance Spot registration 또는 activation 단계에서 거부한다.

같은 Spot의 application callback은 하나의 Spot turn에서 순서대로 실행한다. 다만 callback이 `Yield`로
turn을 반납하면 같은 Spot의 다음 application record가 먼저 실행될 수 있으며, 완료 continuation은 새
turn에서 재개한다([Async 실행 정책 §1.1](../04-async-execution-policy.ko.md#11-submit-async와-yield)). Actor 업무 payload는 이
queue나 Spot callback에 넣지 않고 Actor queue로 직접 제출한다. Actor가 Spot 상태를 바꾸려면 명시적인
Spot 호출을 제출해야 한다. Actor payload와 membership 제어의 경계는
[22 Actor 모델](22-actor-model.ko.md)이 소유한다.

ready notification, completion, send-ready와 relocation progress 같은 infrastructure 작업은 Spot
application claim과 분리한다. application callback이 대기 중이어도 infrastructure progress가 막히지
않아야 한다.

## 6. 실패와 수명

- target Spot의 Ready authority가 없으면 Spot target 오류로 끝난다. Exact-ref operation은 location과
  ObjectGeneration을 구분해 stale 오류를 반환한다.
- request handler를 찾지 못하거나 decode에 실패했을 때 reply route를 복원할 수 있으면 error reply로
  끝낸다.
- one-way Spot direct와 Logical Multicast handler 실패는 원래 호출을 request로 바꾸지 않으며 runtime
  관측 경로에 기록한다.
- Spot을 종료하면 신규 application payload admission을 닫고 이미 수락한 turn과 lifecycle 정리를
  drain deadline 안에서 처리한다.
- Logical Multicast는 종료된 Spot의 subscription을 local match에서 제외한다.

one-way와 request completion의 공통 의미는
[04 비동기 실행 정책](../04-async-execution-policy.ko.md), 전체 drain 순서는
[54 Graceful Drain](54-graceful-drain-handoff.ko.md)이 정한다.

## 7. Metadata와 관측

Spot direct와 Logical Multicast는 [03 메시지 모델](../03-message-model.ko.md)의 immutable metadata
snapshot을 사용한다. metadata ownership, 크기와 reply 규칙은 이 문서에서 다시 정의하지 않는다.

관측 정보는 current owner MeshName, ChannelName, origin RID, remote target 수, local match 수, admission 대기·실패,
drop과 Spot dispatch 결과를 구분해야 한다. topic과 Spot RID는 metric label로 사용하지 않는다.

## 8. 검증 요구

- Spot direct와 Logical Multicast가 MeshNode ROUTER 하나만 사용한다.
- Spot direct가 global Spot RID만 받고 MeshName, owner RID와 generation을 application에 요구하지 않는다.
- Instance intent가 없는 Missing Spot message가 type·Mesh를 새로 제공하거나 creation intent를 만들지 않는다.
- Instance intent를 가진 call만 Missing Spot의 type을 명시하거나 유일한 type을 자동 선택해 cold activation한다.
- Spot Channel 호출이 ChannelName에 등록된 다른 RouteMesh 또는 ClientServer 송신 경로를 사용하고 원래
  Spot의 `Async`·`Yield`와 generation completion을 보존한다.
- Logical Multicast가 remote MeshNode마다 한 번만 전송되고 수신 node가 local subscription만 검사한다.
- Logical Multicast가 bounded executor direct handoff에서 publish transaction을 한 번만 시작하고 commit 뒤 cancellation이
  snapshot의 나머지 target 제출을 중단하지 않는다.
- 같은 node의 여러 target Spot이 immutable message storage를 공유한다.
- local과 remote target의 독립 수락 결과가 snapshot·admitted·dropped·unreachable 수로 집계된다.
- Actor payload가 Spot application queue와 Spot callback을 거치지 않는다.
- join·leave와 lifecycle control만 Spot control claim으로 전달된다.
- classic fanout PUB/SUB과 Logical Multicast의 연결 및 구독 상태가 섞이지 않는다.
