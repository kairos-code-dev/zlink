# SPOT 메시징 — 공통 스펙

[스펙 목차](../README.ko.md) · [Channel 메시징](11-channel-messaging.ko.md) ·
[MeshNode](21-mesh-node.ko.md) · [Spot 주소 메시징](24-spot-address-messaging.ko.md)

## 1. 범위

이 문서는 ZLink Framework 10.0.0에서 Spot direct 메시징과 channel-scoped Logical Multicast의 공통
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

classic fanout은 별도 PUB/SUB socket 계약이다. 서비스 event fanout과 Spot Logical Multicast는 서로
다른 기능이며 어느 한쪽이 다른 쪽의 연결이나 구독 상태를 공유하지 않는다.

## 3. Spot direct

Spot direct send/request는 논리 Spot identity를 대상으로 한다. Framework는 Spot 주소에서 확인한 owner
MeshNode로 한 번 route하고, 수신 MeshNode는 target Spot의 application queue에 payload를 제출한다.

- local Spot과 remote Spot은 같은 handler 및 실행 의미를 가진다.
- 호출자는 owner RID, endpoint 또는 내부 route frame을 조립하지 않는다.
- Spot direct request를 다른 Spot으로 자동 재전송하지 않는다.
- owner 변경과 stale 주소 처리 규칙은
  [24 Spot 주소 메시징](24-spot-address-messaging.ko.md)이 정한다.

## 4. Channel-scoped Logical Multicast

Logical Multicast는 `(MeshName, ChannelName, topic)`으로 대상 범위를 정한다. ChannelName은 물리 socket이
아니라 MeshNode membership이며, topic은 해당 channel 안에서 local Spot subscription을 고르는 key다.

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

### 4.1 NoDrop

Logical Multicast의 `NoDrop` 기본값은 `true`다.

| 설정 | publish 수락 계약 |
|---|---|
| `NoDrop = true` | local 대상과 모든 remote target의 admission이 하나의 operation으로 성공해야 수락한다. backpressure가 있으면 send timeout까지 기다리고, 제한 시간 안에 수락할 수 없으면 실패한다. |
| `NoDrop = false` | 수락 가능한 대상에는 제출하고 막힌 대상은 drop할 수 있다. drop은 runtime 관측 경로에 기록한다. |

publish 수락은 subscriber handler 실행이나 업무 처리를 확인하는 acknowledgement가 아니다. 기본 정책은
전달 대상마다 queue 수락까지 보장하며, durable 저장·재생·exactly-once 전달을 뜻하지 않는다.

publish 결과는 remote target과 local Spot match 각각의 snapshot, admitted와 dropped 수를 제공한다.
`NoDrop = true` 성공에서는 두 dropped 수가 모두 0이다. `NoDrop = false` 성공에서는 local과 remote의
부분 수락을 각각 구분할 수 있어야 한다.

설정의 정확한 이름과 표현은 언어별 공개 인터페이스 문서가 정한다. `.NET`의 정확한 표면은
[RouteMesh·MeshNode 인터페이스](languages/dotnet/05-route-mesh.ko.md)를 따른다.

## 5. Subscription과 dispatch

Spot subscription은 ChannelName과 topic으로 범위를 고정하고 packet name으로 typed handler를 선택한다.
등록한 Spot이 해당 ChannelName 범위에 속하지 않으면 startup 오류다. 같은 Spot에서 같은 ChannelName,
topic, message kind와 packet name을 중복 등록하면 startup 오류다.

Spot control claim은 Actor membership과 lifecycle control에 따라 Spot 소유 상태를 바꾸는 작업이다.
이 claim은 target Spot turn에서 다른 Spot-owned callback과 직렬화되며 Actor 업무 payload를 포함하지 않는다.
control 작업의 닫힌 범위와 Actor control claim과의 순서는
[22 Actor 모델 §4](22-actor-model.ko.md#4-spot-control-claim)가 정한다.

Spot application queue에는 다음 Spot 소유 작업이 들어간다.

- Spot direct payload
- Logical Multicast에서 일치한 payload
- Spot timer callback
- Actor join·leave와 lifecycle control callback (Spot control claim)

같은 Spot의 application callback은 하나의 Spot turn에서 순서대로 실행한다. Actor 업무 payload는 이
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

관측 정보는 MeshName, ChannelName, origin RID, remote target 수, local match 수, admission 대기·실패,
drop과 Spot dispatch 결과를 구분해야 한다. topic과 Spot RID는 metric label로 사용하지 않는다.

## 8. 검증 요구

- Spot direct와 Logical Multicast가 MeshNode ROUTER 하나만 사용한다.
- Logical Multicast가 remote MeshNode마다 한 번만 전송되고 수신 node가 local subscription만 검사한다.
- 같은 node의 여러 target Spot이 immutable message storage를 공유한다.
- `NoDrop = true`가 local과 모든 remote admission을 하나의 수락 결과로 제공한다.
- Actor payload가 Spot application queue와 Spot callback을 거치지 않는다.
- join·leave와 lifecycle control만 Spot control claim으로 전달된다.
- classic fanout PUB/SUB과 Logical Multicast의 연결 및 구독 상태가 섞이지 않는다.
