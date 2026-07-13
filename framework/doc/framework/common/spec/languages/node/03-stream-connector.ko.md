<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:end -->

# Node / TypeScript Stream Connector

> 이 문서는 [Stream Connector 공통 스펙](../../32-stream-connector.ko.md)의 **TypeScript 투영**이다.
> transport·wire·생명주기·오류 의미는 공통 스펙이 소유하고, 이 문서는 그 의미가
> TypeScript에서 갖는 **정확한 public 표면**을 고정한다.

TypeScript connector는 `@zlink-systems/stream-connector` 패키지로 제공하는 client connector다.
서버 framework와 별도 모듈이며 request/reply, dispatch(`Manual`/`Immediate`), typed payload API를
client code에서 사용하게 한다. JSON, MessagePack, Protobuf, custom codec은 connector options의
codec registry에 등록하고, typed `send`/`request`/`on`/`waitFor` 표면이 그 registry로 업무 DTO를
encode/decode한다.

## 1. 대상 실행 환경

**엔진 × 빌드 타깃별 담당 connector는 [공통 스펙 §2](../../32-stream-connector.ko.md)가 소유한다.**
그 배정에 따라 TypeScript connector가 담당하는 것은 **브라우저 계열**(웹 client, Unity WebGL,
Cocos Creator web, Godot Web)과 **Node**(서버 E2E·도구·봇)다.

**웹(브라우저·WASM)으로 빌드하는 모든 엔진이 언어와 무관하게 이 connector를 사용한다.** 이 배정이
TypeScript 표면에 남기는 결과가 **entrypoint 분리**(§2)다.

## 2. 진입점(entrypoint)

두 런타임의 제약이 다르므로 패키지는 **entrypoint를 분리**한다. 런타임 분기가 아니라 빌드 시점
분리다. 브라우저 번들에 Node 소켓 모듈이 섞여 들어가면 안 된다.

| entrypoint | 대상 | 기본 transport factory |
|---|---|---|
| `@zlink-systems/stream-connector` | Node | Node transport factory (`net`·`tls` 기반) |
| `@zlink-systems/stream-connector/browser` | 브라우저 계열 | Browser transport factory (플랫폼 `WebSocket` 기반) |

**계약:**

- 브라우저 entrypoint의 번들 그래프에는 **`net`·`tls`·`Buffer` 같은 Node 전용 모듈이 포함되지
  않는다.** 회귀 테스트로 고정한다(§8).
- 두 entrypoint는 **동일한 public 타입과 시그니처**를 노출한다. 다른 것은 기본 transport
  factory 하나뿐이다.
- 공통 wire 계층(`@zlink-systems/stream-wire`)은 두 런타임에서 **같은 코드**로 동작한다.
  `Uint8Array`만 사용하고 `Buffer`에 의존하지 않는다.

## 3. Transport

scheme → transport 매핑은 [공통 스펙 §3.1](../../32-stream-connector.ko.md)을 따른다.
**사용 가능한 transport는 entrypoint가 결정한다.**

| entrypoint | 사용 가능한 transport |
|---|---|
| Node | `tcp`, `tls`, `ws`, `wss` |
| **브라우저** | **`ws`, `wss`만** |

**브라우저 entrypoint가 `tcp://`·`tls://` endpoint를 받으면 `ZlinkStreamErrorCode.ConfigurationError`로
즉시 실패한다.** 연결을 시도하다 런타임에 조용히 실패하지 않는다.

브라우저에서 `ws`·`wss`는 **플랫폼의 네이티브 `WebSocket`** 으로 구현한다. 핸드셰이크와 프레이밍을
브라우저가 수행하므로 connector가 직접 구현하지 않는다.

### 3.1 transport factory 주입

`transportFactory` option은 두 entrypoint에서 모두 열려 있다. 테스트 대역(in-memory transport)이나
플랫폼 전용 transport를 넣는 확장점이다. **기본값만 entrypoint별로 다르다.**

## 4. Public 표면

두 entrypoint가 노출하는 타입은 같다.

