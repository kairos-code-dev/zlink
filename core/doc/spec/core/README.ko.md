[English](README.md) | 한국어

[스펙 목차](../README.ko.md)

# ZLink Core 10.0.0 스펙

이 목차는 `zlink.h`가 제공하는 Core 10.0.0 공개 C ABI 계약을 연결한다. 정식 API 문서는 공개 계약만
설명하며 source directory, socket 배선과 queue 구조는 설명하지 않는다.

## 1. 공통 계약

| 문서 | 내용 |
|---|---|
| [공개 계약 관리](00-public-contract-governance.ko.md) | spec·header·test·package 일치 절차 |
| [Context](01-context.ko.md) | Context 생성, 종료와 설정 |
| [Message](02-message.ko.md) | message lifecycle, routing ID와 ownership |
| [Errors](03-errors.ko.md) | 공개 result enum, errno와 version |
| [Errno map](04-errno-map.ko.md) | API family별 result·errno 대응 |
| [Events](05-events.ko.md) | 공통 event type과 readiness 의미 |
| [Polling](06-polling.ko.md) | poll item, poller와 source 지원표 |
| [Monitoring](07-monitoring.ko.md) | socket·MeshNode monitor와 status snapshot |
| [Utilities](08-utilities.ko.md) | timer, thread, stopwatch와 atomic helper |

## 2. Socket 계약

| 문서 | 내용 |
|---|---|
| [Socket 목차](socket/README.ko.md) | 공통 lifecycle, option, send와 receive |
| [PAIR](socket/01-pair.ko.md) | 1:1 연결 |
| [PUB](socket/02-pub.ko.md) | classic fanout publisher |
| [SUB](socket/03-sub.ko.md) | classic fanout subscriber |
| [XPUB](socket/04-xpub.ko.md) | subscription-aware publisher |
| [XSUB](socket/05-xsub.ko.md) | upstream subscription socket |
| [DEALER](socket/06-dealer.ko.md) | asynchronous request source |
| [ROUTER](socket/07-router.ko.md) | routing-ID 기반 raw router |
| [STREAM](socket/08-stream.ko.md) | raw TCP/WS session socket |

## 3. Service 계약

| 문서 | 내용 |
|---|---|
| [Service 목차](service/README.ko.md) | service 공통 경계와 문서 지도 |
| [MeshNode](service/01-mesh-node.ko.md) | RouteMesh membership, peer와 node/channel messaging |
| [Dispatch](service/02-dispatch.ko.md) | ready, claim, receive batch와 reply token |
| [Spot](service/03-spot.ko.md) | Spot direct messaging과 Logical Multicast |
| [Actor](service/04-actor.ko.md) | ActorRef, mailbox, Spot membership과 transfer |
| [STREAM session](service/05-stream-session.ko.md) | session–Actor binding과 transfer barrier |
