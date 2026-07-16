[English](05-events.md) | 한국어

[스펙 목차](../README.ko.md) · [코어 목차](README.ko.md) · [Monitoring](07-monitoring.ko.md) · [Polling](06-polling.ko.md) · [Dispatch](service/02-dispatch.ko.md)

# Event와 readiness 카탈로그

이 문서는 ZLink Core 10.0.0의 공개 event family와 readiness 의미를 정리한다. 대상 독자는 monitor,
poller와 service dispatch를 bindings에 투영하는 개발자다. 각 event의 구조체와 API 계약은 연결된 owner
문서가 소유하며 이 문서는 family 간 경계를 정의한다.

## 1. Event family

| Family | Source | 전달 API | 의미 |
|---|---|---|---|
| socket monitor | raw socket monitor handle | handler 또는 recv | bind, connect, handshake, disconnect, protocol과 close |
| MeshNode monitor | MeshNode monitor handle | handler 또는 recv | lifecycle, peer, multicast, backpressure, operation과 claim 상태 |
| poller readiness | socket, FD, timer, MeshNode | `zlink_poll`, poller wait | 지금 drain 또는 submit retry를 수행할 가치가 있음 |
| service ready | MeshNode ready index | ready handler 또는 `POLLIN` 뒤 ready batch | application/infrastructure owner claim을 가져올 수 있음 |
| timer fire | timer handle | handler 또는 timer recv | 누적 fire count가 있음 |

monitor event는 관측 기록이고 readiness는 현재 work 존재 가능성을 알리는 level-triggered 상태다. readiness
하나가 application message 하나와 일대일로 대응한다고 가정하지 않는다.

## 2. Raw socket lifecycle

raw socket monitor는 endpoint bind/listen, outgoing connect, accept, handshake success/failure, disconnect,
protocol error와 close를 기록한다. disconnect reason은 transport error, handshake failure, Context 종료와
unknown으로 구분한다. raw event는 MeshName, ChannelName 또는 service owner를 포함하지 않는다.

## 3. MeshNode lifecycle과 peer

MeshNode monitor는 다음 상태 전이를 기록한다.

```text
CREATED -> STARTED -> PARTIAL_READY <-> READY -> DRAINING -> STOPPED
                         |               |
                         +---- ERROR <---+
```

peer event는 RID와 lifecycle generation을 함께 사용한다. endpoint 문자열만으로 peer identity를 정하지
않는다. admission 거절은 MeshName, expected RID, generation과 trust failure를 result·errno로 구분한다.

## 4. Logical Multicast와 backpressure

Logical Multicast event는 publish 하나의 snapshot, admitted와 dropped target aggregate를 기록한다. local
Spot match 수와 remote target 수를 구분하며 topic과 payload는 monitor에 포함하지 않는다. 기본 NODROP
성공은 dropped target이 0이다.

backpressure event는 submit이 queue 또는 reservation capacity 때문에 진행되지 않았음을 뜻한다. send-ready와
`POLLOUT`은 이후 retry할 가치가 생겼다는 뜻이며 다음 submit의 성공을 보장하지 않는다.

## 5. Service ready와 claim

MeshNode ready callback은 readable domain mask만 통지하고 payload를 전달하지 않는다. ready batch의 각
record는 application 또는 infrastructure domain 하나와 claim 하나를 소유한다. 같은 owner의 두 domain은
서로 다른 ready record이므로 application turn과 completion 진행을 분리할 수 있다.

claim release 뒤 mailbox에 work가 남으면 Core가 ready index를 다시 설정한다. callback 또는 poller가
ready를 관측한 직후 다른 consumer가 drain할 수 없으므로, single-consumer receive mode를 지키면 lost
wakeup 없이 level-triggered 의미가 유지된다.

## 6. Ordering과 overflow

같은 source queue에서는 Core가 event를 commit한 순서를 보존한다. 서로 다른 peer I/O thread, raw socket과
MeshNode monitor 사이의 전역 wall-clock order는 제공하지 않는다.

monitor queue overflow는 status counter에 반영하고 lifecycle·peer·protocol event를 우선 보존한다. service
ready index는 payload를 drop하지 않으며 consumer가 claim을 release할 때 남은 work를 다시 통지한다.
