# Stream Connector — 대상 환경 계약 신설과 브라우저 결함 수정 (초안)

> **구현 상태(2026-07-13):** 이 초안이 제안한 브라우저 진입점과 transport 분리를 Node.js
> package에 반영했다. 아래의 구현 전 분석과 표는 당시 상태를 기록한 것이며 현재 상태표로
> 사용하지 않는다. 정식 계약은
> [Node Stream Connector 공개 계약](../spec/languages/node/03-stream-connector.ko.md), 남은 비동기
> flow 문맥 차이는 [implementation gap §4.10](../spec/90-implementation-gap.ko.md)이 소유한다.

> 이 문서는 구현 전에 작성한 설계 초안 기록이며 현재 공개 계약이 아니다. 두 가지를 함께 다룬다.
>
> 1. **connector 공통(언어 중립) spec을 신설**한다 — 지금은 없다(§2.2).
> 2. 그 계약이 없어서 생긴 **TypeScript connector의 브라우저 결함**을 수정한다.
>
> [공개 계약 관리 절차](../spec/00-public-contract-governance.ko.md)에 따라 공통 스펙 →
> 언어별 스펙 → 구현 순서로 진행한다.

## 0. 결정 사항 — 담당 경계는 "언어"가 아니라 "엔진 × 빌드 타깃"이다

**모든 엔진을 지원한다.** 다만 한 엔진이라도 **빌드 타깃(네이티브 / 웹)에 따라 connector가
갈린다.** 이것이 이 결함의 핵심 교훈이다 — 언어만 보고 connector를 고르면 웹 빌드에서
실패한다.

