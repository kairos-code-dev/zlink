# gRPC 대안 — ZLink을 어디에 쓰나

이 장은 Node/NestJS 서비스에서 ZLink framework 를 gRPC/HTTP 대신 언제 쓰는지 판단
기준을 정리한다. ZLink 는 범용 RPC 문법 대체가 아니라 zlink core 의 channel, Spot,
STREAM 기능을 NestJS 표면으로 올리는 계층이다.

## 1. ZLink가 맞는 경우

- 서버 간 호출이 많고 endpoint 배선을 application 에서 숨기고 싶다.
- request/send/pub-sub/room/stream 을 같은 runtime 안에서 다루고 싶다.
- game room, stage, session actor 처럼 동적 routing 단위가 필요하다.
- 외부 client STREAM 과 server actor 를 연결해야 한다.
- `.NET`, Java/Kotlin, C++, Node 등 여러 언어가 같은 channel/packet 으로 통신해야 한다.

## 2. gRPC가 더 단순한 경우

- 정적인 service-to-service API 만 필요하다.
- load balancer 와 service discovery 를 이미 표준화했다.
- 동적 Spot, actor/session relay, external stream connector 가 필요 없다.

## 3. 판단 기준

단순 CRUD RPC 만 필요하면 gRPC 가 더 작고 익숙할 수 있다. 동적 room/session routing 과
실시간 상태 서버가 핵심이면 ZLink 가 더 적합하다. 도메인 일관성·영속성은 그대로
application 책임으로 남고, ZLink 는 서비스 간 통신 배선을 줄인다.

## 4. 정본 sample

실제 업무 흐름과 완료 기준은
[공통 샘플 시나리오](../../common/sample/README.ko.md)가 정의한다. 언어별 문서에서
같은 샘플 계약을 다시 정의하지 않는다.

cross-language wire 계약 smoke 기준은
[regression test matrix](../internals/regression-test-matrix.ko.md#81-sample--guide--cross-language-release-항목)에 있다.

## 회귀 테스트

가이드 장 존재와 README 링크는 `test/contract/documentation-regression.test.js` 에서 확인한다.
