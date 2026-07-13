# Stream Connector 문서

ZLink STREAM 서버에 접속하는 **client 쪽 connector**의 문서 트리다. 서버 framework 문서와
분리한다. connector는 서버 framework package에 의존하지 않는다.

## 어떤 connector를 쓰는가

**언어가 아니라 "엔진 × 빌드 타깃"이 결정한다.**

| 대상 | 네이티브 빌드 | 웹 빌드(브라우저·WASM) |
|------|--------------|----------------------|
| Unity | [`.NET`](dotnet/guide/02-unity.ko.md) | [**TypeScript**](node/guide/01-overview.ko.md) |
| Godot | [C++](cpp/guide/09-engine-adapters.ko.md) 또는 [C#](dotnet/guide/03-godot-csharp.ko.md) | [**TypeScript**](node/guide/01-overview.ko.md) |
| Cocos | [C++(Axmol)](cpp/guide/09-engine-adapters.ko.md) | [**TypeScript**](node/guide/01-overview.ko.md) |
| Unreal | [C++](cpp/guide/09-engine-adapters.ko.md) | (해당 없음) |
| 브라우저 웹 client | — | [**TypeScript**](node/guide/01-overview.ko.md) |
| 데스크톱·서버 애플리케이션 | [`.NET`](dotnet/guide/INDEX.ko.md) · [C++](cpp/guide/INDEX.ko.md) · Java | — |
| Node(서버 E2E·도구·봇) | [TypeScript](node/guide/02-node.ko.md) | — |

**웹으로 빌드하는 순간 언어와 무관하게 TypeScript connector를 사용한다.** 브라우저 샌드박스에서
OS 소켓을 열 수 있는 언어가 없기 때문이다.

## 언어별 가이드

| 가이드 | 대상 |
|--------|------|
| [C++](cpp/guide/INDEX.ko.md) | Unreal, Godot(GDExtension), Axmol, 일반 C++, 서버 e2e/perf |
| [.NET](dotnet/guide/INDEX.ko.md) | Unity(네이티브), Godot C#, 데스크톱·서버 |
| [Node / TypeScript](node/guide/INDEX.ko.md) | 브라우저 계열(웹·Unity WebGL·Cocos web), Node |

**Java/Kotlin은 이 트리에 가이드를 두지 않는다.** 대상이 JVM 애플리케이션(서버 도구·E2E·봇)
하나뿐이라 엔진별로 갈라질 것이 없기 때문이다. 사용법은
[Java framework 가이드 07 — STREAM](../framework/java/guide/07-stream.ko.md)에 thin client로
포함되고, 계약은
[Java/Kotlin 공개 계약](../framework/common/spec/languages/java/stream-connector.ko.md)이 소유한다.

## 계약 문서

| 문서 | 소유 범위 |
|------|-----------|
| [Stream Connector 공통 스펙](../framework/common/spec/stream-connector.ko.md) | **정본** — 대상 실행 환경, transport, wire 계약, 연결 생명주기, 오류 의미, 배포 산출물 |
| [`.NET` 공개 계약](../framework/common/spec/languages/dotnet/stream-connector.ko.md) | `.NET` public 타입과 시그니처 |
| [Java/Kotlin 공개 계약](../framework/common/spec/languages/java/stream-connector.ko.md) | Java/Kotlin public 타입과 시그니처 |
| [Node 공개 계약](../framework/common/spec/languages/node/stream-connector.ko.md) | TypeScript public 타입과 시그니처, entrypoint 분리 |

가이드와 계약이 어긋나면 **계약이 기준이다**. 차이는
[구현 차이](../framework/common/spec/implementation-gap.ko.md)에 기록하고 구현이 계약을 따르게 한다.

## 알려진 구현 차이

TypeScript browser entrypoint의 WebSocket transport는 구현되어 있다. 남은 차이는 handler의
`await` continuation에 flow를 보존하면서 관련 없는 callback의 flow 누출을 막는
MFLOW-EXT-014다. 현재 상태는
[implementation gap §4.10](../framework/common/spec/implementation-gap.ko.md)이 소유한다.