| 엔진 | 네이티브 빌드 | 웹 빌드 |
|---|---|---|
| **Unity** | **`.NET` connector** — 기본 connector를 그대로 사용하고 dispatch mode `Manual` + main thread에서 `Dispatch.Async()` | **TypeScript connector** — IL2CPP/WASM은 `System.Net.Sockets`를 사용할 수 없다. C#이 jslib interop으로 JS 계층을 호출한다 |
| **Godot** | **C++ connector**(GDExtension) 또는 **`.NET` connector**(Godot C#) — 둘 다 지원한다 | **TypeScript connector** — Godot Web export |
| **Cocos** | **C++ connector**(Axmol 어댑터) | **TypeScript connector** — Cocos Creator(web) |
| **Unreal** | **C++ connector**(Unreal plugin) | (해당 없음) |
| 브라우저 웹 client | (해당 없음) | **TypeScript connector** |
| 데스크톱·서버 | `.NET` / Java / C++ | — |
| Node(E2E·도구·봇) | **TypeScript connector** | — |

**규칙 하나로 요약하면:** 웹(브라우저·WASM)으로 빌드하는 순간, 언어가 무엇이든
**TypeScript connector**를 쓴다. 브라우저 샌드박스에서 OS 소켓을 열 수 있는 언어가 없기
때문이다.

이 표를 공통 spec에 고정해야 같은 결함이 반복되지 않는다. **현재 웹 열(column)이 전부
동작하지 않는다** — 이 문서가 그 수정을 다룬다.

## 1. 결함 요지

**TypeScript stream connector(`@zlink-systems/stream-connector`)는 브라우저에서 동작하지
않는다.** `ws://`·`wss://` transport를 지원한다고 되어 있지만, 그 구현이 Node의 생 TCP
소켓 위에 올려져 있어 브라우저에서는 실행되지 않는다.

이 connector의 실제 용도는 **브라우저 기반 client**(웹 client, Cocos Creator web, Unity
WebGL의 JS 계층)와 **server E2E 테스트**다. 그중 브라우저 계열이 전부 동작하지 않는다.

WS/WSS transport가 존재하는 이유가 브라우저인데 브라우저에서 동작하지 않으므로, 이는
설계 의도와 구현이 어긋난 **결함**이다.

## 2. 근본 원인

### 2.1 spec이 대상 환경을 정의하지 않았다

[node stream connector spec](../spec/languages/node/03-stream-connector.ko.md)은 대상 실행
환경을 한 번도 언급하지 않는다. 문서는 "TCP, request/reply, dispatch, typed payload
API"만 규정한다. **브라우저·Unity Web·Cocos Web·E2E 테스트라는 용도가 spec에 없다.**

그래서 구현자에게 보이는 요구는 다음 두 가지뿐이었다.

- Node client용 connector를 만든다.
- TCP/TLS/WS/WSS를 지원한다.

이 요구만 보면 **Node에서 `ws://`를 지원하는 가장 자연스러운 방법이 TCP 위에 WebSocket을
직접 구현하는 것**이다(Node에는 오랫동안 WebSocket client가 없었고, 외부 라이브러리를
쓰지 않으면 직접 구현해야 한다). 구현은 spec을 따랐고, **spec이 요구를 빠뜨렸다.**

이 저장소는 [spec이 계약의 기준](../spec/00-public-contract-governance.ko.md)이라는 원칙을
쓴다. 그 원칙에 따르면 **결함의 1차 위치는 spec**이다.

대상 환경을 명시하는 선례는 이미 있다 — C++ connector는 `connector/engines/`에 `axmol`,
`godot`, `unreal` 어댑터를 두어 대상을 드러낸다. TypeScript connector에는 그에 해당하는
정의가 없다.

### 2.2 connector에는 공통(언어 중립) spec 자체가 없다

더 근본적인 문제다. connector spec은 **언어별 문서만 존재하고 그 위의 공통 계약이 없다.**

| 문서 | 줄 수 | 상태 |
|---|---:|---|
| `spec/languages/dotnet/stream-connector.ko.md` | 239 | 있음 |
| `spec/languages/java/stream-connector.ko.md` | 453 | 있음 |
| `spec/languages/node/stream-connector.ko.md` | 72 | 있음 |
| **`spec/stream-connector.ko.md`(언어 중립)** | — | **없음** |

[공개 계약 관리](../spec/00-public-contract-governance.ko.md)는 **"공통 스펙이 언어와 무관한
기능·동작·오류·완료 조건을 소유하고, 언어별 스펙은 그 의미를 각 언어의 정확한 시그니처로
고정한다"** 고 규정한다. connector에는 그 **상위 층이 통째로 비어 있다.**

**대상 실행 환경은 언어 중립 요구다.** 어느 언어 spec에도 속하지 않는 상위 계약이므로,
공통 spec이 없으면 적을 자리가 없다. 그래서:

- node connector spec(72줄)에는 **transport 절조차 없다.**
- dotnet connector spec에는 transport 표는 있으나 **실행 환경 서술이 없다** — Unity WebGL에서
  `System.Net.Sockets`를 쓸 수 없다는 제약을 기록할 자리가 없다.
- 언어별 connector가 **어느 환경을 담당하는지**(§0의 경계)를 정할 자리가 없다.

즉 이 결함은 한 언어의 실수가 아니라 **계약 구조의 공백**에서 나왔다. 그래서 이 문서는
TypeScript 구현 수정만이 아니라 **공통 spec 신설**을 함께 제안한다.

## 3. 구현이 Node에 묶인 지점

### 3.1 기본 transport가 정적으로 배선되어 있다

```ts
// Runtime/ZlinkStreamConnectorOptions.ts:11
import { inferTransport, NodeStreamTransportFactory } from './Transport/ZlinkStreamTransportFactory';

// Runtime/ZlinkStreamConnectorOptions.ts:63
transportFactory: options.transportFactory ?? new NodeStreamTransportFactory(),
```

`NodeStreamTransportFactory` → `NodeSocketConnector` → `node:net` · `node:tls`로 이어진다.
**패키지를 import하면 이 체인이 모듈 그래프에 들어온다.** 사용자가 브라우저 transport를
주입해도, 기본값 표현식 안에 있어 번들러가 `node:net`을 제거하지 못한다. 결과적으로
브라우저 번들이 실패한다.

### 3.2 WS 구현이 생 TCP 소켓 위에 있다

```ts
// Runtime/Transport/NodeSocketConnector.ts — ws:// 경로
const socket = await connectSocket(..., (port, host) => net.connect({ port, host }), 80);
const leftover = await completeWebSocketHandshake(socket, endpoint, ...);
return new NodeWebSocketConnection(socket, ...);
```

- `net.connect()` — 생 TCP 소켓. **브라우저에는 이 API가 없다**(보안 샌드박스).
- `WebSocketHandshake.ts` — `GET ... HTTP/1.1` + `Sec-WebSocket-Key`를 직접 조립해 전송한다.
- `WebSocketFrameCodec.ts` — opcode·mask·payload length를 직접 비트 연산으로 처리한다.

브라우저에서는 이 세 가지가 모두 불필요하고, **애초에 첫 단계(TCP)가 불가능**하다.

### 3.3 package export에 브라우저 분기가 없다

```json
"exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" } }
```

`browser` 조건이 없어 환경별 구현을 나눌 통로가 없다.

## 4. 설계는 이미 교체 가능하게 되어 있다

결함이 구조 전체에 걸친 것은 아니다. 교체 지점과 platform-neutral 계층이 이미 존재한다.

| 계층 | 현재 상태 | 브라우저에서 |
|---|---|---|
| `stream-wire`(ZLink 프레이밍·헤더 codec) | `Uint8Array` 기반, `Buffer` 미사용 | ✅ **그대로 재사용** |
| Runtime(dispatcher, pending request, inbound observer) | transport에 의존하지 않는다 | ✅ **그대로 재사용** |
| `ZlinkStreamTransportFactory` | 주입 가능한 interface로 이미 정의됨(`Contracts/ZlinkStreamConnectorOptions.ts:12`) | ✅ **교체 지점 존재** |
| 기본 transport 배선 | Node 구현을 정적 import | ❌ **수정 대상** |
| package export | 브라우저 entrypoint 없음 | ❌ **수정 대상** |

즉 "교체 가능한 transport"라는 설계 의도는 있었으나 **기본값 배선이 그 의도를 무효화**했다.
수정은 재구현이 아니라 **분리**다.

## 5. 공통 spec에 신설할 내용

### 5.1 대상 환경과 담당 connector

§0의 표를 계약으로 고정한다. 핵심은 **엔진 × 빌드 타깃**으로 결정된다는 것이다.

**환경 제약이 계약을 만든다.** 각 connector가 그 환경의 제약에 맞춰 설계된 근거다. 이것이
공통 spec이 대상 환경을 먼저 정의해야 하는 이유다.

| 환경 | 제약 | 계약에 미치는 영향 |
|---|---|---|
| 게임 엔진(공통) | 엔진 객체를 main thread 밖에서 다룰 수 없다 | dispatch mode **`Manual`** 기본 + main thread에서 명시 pump(`Dispatch.Async()`) |
| 게임 엔진(C++) | 예외·coroutine이 비활성인 경우가 있다 | C++ core를 **no-exception·no-coroutine**으로 제공. public header가 `<coroutine>`을 노출하지 않는다 |
| **브라우저·WASM** | **OS 소켓을 열 수 없다**(보안 샌드박스) | **`tcp`·`tls` 사용 불가.** `ws`·`wss`만. 네이티브 `WebSocket` API 사용 |
| Node | `net`·`tls` 사용 가능 | 4개 transport 전부 |

이미 두 가지는 구현이 이 원칙을 따르고 있다 — C++ core의 no-exception 설계, `.NET`
connector의 `Dispatch.Manual` 기본값(Unity main thread 제약). **브라우저 제약만 반영되지
않았다.**

### 5.2 실행 환경별 transport 가용성

| 환경 | 사용 가능한 transport | 근거 |
|---|---|---|
| **브라우저 계열**(웹, Cocos web, Unity WebGL) | `ws`, `wss` | 네이티브 `WebSocket` API. **생 TCP 소켓을 열 수 없다**(보안 샌드박스) |
| **Node** | `tcp`, `tls`, `ws`, `wss` | `net`·`tls` 모듈 사용 가능 |
| **네이티브**(.NET·C++·Java) | `tcp`, `tls`, `ws`, `wss` | OS 소켓 사용 가능 |

**브라우저 계열은 `tcp`·`tls`를 사용할 수 없다.** 구현 제약이 아니라 플랫폼 제약이다.
브라우저 entrypoint가 `tcp://`·`tls://` endpoint를 받으면 **구성 오류로 즉시 실패**한다
(런타임에 조용히 실패하지 않는다).

### 5.3 배포 계획 — 타입별 산출물

공통 spec은 **각 대상 타입이 어떤 산출물로 배포되는지**도 소유한다. 배포 형식이 곧 그
환경의 제약을 반영하기 때문이다(예: 브라우저는 npm + ESM 번들, Unreal은 source plugin).
C++ connector guide가 이미 이 표를 갖고 있으며, 그 형식을 전 언어로 넓힌다.

| 대상 | 산출물 | 패키지 id | 배포 채널 | 상태 |
|---|---|---|---|---|
| **일반 C++ client** | `zlink-stream-connector` | `zlink::stream_connector` | CMake · vcpkg · Conan | ✅ 있음 |
| **서버 e2e/perf** (C++) | `zlink-stream-e2e-client` | `zlink::stream_e2e_client` | CMake · vcpkg · Conan | ✅ 있음 |
| **Unreal** | `zlink-unreal-stream-connector` | Unreal plugin module | source plugin | ✅ 있음 |
| **Godot(C++)** | `zlink-godot-stream-connector` | GDExtension | source GDExtension | ✅ 있음 |
| **Cocos/Axmol** | `zlink-axmol-connector` | CMake target | source package | ✅ 있음 |
| **`.NET`**(데스크톱·서버) | `Systems.Zlink.Stream.Connector` | 동명 NuGet | NuGet | ✅ 있음 |
| **Unity(네이티브)** | 위 `.NET` 패키지를 그대로 사용 | — | NuGet 또는 UPM(검토) | ✅ 있음(별도 패키지 없음) |
| **Godot C#** | 위 `.NET` 패키지를 그대로 사용 | — | NuGet | ✅ 있음(문서 없음) |
| **Java** | `zlink-stream-connector` | Maven 좌표(확정 필요) | Maven | ✅ 있음 |
| **Node**(E2E·도구·봇) | `@zlink-systems/stream-connector` | 동명 npm | npm | ✅ 있음 |
| **브라우저 웹 client** | 위 npm 패키지의 **browser entrypoint** | 동명 npm(`exports.browser`) | npm | ❌ **없음** |
| **Cocos Creator(web)** | 위와 동일 | 동명 npm | npm | ❌ **없음** |
| **Unity WebGL** | 위 npm 패키지 + **jslib interop 어댑터** | 동명 npm + Unity 측 `.jslib` | npm + UPM(검토) | ❌ **없음** |
| **Godot Web export** | 위와 동일 | 동명 npm | npm | ❌ **없음** |
| (공통) wire 계층 | `@zlink-systems/stream-wire` | 동명 npm | npm | ✅ 있음 |

**배포 원칙:**

- **웹 계열은 하나의 npm 패키지로 통합한다.** 브라우저·Cocos web·Unity WebGL·Godot Web은
  전부 브라우저 런타임이므로, `@zlink-systems/stream-connector`의 **browser entrypoint 하나**를
  공유한다. 대상별로 npm 패키지를 늘리지 않는다.
- **Unity WebGL만 얇은 어댑터가 추가된다.** C#에서 JS를 호출해야 하므로 `.jslib` 파일과 C#
  바인딩이 필요하다. 이것은 connector 본체가 아니라 **Unity 측 어댑터**이므로, C++가
  `engines/`를 두는 것과 같은 자리에 둔다(배포 형태는 UPM 또는 source package — 결정 필요).
- **네이티브 엔진 어댑터는 source 배포다.** 엔진 빌드 시스템에 소스로 편입되는 것이 관례이고
  (Unreal plugin, GDExtension, Axmol CMake), C++ connector가 이미 그렇게 한다.
- **Unity(네이티브)와 Godot C#은 별도 패키지를 두지 않는다.** `.NET` connector를 그대로
  사용한다. 다만 **UPM 배포가 필요한지는 결정이 필요하다**(Unity는 NuGet을 직접 소비하기
  불편하다).

**미결정:**

| # | 항목 |
|---|---|
| 1 | Unity 배포 채널 — NuGet 직접 소비 vs UPM 패키지 제공(네이티브·WebGL 공통) |
| 2 | Unity WebGL 어댑터(`.jslib` + C# 바인딩)의 산출물 위치와 배포 형태 |
| 3 | Java connector의 Maven 좌표 확정 |

### 5.4 공통 spec이 소유할 그 밖의 계약

connector 공통 spec(`spec/stream-connector.ko.md`, 신설)이 소유할 언어 중립 계약이다.
현재 언어별 문서에 흩어져 있거나 누락된 것들이다.

- 대상 실행 환경과 담당 경계(§5.1)
- 환경별 transport 가용성과 위반 시 실패 계약(§5.2)
- **타입별 배포 계획**(§5.3) — 산출물·패키지 id·배포 채널
- endpoint scheme → transport 결정 규칙
- packet wire 계약(header·framing) — 서버 STREAM과 공유
- request/reply correlation, dispatch 모드, close reason 분류
- codec registry와 압축 계약
- inbound observer 의미

언어별 spec은 이 의미를 각 언어의 정확한 시그니처로 고정한다.

## 6. 수정 계획

### 6.1 spec·문서 정리 (선행)

**(a) 공통 spec 신설** — ✅ [`spec/stream-connector.ko.md`](../spec/32-stream-connector.ko.md) **완료**

언어 중립 계약이 `framework/doc/framework/dotnet/guide/samples/streaming-client.ko.md`
(약 1000줄)에 들어 있었다. **언어 중립 계약이 특정 언어의 샘플 폴더에 있어서** node·java
구현자가 참조할 정본이 사실상 없었다. (이 문서는 커밋 `530f9d2b6`에서 삭제되었고, git
히스토리에서 복원해 분해했다.)

| 현재 위치의 내용 | 이동할 곳 |
|---|---|
| wire 포맷(header·framing), packet 모델, transport, request correlation, 오류 kind, codec, 압축, inbound observer | → **공통 spec**(언어 중립화) |
| C# 타입·메서드 시그니처 | → `spec/languages/dotnet/stream-connector.ko.md` |
| 사용법·예제 | → `doc/stream-connector/dotnet/guide/`(신설) |

여기에 **지금 어디에도 없는 절**을 새로 쓴다.

- **대상 환경 × 담당 connector**(§0 표) — 엔진 × 빌드 타깃
- **환경 제약이 계약에 미치는 영향**(§5.1 표)
- **타입별 배포 계획**(§5.3 표) — 산출물·패키지 id·배포 채널
- 환경 위반 시 실패 계약 — 브라우저 entrypoint가 `tcp://`·`tls://`를 받으면 **구성 오류**

**(b) 엔진별 문서 정리**

엔진 지원은 **구현이 이미 있는데 문서 위치가 어긋나 있다.**

| 엔진 | 구현 | 문서 | 상태 |
|---|---|---|---|
| Unreal·Godot·Axmol (C++) | `cpp/connector/engines/` | `doc/stream-connector/cpp/guide/09-engine-adapters.ko.md` | ✅ 유지(모범 사례) |
| **Unity(네이티브)** (.NET) | 기본 connector + `Dispatch.Manual` | `doc/stream-connector/dotnet/guide/02-unity.ko.md` | ✅ **이전 완료** (`core/doc/guide/`에서 옮김 — core는 C 라이브러리 문서 트리다) |
| Godot C# (.NET) | 기본 connector | `doc/stream-connector/dotnet/guide/03-godot-csharp.ko.md` | ✅ **신설 완료** |
| 브라우저·Cocos web·Unity WebGL·Godot web (TS) | `/browser` 진입점과 네이티브 `WebSocket` adapter | `framework/doc/stream-connector/node/` | ✅ 구현 완료 |

C++ guide `01-overview`가 **대상 환경부터 정의하고 그 제약으로 설계를 이끄는** 형식을 이미
갖추고 있다. 다른 언어 guide는 이 형식을 따랐다.

**(c) 언어별 spec 보강** — ✅ node connector spec(72줄)을 공통 spec의 투영으로 다시 썼다.
대상 실행 환경(§1), entrypoint 분리(§2), 환경별 transport 가용성(§3)이 계약으로 들어갔다.

### 6.2 구현 수정

```text
packages/stream-connector/
  src/
    Contracts/            # 변경 없음 (transportFactory 주입 지점 유지)
    Runtime/
      Protocol/           # 변경 없음 (stream-wire 사용)
      ...                 # dispatcher, pending request 등 — 변경 없음
      ZlinkStreamConnectorOptions.ts   # 기본 transport 정적 import 제거
    Runtime/
      Transport/
        NodeSocketConnector              # Node net/tls transport
        NodeWebSocketConnection           # Node WebSocket framing
        BrowserWebSocketConnection        # Native WebSocket adapter
      ZlinkStreamConnectorNode.ts         # Node default wiring
      ZlinkStreamConnectorBrowser.ts      # Browser default wiring
    index.ts                              # Node entrypoint
    browser.ts                            # Browser entrypoint
```

`package.json`:

```json
"exports": {
  ".": {
    "types": "./dist/index.d.ts",
    "default": "./dist/index.js"
  },
  "./browser": {
    "types": "./dist/browser.d.ts",
    "default": "./dist/browser.js"
  }
}
```

브라우저 transport 어댑터는 짧다. 핸드셰이크와 프레이밍을 브라우저가 수행하므로
`WebSocketHandshake`·`WebSocketFrameCodec`이 필요 없다.

```ts
const ws = new WebSocket(endpoint);
ws.binaryType = 'arraybuffer';
ws.send(bytes);
ws.onmessage = e => ...;
```

### 6.3 공개 계약 영향

`ZlinkStreamConnectorOptions`·`ZlinkStreamTransportFactory`·`ZlinkStreamConnection` 등
**공개 타입은 바뀌지 않는다.** 바뀌는 것은 기본 transport의 배선과 package entrypoint다.
기존 Node 사용자는 영향받지 않는다.

## 7. 다른 언어 connector

§0의 경계 결정으로 **`.NET` connector는 Unity WebGL을 담당하지 않는다.** 따라서 `.NET`
connector에 `System.Net.Sockets` 관련 결함은 없다 — 네이티브 환경만 담당하므로 정상이다.

| connector | 담당 환경 | 결함 여부 |
|---|---|---|
| TypeScript | 브라우저·Cocos web·Unity WebGL·Node | transport 분리 완료, browser MFLOW-EXT-014 남음 |
| `.NET` | Unity(네이티브)·데스크톱·서버 | 없음(WebGL은 담당 밖) |
| C++ | Unreal·Godot·Axmol | 없음 |
| Java | JVM client | 없음 |

다만 **경계를 문서에 적어야 한다.** `.NET` connector spec에 "Unity WebGL은 담당하지 않으며
TypeScript connector를 사용한다"를 명시하지 않으면, 사용자가 WebGL 빌드에서 `.NET`
connector를 쓰려다 실패한다.

## 8. 회귀 테스트

| 테스트 | 확인 |
|---|---|
| `browser-bundle-check` | 실제 bundle과 module graph에 Node 전용 모듈과 `Buffer`가 **없다** — 통과 |
| `browser-transport-smoke` | 네이티브 `WebSocket` event 계약으로 `wss://` 연결·request/reply·push 수신이 동작한다 — 통과. headless 브라우저 도구는 현재 검증 환경에 없음 |
| `browser-rejects-tcp` | 브라우저 entrypoint에 `tcp://` endpoint를 주면 구성 오류로 실패한다 |
| 기존 Node connector 테스트 | 71개 통과(공개 계약 불변) |

## 9. ZoneWorld 샘플과의 관계

[ZoneWorld](../sample/zoneworld/README.ko.md)는 브라우저 client를 제공하는 첫 샘플이며,
transport 수정은 완료됐다. ZoneWorld는 browser MFLOW-EXT-014와 실제 브라우저 실행 증거가
완료되어야 구현할 수 있다. ZoneWorld §14가 현재 implementation gap을 참조한다.

이 결함은 ZoneWorld를 위해 새로 생긴 요구가 아니라, **connector가 원래 목표로 하던 용도
(브라우저·Cocos web·Unity WebGL)에 필요한 비동기 flow 격리가 완성되지 않은 기존 차이**다.
ZoneWorld는 그 차이를 드러낸 계기다.
