# Node / TypeScript Stream Connector 가이드

TypeScript STREAM client connector(`@zlink-systems/stream-connector`)의 공식 사용자 가이드다.
**브라우저 계열**(웹 client, Unity WebGL, Cocos web, Godot Web)과 **Node**(서버 E2E·도구·봇)가
대상이다.

브라우저 진입점은 플랫폼 `WebSocket`으로 동작한다. 현재 지원 범위와 비동기 flow 문맥의
남은 제한은 [01 — 개요](01-overview.ko.md)와 [03 — 브라우저](03-browser.ko.md)에서 설명한다.

## 목차

| 문서 | 내용 |
|------|------|
| [01 — 개요](01-overview.ko.md) | 대상 실행 환경, entrypoint 분리, 브라우저 계열의 현재 상태 |
| [02 — Node](02-node.ko.md) | 서버 E2E·도구·봇에서의 연결, send/request, dispatch |
| [03 — 브라우저](03-browser.ko.md) | browser entrypoint, WebSocket 연결, 현재 flow 제한 |

connector의 API 표면(옵션, codec, inbound observer, 오류)은
[Node 공개 계약](../../../framework/common/spec/languages/node/03-stream-connector.ko.md)이 소유한다.

## 다른 언어의 connector

| 문서 | 대상 |
|------|------|
| [C++ Stream Connector 가이드](../../cpp/guide/INDEX.ko.md) | Unreal, Godot(GDExtension), Axmol, 일반 C++ |
| [.NET Stream Connector 가이드](../../dotnet/guide/INDEX.ko.md) | Unity(네이티브), Godot C#, 데스크톱·서버 |

**웹(브라우저·WASM)으로 빌드하면 언어와 무관하게 이 TypeScript connector를 사용한다.**
