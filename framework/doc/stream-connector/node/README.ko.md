# Node / TypeScript Stream Connector

TypeScript STREAM client connector(`@zlink-systems/stream-connector`)의 문서 진입점이다.
**브라우저 계열**(웹 client, Unity WebGL, Cocos web, Godot Web)과 **Node**(서버 E2E·도구·봇)가
대상이다.

| 문서 | 내용 |
|------|------|
| [가이드 INDEX](guide/INDEX.ko.md) | 개요, Node와 브라우저 사용법 |
| [Node 공개 계약](../../framework/common/spec/languages/node/03-stream-connector.ko.md) | public 타입, entrypoint 분리 |
| [Stream Connector 공통 스펙](../../framework/common/spec/32-stream-connector.ko.md) | **정본** — 대상 환경, transport, wire 계약 |

브라우저 진입점은 플랫폼 `WebSocket`으로 `ws`·`wss` 연결을 제공한다. 다만 브라우저에는
비동기 실행 문맥을 격리하는 표준 기능이 없어서, handler의 `await` continuation에 flow를
보존하면서 관련 없는 callback의 flow 누출을 막는 계약은 아직 충족하지 못한다. 현재 차이는
[implementation gap §4.10](../../framework/common/spec/90-implementation-gap.ko.md)에 기록한다.
