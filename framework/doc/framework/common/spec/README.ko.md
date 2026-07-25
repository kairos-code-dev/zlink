# Framework 공통 스펙

이 디렉터리의 문서는 Framework의 공통 공개 계약을 설명한다. 각 문서는 구현과
contract test에 필요한 입력, 상태, 정상 흐름, 실패와 완료 조건을 자체적으로
정의한다.

## 작성 기준과 공통 용어

- [스펙 작성 가이드](00-spec-writing-guide.ko.md)
- [Framework 메시징 용어집](01-glossary.ko.md)

## 기반 계약

- [00 공개 계약 관리](00-public-contract-governance.ko.md)
- [01 Framework 개요](01-overview.ko.md)
- [02 상호작용 모델](02-interaction-model.ko.md)
- [03 메시지 모델](03-message-model.ko.md)
- [04 비동기 실행 정책](04-async-execution-policy.ko.md)
- [05 Framework API](05-framework-api.ko.md)

`90-implementation-gap.ko.md`는 공개 계약이 아니다. 언어별 구현과 목표 계약의
차이를 기록하는 진행 문서이며, 현재 구현을 공통 계약으로 바꾸는 근거로 사용하지
않는다.

- [90 언어별 구현 차이](90-implementation-gap.ko.md)

## Channel과 network

- [10 RouteMesh topology](10-channel-topology.ko.md)
- [11 Channel 메시징](11-channel-messaging.ko.md)
- [12 ClientServer Channel](12-client-server-channel.ko.md)
- [13 Network listener identity](13-network-listener-identity.ko.md)

## Object 메시징

- [19 Spot 모델](19-spot-model.ko.md)
- [20 Spot 메시징](20-spot-messaging.ko.md)
- [21 MeshNode](21-mesh-node.ko.md)
- [22 Actor 모델](22-actor-model.ko.md)
- [23 Spot과 Actor membership](23-spot-actor.ko.md)
- [24 Spot 주소 메시징](24-spot-address-messaging.ko.md)
- [25 Stage wrapper on Spot](25-stage-wrapper-on-spot.ko.md)
- [26 Spot·Actor routing](26-object-routing.ko.md)

## STREAM과 session

- [30 STREAM 서버 session](30-stream-session.ko.md)
- [31 Session Actor dispatch](31-session-actor-dispatch.ko.md)

## Location Store와 relocation

- [40 Location runtime](40-location-runtime.ko.md)
- [41 Redis Location Store](41-location-store-redis.ko.md)
- [42 Redis Relocation Store](42-relocation-store-redis.ko.md)

## 관측과 종료

- [50 Runtime monitoring](50-runtime-monitoring.ko.md)
- [51 Runtime metrics](51-runtime-metrics.ko.md)
- [52 Message flow tracing](52-message-flow-tracing.ko.md)
- [53 Flow correlation](53-flow-correlation.ko.md)
- [54 Host Retire, Shutdown과 handoff](54-graceful-drain-handoff.ko.md)
- [55 Transport liveness](55-transport-liveness.ko.md)

## Server 언어별 exact interface

공통 server 계약이 각 언어에서 사용하는 정확한 public type, signature와 비동기
표현은 다음 문서가 소유한다.

- [C++](server/languages/cpp/README.ko.md)
- [.NET](server/languages/dotnet/README.ko.md)
- [Java](server/languages/java/README.ko.md)
- [Kotlin](server/languages/kotlin/README.ko.md)
- [Node.js](server/languages/node/README.ko.md)

## HTTP client

- [HTTP client 스펙 목차](http-client/README.ko.md)
- [12 HTTP client 통합 계약](http-client/12-http-client.ko.md)
- [언어별 HTTP client 계약](http-client/language-interfaces.ko.md)

`10-revision-candidates.ko.md`는 공개 계약이 아니라 다음 revision의 설계 후보를
관리하는 문서다.

## Stream connector

- [32 Stream connector](stream-connector/32-stream-connector.ko.md)
- [언어별 Stream connector 계약](stream-connector/README.ko.md#언어별-public-api)