```ts
interface ZlinkStreamConnector {
  readonly isConnected: boolean;
  readonly state: ZlinkStreamConnectionState;
  readonly closeReason?: ZlinkStreamCloseReason;
  readonly options: RequiredZlinkStreamConnectorOptions;
  readonly pendingDispatchCount: number;

  connect(signal?: AbortSignal): Promise<void>;
  close(signal?: AbortSignal): Promise<void>;
  dispatch(signal?: AbortSignal): Promise<void>;

  send(payload: unknown, messageType?: Function): ZlinkStreamSendCall;
  request(payload: unknown, messageType?: Function): ZlinkStreamRequestCall;
  waitFor<TPayload>(name: string): ZlinkStreamWaitCall<TPayload>;
  on<TPayload>(
    name: string,
    handler: (message: ZlinkStreamMessage<TPayload>, signal?: AbortSignal) => Promise<void> | void,
    messageType?: Function
  ): Disposable;

  onErrorReceived(handler: (error: ZlinkStreamError, signal?: AbortSignal) => Promise<void> | void): Disposable;
  onDisconnected(handler: (signal?: AbortSignal) => Promise<void> | void): Disposable;
  onConnectionStateChanged(
    handler: (change: ZlinkStreamConnectionStateChanged, signal?: AbortSignal) => Promise<void> | void
  ): Disposable;
  observeInbound(
    observer: (observation: ZlinkStreamInboundObservation, signal?: AbortSignal) => Promise<void> | void
  ): Disposable;
}

interface ZlinkStreamSendCall {
  packetName(name: string): ZlinkStreamSendCall;
  metadata(key: string, value: string): ZlinkStreamSendCall;
  metadata(metadata: ZlinkStreamMetadata): ZlinkStreamSendCall;
  compress(): ZlinkStreamSendCall;
  submit(): void;
}

interface ZlinkStreamRequestCall {
  packetName(name: string): ZlinkStreamRequestCall;
  metadata(key: string, value: string): ZlinkStreamRequestCall;
  metadata(metadata: ZlinkStreamMetadata): ZlinkStreamRequestCall;
  timeout(timeoutMs: number): ZlinkStreamRequestCall;
  compress(): ZlinkStreamRequestCall;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<TReply>;
  submitEncoded(signal?: AbortSignal): Promise<ZlinkStreamEncodedPayload>;
  submit(callback: (result: ZlinkStreamResultOf<ZlinkStreamEncodedPayload>) => void): void;
}

interface ZlinkStreamWaitCall<TPayload = ZlinkStreamEncodedPayload> {
  where(predicate: (message: ZlinkStreamMessage<TPayload>) => boolean): ZlinkStreamWaitCall<TPayload>;
  timeout(timeoutMs: number): ZlinkStreamWaitCall<TPayload>;
  submit(signal?: AbortSignal): Promise<ZlinkStreamMessage<TPayload>>;
}
```

connector 생성은 `zlinkStreamConnectorFactory.create(options)`를 사용한다.

- **취소는 optional `AbortSignal`로 전달한다.** 다른 언어의 cancellation token 모양을 복제하지
  않는다([비동기 실행과 coroutine 정책](../../04-async-execution-policy.ko.md)).
- **event 구독은 `Disposable`을 반환한다.** 해제는 그 `Disposable`로 한다.
- `send`·`request`·`waitFor`는 즉시 실행하지 않고 **call builder를 반환한다.** builder에
  `packetName(...)`·`metadata(...)`·`timeout(...)`·`compress()`를 붙인 뒤 `submit()`으로 제출한다.
  `send`의 `submit()`은 응답을 기다리지 않는다.

option의 기본값은 [공통 스펙 §6.1](../../32-stream-connector.ko.md)이 소유한다. TypeScript는 이를
`ZlinkStreamConnectorOptions`의 필드로 표현하며, 해석된 전체 값을 `RequiredZlinkStreamConnectorOptions`로
노출한다.

## 5. Inbound Observer와 수신 큐

관찰 의미와 격리·overflow 규칙은 [공통 스펙 §10](../../32-stream-connector.ko.md)이 소유한다. 이
문서는 TypeScript 표면만 고정한다.

`observeInbound(...)`는 `Disposable`을 반환하며, **`connect(...)` 호출 전에만** 등록할 수 있다.
연결이 시작된 뒤 등록하면 오류를 던진다.

```ts
const registration = client.observeInbound((observation) => {
  console.log(
    `stream-inbound kind=${observation.kind} ` +
    `name=${observation.name} bytes=${observation.payloadLength}`);
});
await client.connect();
```

두 큐의 한도는 다음 option으로 조절한다. 기본값은
[공통 스펙 §6.1](../../32-stream-connector.ko.md)이 소유한다.

