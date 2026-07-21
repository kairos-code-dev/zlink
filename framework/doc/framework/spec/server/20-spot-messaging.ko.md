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

모든 분산 Spot은 정확히 하나의 MeshName과 owner MeshNode에 속한다. Spot factory, Entry Spot과 Spot
lifecycle 등록도 해당 MeshNode가 소유한다. Spot direct와 Logical Multicast는 Node·Channel 메시징과 같은
MeshNode ROUTER를 사용하며 Spot 전용 ROUTER 또는 PUB/SUB mesh를 만들지 않는다.

Entry·Domain Spot 생성과 `GetOrCreate`는 호출을 받은 local MeshNode에서만 수행한다. SpotHandle을 사용하는
remote Spot resolve와 메시징은 이미 게시된 owner location row를 사용하며, 존재하지 않는 Spot의 serving
MeshNode를 고르거나 다른 MeshNode에 생성 요청을 전달하지 않는다.

명시적인 `InstanceSpotAddress`를 사용하는 direct send/request만 이 existing-only 규칙의 예외다. Instance
Spot은 Actor membership이 없는 별도 Spot kind이며, 첫 주소 호출의 source runtime이 outbound 전 location claim을
수행하고 target activation을 시작할 수 있다. Caller는 owner node와 generation을 선택하지 않는다. Address, activation, owner fencing과
재제출 제한은 [24 Spot 주소 메시징](24-spot-address-messaging.ko.md)이 소유한다.

classic fanout은 별도 PUB/SUB socket 계약이다. 서비스 event fanout과 Spot Logical Multicast는 서로
다른 기능이며 어느 한쪽이 다른 쪽의 연결이나 구독 상태를 공유하지 않는다.

## 3. Spot direct

Spot direct send/request는 논리 Spot identity를 대상으로 한다. SpotHandle 호출에서는 Framework가 주소에서
확인한 owner MeshNode로 한 번 route하고, 수신 MeshNode는 target Spot의 application queue에 payload를
제출한다. InstanceSpotAddress 호출에서는 location claim과 activation barrier를 먼저 처리하고 `Ready` 뒤 같은
application queue에 payload를 제출한다. Missing Spot의 cold activation은 Framework service runtime의 Instance
placement operation만 사용한다. Location Store가 `Ready` owner를 반환하면 Framework는 node RID, Spot RID와
Spot generation을 받는 exact Spot direct 경로를 사용한다.

Spot direct send는 비동기 submit 하나만 제공한다. Immediate-only 동기 terminator는 제공하지 않으며, queue가
일시적으로 가득 차면 owner MeshNode ROUTER의 유한한 send timeout까지 admission을 기다린다. 완료 결과는
local outbound admission만 나타내고 target Spot handler 실행은 기다리지 않는다. `Submitted`,
`Backpressured`, `TimedOut`, `TargetNotFound`, `RouteNotConnected`, `Shutdown`의 의미와 cancellation·local
오류 경계는 [04 비동기 실행 정책 §1.3](../04-async-execution-policy.ko.md#13-one-way-submit)을 따른다.
Instance cold send도 source outbound admission에서 같은 결과를 완료하며 target activation queue 수락을
기다리지 않는다. Target runtime은 location owner claim을 수행하지 않는다.

- local Spot과 remote Spot은 같은 handler 및 실행 의미를 가진다.
- 호출자는 owner RID, endpoint 또는 내부 route frame을 조립하지 않는다.
- Spot direct request를 다른 Spot으로 자동 재전송하지 않는다.
- owner 변경과 stale 주소 처리 규칙은
  [24 Spot 주소 메시징](24-spot-address-messaging.ko.md)이 정한다.
- Instance Spot의 첫 message는 lifecycle callback 인자가 아니다. Actor-free lifecycle의 Configure와
  message 없는 initialize, location `Ready` commit과 Framework activation barrier가 끝난 뒤 일반 direct handler에 한 번
  전달한다.

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

ready notification, completion, send-ready와 transfer progress 같은 infrastructure 작업은 Spot
application claim과 분리한다. application callback이 대기 중이어도 infrastructure progress가 막히지
않아야 한다.

## 6. 실패와 수명

- target Spot이 없거나 owner generation이 맞지 않으면 Spot target 오류로 끝난다.
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

관측 정보는 선택된 owner MeshName, ChannelName, origin RID, remote target 수, local match 수, admission 대기·실패,
drop과 Spot dispatch 결과를 구분해야 한다. topic과 Spot RID는 metric label로 사용하지 않는다.

## 8. 검증 요구

- Spot direct와 Logical Multicast가 MeshNode ROUTER 하나만 사용한다.
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
