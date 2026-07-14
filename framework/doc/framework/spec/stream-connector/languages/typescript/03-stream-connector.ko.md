<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:end -->

# TypeScript Stream Connector

> 이 문서는 [Stream Connector 공통 스펙](../../32-stream-connector.ko.md)의 **TypeScript 투영**이다.
> transport·wire·생명주기·오류 의미는 공통 스펙이 소유하고, 이 문서는 그 의미가
> TypeScript에서 갖는 **정확한 public 표면**을 고정한다.

TypeScript connector는 `@zlink-systems/stream-connector` 패키지로 제공하는 브라우저 client
connector다.
서버 framework와 별도 모듈이며 request/reply, dispatch(`Manual`/`Immediate`), typed payload API를
client code에서 사용하게 한다. JSON, MessagePack, Protobuf 또는 custom codec은 connector를 만들 때
`codec` option 하나로 주입한다. typed `send`/`request`/`on`/`waitFor` 표면은 주입된 codec으로 업무
DTO를 encode/decode한다.

## 1. 대상 실행 환경

**엔진 × 빌드 타깃별 담당 connector는 [공통 스펙 §2](../../32-stream-connector.ko.md)가 소유한다.**
그 배정에 따라 TypeScript connector가 담당하는 것은 **브라우저 계열**(웹 client, Unity WebGL,
Cocos Creator web, Godot Web)이다. Node.js process는 connector의 제품 실행 환경이 아니다.

**웹(브라우저·WASM)으로 빌드하는 모든 엔진이 언어와 무관하게 이 connector를 사용한다.**

## 2. 진입점(entrypoint)

공개 진입점은 package root인 `@zlink-systems/stream-connector` 하나다. 이 진입점은 플랫폼
`WebSocket`을 사용하는 브라우저 구현과 ESM type declaration을 직접 내보낸다. `/browser` subpath와
Node 조건부 export는 제공하지 않는다.

**계약:**

- package root의 번들 그래프에는 **`net`·`tls`·`Buffer` 같은 Node 전용 모듈이 포함되지
  않는다.** 검증 범위는 §8의 문서가 소유한다.
- 공통 wire 계층(`@zlink-systems/stream-wire`)은 두 런타임에서 **같은 코드**로 동작한다.
  브라우저 ESM과 server CommonJS 산출물은 같은 source와 wire 상수를 사용하며 `Uint8Array` byte
  fixture가 일치해야 한다.

## 3. Transport

scheme → transport 매핑은 [공통 스펙 §3.1](../../32-stream-connector.ko.md)을 따른다.
TypeScript connector가 사용할 수 있는 transport는 **`ws`와 `wss`뿐**이다.

**package root가 `tcp://`·`tls://` endpoint를 받으면 `ZlinkStreamErrorCode.ConfigurationError`로
즉시 실패한다.** 연결을 시도하다 런타임에 조용히 실패하지 않는다.

브라우저에서 `ws`·`wss`는 **플랫폼의 네이티브 `WebSocket`** 으로 구현한다. 핸드셰이크와 프레이밍을
브라우저가 수행하므로 connector가 직접 구현하지 않는다.

### 3.1 transport factory 주입

`transportFactory` option은 테스트 대역(in-memory transport)이나 플랫폼 전용 transport를 넣는
확장점이다. 기본값은 플랫폼 `WebSocket` adapter다. Node transport 호환 지점으로 사용하지 않는다.

## 4. Public 표면

package root가 노출하는 public 타입은 다음과 같다.

```ts
interface ZlinkStreamFlow {
  readonly flowId: string;
  readonly flowOrigin: ZlinkFlowOrigin;
}

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
  waitFor<TPayload = ZlinkStreamEncodedPayload>(name: string): ZlinkStreamWaitCall<TPayload>;
  on<TPayload = ZlinkStreamEncodedPayload>(
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
  flowFrom(flow: ZlinkStreamFlow): ZlinkStreamSendCall;
  submit(): void;
}

interface ZlinkStreamRequestCall {
  packetName(name: string): ZlinkStreamRequestCall;
  metadata(key: string, value: string): ZlinkStreamRequestCall;
  metadata(metadata: ZlinkStreamMetadata): ZlinkStreamRequestCall;
  timeout(timeoutMs: number): ZlinkStreamRequestCall;
  compress(): ZlinkStreamRequestCall;
  flowFrom(flow: ZlinkStreamFlow): ZlinkStreamRequestCall;
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
  않는다([비동기 실행과 coroutine 정책](../../../04-async-execution-policy.ko.md)).
- **event 구독은 `Disposable`을 반환한다.** 해제는 그 `Disposable`로 한다.
- `send`·`request`·`waitFor`는 즉시 실행하지 않고 **call builder를 반환한다.** builder에
  `packetName(...)`·`metadata(...)`·`timeout(...)`·`compress()`를 붙인 뒤 `submit()`으로 제출한다.
  `send`의 `submit()`은 응답을 기다리지 않는다.
- inbound handler가 시작한 관련 outbound에는 `flowFrom(message)`를 호출한다. 이 메서드는 message의
  `flowId`와 `flowOrigin`을 한 쌍으로 복사한다. 호출하지 않은 outbound는 `origin=application`인 새
  flow를 시작한다. 자세한 비동기 문맥 경계는 [flow correlation §4.4](../../../server/53-flow-correlation.ko.md)를
  따른다.

option의 기본값은 [공통 스펙 §6.1](../../32-stream-connector.ko.md)이 소유한다. TypeScript는 이를
`ZlinkStreamConnectorOptions`의 필드로 표현하며, 해석된 전체 값을 `RequiredZlinkStreamConnectorOptions`로
노출한다.

## 5. Inbound Observer와 수신 큐

관찰 의미와 격리·overflow 규칙은 [공통 스펙 §10](../../32-stream-connector.ko.md)이 소유한다. 이
문서는 TypeScript 표면만 고정한다.

`observeInbound(...)`는 `Disposable`을 반환하며, **`connect(...)` 호출 전에만** 등록할 수 있다.
연결이 시작된 뒤 등록하면 오류를 던진다.

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

**TypeScript에서는 이 값을 connector의 읽기 전용 속성 `closeReason`으로 노출한다.**
`onDisconnected(...)` handler는 인자로 사유를 받지 않으므로, handler 안에서 `closeReason`을
읽는다. 아직 끊긴 적이 없으면 `undefined`다.

## 7. 구현 상태

§1~§4의 browser-only transport와 명시적 flow 전달 계약을 구현했다. package root는 플랫폼의
네이티브 `WebSocket`만 사용하며 공용 protocol과 connector runtime에는 Node 전용 module을 import하지
않는다. npm tarball, 실제 Chromium의 WS/WSS 실행과 비동기 flow 격리 결과는 구현 계획의 G3·G4
로그에서 함께 검증한다.

## 8. 검증

공통 동작의 검증 범위는 [공통 Stream Connector 스펙](../../32-stream-connector.ko.md)이,
TypeScript 구현에서 실행하는 검증 묶음은
[회귀 검증 matrix](../../../../node/internals/regression-test-matrix.ko.md)가 소유한다.
이 문서는 공개 TypeScript 시그니처와 브라우저 실행 환경을 고정한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
