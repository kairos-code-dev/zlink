# 01 — 개요

[← 목차](INDEX.ko.md) | [다음: Node →](02-node.ko.md)

---

TypeScript Stream Connector는 ZLink STREAM 서버에 연결하는 client-side library다.
**두 종류의 런타임**을 담당한다.

## 대상 실행 환경

| 런타임 | 대상 | 상태 |
|--------|------|------|
| **브라우저 계열** | 브라우저 웹 client, **Unity WebGL**, **Cocos Creator web**, Godot Web | WebSocket 전송 구현, 비동기 flow 문맥 gap 있음 |
| **Node** | 서버 E2E 테스트, 도구, 봇 | 사용 가능 |

**웹(브라우저·WASM)으로 빌드하는 모든 엔진이 언어와 무관하게 이 connector를 사용한다.**
브라우저 샌드박스에서 OS 소켓을 열 수 있는 언어가 없기 때문이다. Unity를 C#으로 만들었든
Cocos를 C++로 만들었든, **웹으로 빌드하는 순간 JavaScript 런타임 위에 올라간다.**

| 엔진 | 네이티브 빌드 | 웹 빌드 |
|------|--------------|---------|
| Unity | [`.NET` connector](../../dotnet/guide/02-unity.ko.md) | **이 connector** — C#이 jslib interop으로 JS 계층을 호출한다 |
| Godot | [C++](../../cpp/guide/09-engine-adapters.ko.md) 또는 [C#](../../dotnet/guide/03-godot-csharp.ko.md) | **이 connector** |
| Cocos | [C++(Axmol)](../../cpp/guide/09-engine-adapters.ko.md) | **이 connector**(Cocos Creator web) |
| 브라우저 웹 client | — | **이 connector** |

## 브라우저 계열의 현재 상태

`@zlink-systems/stream-connector/browser`는 플랫폼의 네이티브 `WebSocket`으로 `ws`와 `wss`에
연결한다. browser bundle에는 Node의 `net`, `tls`, `async_hooks`, `crypto`와 `Buffer`가 포함되지
않는다. `tcp`와 `tls` endpoint는 연결을 시작하기 전에 `ConfigurationError`로 거부한다.

현재 남은 차이는 message flow 실행 문맥이다. Node 진입점은 `AsyncLocalStorage`로 handler의
`await` continuation에 flow를 보존하고 callback 종료 뒤 문맥을 정리한다. 브라우저 표준에는 같은
기능이 없다. 현재 browser 구현은 handler가 기다리는 동안 같은 connector를 사용하는 관련 없는
callback에 inbound flow가 노출될 수 있으므로, 공통 MFLOW-EXT-014를 충족했다고 표시하지 않는다.

**계약은 이미 고정되어 있다.**

- [공통 스펙 §2 — 대상 실행 환경](../../../framework/common/spec/32-stream-connector.ko.md)
- [공통 스펙 §3.2 — 환경별 transport 가용성](../../../framework/common/spec/32-stream-connector.ko.md)
- [Node 공개 계약 §2 — entrypoint 분리](../../../framework/common/spec/languages/node/03-stream-connector.ko.md)

transport 구현과 현재 차이의 근거는
[Node 공개 계약 §7](../../../framework/common/spec/languages/node/03-stream-connector.ko.md)과
[implementation gap §4.10](../../../framework/common/spec/90-implementation-gap.ko.md)이 소유한다.

## entrypoint 분리

두 런타임의 제약이 다르므로 패키지는 entrypoint를 분리한다. 런타임 분기가 아니라 **빌드 시점
분리**다. 브라우저 번들에 Node 소켓 모듈이 섞여 들어가면 안 된다.

| entrypoint | 대상 | 기본 transport |
|------------|------|----------------|
| `@zlink-systems/stream-connector` | Node | `net`·`tls` 기반 |
| `@zlink-systems/stream-connector/browser` | 브라우저 계열 | 플랫폼 `WebSocket` 기반 |

두 entrypoint는 **같은 public 타입과 시그니처**를 노출한다. 다른 것은 기본 transport factory
하나뿐이다.

## transport 지원

**사용 가능한 transport는 entrypoint가 결정한다.**

| entrypoint | `tcp` | `tls` | `ws` | `wss` |
|------------|-------|-------|------|-------|
| Node | ✅ | ✅ | ✅ | ✅ |
| **브라우저** | ❌ | ❌ | ✅ | ✅ |

**브라우저는 OS 소켓을 열 수 없다.** `tcp://`·`tls://` endpoint를 주면 연결을 시도하지 않고
구성 오류(`ConfigurationError`)로 즉시 실패한다. 이는 구현 제약이 아니라 플랫폼 제약이다.

브라우저에서 `ws`·`wss`는 **플랫폼의 네이티브 `WebSocket`** 으로 동작한다. 핸드셰이크와
프레이밍을 브라우저가 수행하므로 connector가 직접 구현하지 않는다.

## 서버 framework와의 관계

connector는 STREAM 서버에 연결하는 client library다. 서버 framework package와 상호 의존하지
않는다. 양쪽은 STREAM header/payload wire 계약만 공유한다.

wire 계약의 정본은
[Stream Connector 공통 스펙](../../../framework/common/spec/32-stream-connector.ko.md)이다.