| option | 대상 큐 | overflow 시 error handler로 보고하는 코드 |
|---|---|---|
| `maxInboundObserverNotifications` | observer notification 큐 | `ZlinkStreamErrorCode.ObserverDropped` |
| `maxReceivedMessages` | 수신 메시지 큐(§10.1) | `ZlinkStreamErrorCode.ReceivedMessageDropped` |

observer callback 실패는 `ZlinkStreamErrorCode.ObserverFailed`로 보고한다.
`maxInboundObserverPayloadPreviewBytes`는 observation에 담을 payload preview 길이를 정한다.

## 6. 세션 종료 사유 (close reason)

사유의 값 집합과 의미는 [공통 스펙 §6.2](../../32-stream-connector.ko.md)가 소유한다. 이 문서는
TypeScript 표면만 고정한다.

`ZlinkStreamCloseReason`은 닫힌 union이다.

```ts
type ZlinkStreamCloseReason =
  | 'ClientClose' | 'IdleTimeout' | 'HeartbeatTimeout'
  | 'ServerDrain' | 'ProtocolError' | 'TransportError';
```

**Node에서는 이 값을 connector의 읽기 전용 속성 `closeReason`으로 노출한다.**
`onDisconnected(...)` handler는 인자로 사유를 받지 않으므로, handler 안에서 `closeReason`을
읽는다. 아직 끊긴 적이 없으면 `undefined`다.

## 7. 구현 상태

§1~§3의 transport 계약을 구현했다. 패키지는 Node 기본 진입점과 `/browser` 진입점을 분리하고,
브라우저 진입점은 플랫폼의 네이티브 `WebSocket`만 사용한다. 공용 protocol과 connector
runtime에는 Node 전용 모듈을 import하지 않는다. Node 전용 transport와 flow context는 Node
진입점에서만 선택한다.

실제 npm tarball 소비자에서 두 진입점의 runtime import와 TypeScript type 해석을 확인했다.
브라우저 bundle은 `net`, `tls`, `async_hooks`, `crypto` Node 모듈과 `Buffer`를 포함하지 않는다.
현재 검증 환경에는 headless 브라우저 실행 도구가 없어서 실제 브라우저 프로세스의 WSS 검증은
실행하지 못했다. 대신 네이티브 `WebSocket`과 같은 event 계약을 제공하는 테스트 대역으로
WSS 연결, request/reply, push 수신을 검증했다.

브라우저 비동기 flow 문맥은 아직 [공통 flow 계약의 MFLOW-EXT-014](../../53-flow-correlation.ko.md)를
충족하지 못한다. browser runtime에는 `AsyncLocalStorage`에 해당하는 표준 기능이 없고, 현재
`BrowserZlinkFlowContext`는 handler가 기다리는 동안 관련 없는 callback에 inbound flow를 노출할
수 있다. callback 직후 문맥을 지우면 반대로 `await` 이후 continuation이 flow를 잃으므로 완료로
표시하지 않는다. 현재 구현 차이는 [implementation gap §4.10](../../90-implementation-gap.ko.md)에
기록한다.

## 8. 회귀 테스트

stream connector 문서는 connector 표면이 framework server 표면과 다른 책임을 가진다는 점을
계속 유지해야 한다. 아래 회귀 항목이 이 문서와 구현을 함께 고정한다.

- `test/contract/documentation-regression.test.js`
  - spec 문서가 회귀 테스트 절을 계속 포함하는지 확인한다.
- `test/contract/stream-connector*.test.js`
  - connector wait builder, codec decode, inbound observer가 public surface대로 동작하는지 확인한다.
- 브라우저 entrypoint 회귀
  - 브라우저 entrypoint의 실제 bundle 그래프에 Node 전용 모듈(`net`·`tls`·`async_hooks`·`crypto`)과
    `Buffer`가 없는지 확인한다(§2).
  - 브라우저 entrypoint가 `tcp://`·`tls://` endpoint를 `ConfigurationError`로 거부하는지 확인한다(§3).
  - 플랫폼 `WebSocket` adapter로 `wss://` 연결, request/reply와 push 수신이 동작하는지 확인한다.
  - handler의 `await` continuation은 같은 flow를 사용하고, handler가 기다리는 동안 실행되는 관련
    없는 callback은 새 application flow를 사용하는지 확인한다. 이 항목은 현재 실패하는 gap이다.
  - 배포 전 검증 환경에 headless 브라우저를 제공하면 실제 브라우저에서도 같은 WSS 시나리오를 확인한다.
- `samples/*`
  - sample client가 connector helper와 `waitFor(...)`를 기본 경로로 사용하는지 확인한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
