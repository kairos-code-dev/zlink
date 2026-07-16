# ZLink Framework 개요

[스펙 목차](README.ko.md) · [이전: 공개 계약 관리](00-public-contract-governance.ko.md) ·
[다음: 상호작용 모델](02-interaction-model.ko.md)

## 1. 한 줄 정의

ZLink Framework 10.0.0은 typed message handler, RouteMesh, Spot, Actor, STREAM session, classic fanout과
location runtime을 application host의 lifecycle과 DI에 연결하는 상위 계층이다.

## 2. RouteMesh와 MeshNode

`RouteMesh`는 같은 `MeshName`을 공유하는 MeshNode의 물리 연결 범위다. MeshNode 하나는 routing ID 하나와
ROUTER endpoint 하나를 가지며, 하나 이상의 immutable `ChannelName`에 참여한다.

`MeshName`과 `ChannelName`은 역할이 다르다.

| 이름 | 의미 |
|---|---|
| MeshName | 서로 메시징할 수 있는 물리 mesh와 RID namespace |
| ChannelName | 같은 mesh 안에서 select-one과 Logical Multicast 대상을 묶는 논리 membership |

한 process는 서로 다른 MeshName의 MeshNode를 여러 개 가질 수 있다. 각 mesh는 독립적이며 자동 relay를
제공하지 않는다. ChannelName을 추가해도 socket이나 endpoint가 추가되지 않는다.

## 3. 메시지 대상

MeshNode 위의 메시징은 대상 선택 방식으로 구분한다.

- node direct는 같은 MeshName의 RID 하나를 지정한다.
- channel send/request는 `(MeshName, ChannelName)`의 ready member 하나를 positive weight
  round-robin으로 선택한다.
- Spot Logical Multicast는 ChannelName의 remote MeshNode와 node-local Spot subscription을 대상으로 한다.
- Spot direct와 Actor direct는 address와 generation을 검증한 owner mailbox로 전달한다.

선택과 submit은 하나의 operation이다. application은 peer 목록이나 선택된 RID를 받아 별도 send를
반복하지 않는다.

## 4. Logical Multicast와 classic fanout

Spot Logical Multicast는 room, stage, zone처럼 위치가 바뀔 수 있는 logical Spot에 event를 전달한다.
송신 MeshNode는 target channel의 remote MeshNode마다 routed message를 한 번 보내고, 수신 MeshNode가
자기 node의 subscription을 검사한다. 같은 node에서 여러 Spot이 일치하면 immutable message storage의
reference를 공유해 각 Spot queue에 넣는다.

Logical Multicast의 기본 publish 정책은 `NoDrop = true`다. 모든 remote pipe와 local Spot queue가 message를
받을 수 있을 때 한 번에 commit한다. 한 대상이라도 backpressure 상태이면 send timeout까지 기다리고,
시간 안에 admission할 수 없으면 어느 대상에도 commit하지 않는다.

classic fanout은 연결되어 있고 subscription 준비가 끝난 subscriber에게 event를 보내는 독립 PUB/SUB
기능이다. MeshNode나 Spot이 필요하지 않은 host도 사용할 수 있으며 저장과 replay를 보장하지 않는다.

## 5. 실행 owner

Framework는 메시지를 실제 상태를 소유하는 실행 단위로 전달한다.

| owner | 책임 |
|---|---|
| Node | RID direct와 ChannelName handler, node에서 시작한 completion |
| Spot | Spot direct, Logical Multicast subscription, timer와 Spot 상태 |
| Actor | Actor direct message, Actor lifecycle과 Actor별 mailbox |
| STREAM session | 연결 lifecycle, packet dispatch와 Actor binding ingress |

Spot과 Actor message를 Node handler에서 다시 분배하도록 application에 요구하지 않는다. Core ready event는
작업 가능 상태만 알리고, Framework가 owner별 claim을 drain해 등록된 handler 실행 문맥으로 연결한다.

## 6. 연결 관리

자동 discovery는 location store의 descriptor와 lease로 같은 MeshName의 peer를 찾는다. 분산 discovery,
Spot·Actor location 또는 Actor transfer를 사용하는 host는 공식 Redis location store instance를
명시적으로 등록한다.

manual peer는 endpoint 또는 expected RID와 endpoint를 application이 제공하는 연결 intent다. manual peer도
자동 discovery peer와 같은 MeshName, RID, ChannelName, generation과 security admission을 통과한다.
manual이라는 이유로 message path나 handler 의미가 달라지지 않는다.

## 7. Framework가 숨기는 것

Framework는 transport 주소 선택, peer reconnect, multipart framing, packet codec, reply correlation과
backpressure queue를 내부에서 관리한다. application handler는 typed payload와 context를 사용하며 raw
socket 배선을 구성하지 않는다.

외부 edge gateway의 인증, quota, WAF, public API versioning과 billing은 이 framework의 계약 범위가 아니다.
