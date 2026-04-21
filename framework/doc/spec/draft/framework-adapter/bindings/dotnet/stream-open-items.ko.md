[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework .NET STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` `STREAM` 표면에서 아직 닫지 않은 항목을
> 체크리스트처럼 모아 둔 문서다.

## 1. 목적

`STREAM`은 packet session과 raw session의 큰 방향은 정리됐지만, 실제 구현 전에
몇 가지 중요한 계약을 더 좁혀야 한다. 이 문서는 남은 항목을 우선순위 기준으로
정리한다.

현재 기준에서 우선순위가 높은 축은 아래 세 가지다.

1. serializer 계층
2. write API
3. monitor event -> session lifecycle mapping

## 2. serializer 계층

### 2.1 현재 정리된 방향

- `STREAM` session callback은 `Message`를 받는다.
- packet session은 주로 `header`를 먼저 읽고 body를 다시 parse한다.
- body는 `header.MsgId`를 보고 각 packet 타입으로 parse한다.
- protobuf/json/messagepack 같은 객체 변환은 transport 본체보다
  `playhouse/extensions` 같은 extension 계층으로 두는 방향을 기본으로 본다.

### 2.2 남은 결정 항목

- `Parse<T>()` 하나로 둘지, `ParseProto<T>()`, `ParseJson<T>()` 같은 명시형 helper도
  함께 둘지
- `Parse<T>()`를 둘 경우 serializer 선택 기준을 `IMessage<T>` 여부, `msgId`,
  `content-type`, 등록 순서 중 무엇으로 둘지
- serializer helper를 `framework` 기본 패키지에 둘지, 확장 패키지로 나눌지
- parse 실패 시 예외를 던질지, `TryParse...`를 기본으로 둘지
- encode helper도 같은 계층에 둘지

### 2.3 현재 추천 방향

- transport 본체는 `Message`까지만 책임진다.
- serializer는 extension 패키지로 분리한다.
- handler 샘플은 `body.Parse<T>()` 같은 helper를 기준으로 쓴다.
- generated protobuf 타입은 `IMessage<T>` 계열인지 보고 protobuf로 해석한다.
- 그 밖의 일반 class는 json으로 해석하는 규칙을 기본값으로 둔다.
- helper 내부는 `Message.AsReadOnlySpan()`를 사용해서 추가 복사를 피한다.

## 3. write API

### 3.1 현재 비어 있는 점

현재 문서는 수신 handler에 더 초점이 맞춰져 있다. 하지만 실제 `STREAM` 구현에서는
송신 표면도 같이 있어야 한다.

### 3.2 남은 결정 항목

- raw write와 framed write를 overload만으로 구분할지
- framed write는 `Message header, Message body`로 받을지
- reply 성격의 helper를 둘지
- header encode와 body encode를 serializer helper가 맡을지
- `SendFlags`를 session write에 그대로 노출할지, framework 전용 option으로 감쌀지

### 3.3 현재 추천 방향

- raw write와 framed write는 이름이 아니라 파라미터 overload로 구분한다.
- 현재 `.NET zlink` binding이 가진 sync send/write 표면에 맞춰 아래처럼 두는 편이
  자연스럽다.

```csharp
bool Write(
    Message payload,
    SendFlags flags = SendFlags.None);

bool Write(
    Message header,
    Message body,
    SendFlags flags = SendFlags.None);
```

- temporary backpressure일 때만 `false`를 돌려주고, 그 밖의 submit 실패는 예외로
  보는 쪽이 기본이다.

## 4. Monitor Event -> Session Lifecycle

### 4.1 현재 비어 있는 점

`STREAM` session lifecycle 자체는 올렸지만, 실제로 어떤 monitor 이벤트를
`OnConnectedAsync(...)`, `OnDisconnectedAsync(...)`, `OnErrorAsync(...)`로
승격할지 규칙이 아직 완전히 닫히지 않았다. 특히 `OnErrorAsync(...)`는
`ZLinkStreamError`를 받는 방향으로 정리됐지만, 어떤 monitor 이벤트를
`Internal`, `TransportError`, `HandshakeFailed`로 자를지는 아직 더 좁혀야 한다.

### 4.2 남은 결정 항목

- `Connected`와 `ConnectionReady` 중 어느 시점을 `OnConnectedAsync(...)` 기준으로
  둘지
- `Disconnected`를 항상 `OnDisconnectedAsync(...)`로 올릴지
- `DisconnectReason.TransportError`를 `OnErrorAsync(...)`와
  `OnDisconnectedAsync(...)` 둘 다로 볼지
- handshake 실패를 session error로 볼지, monitor 전용 이벤트로 남길지
- bind/accept/close 실패 같은 socket-level 오류를 session callback에서 완전히
  제외할지
- `InternalErrno`가 0인 경우에도 `Internal`로 묶을지, 별도 보조 정보를 더 둘지

### 4.3 현재 추천 방향

- `OnConnectedAsync(...)`는 `ConnectionReady`에 대응시키는 편이 더 자연스럽다.
- `OnDisconnectedAsync(...)`는 `Disconnected`에 대응시킨다.
- `OnErrorAsync(...)`는 session으로 매핑 가능한 transport 오류만 받는다.
- application handler 내부 예외는 `OnErrorAsync(...)`로 올리지 않는다.
- bind/accept/close 실패 같은 node/socket 단위 오류는 `SocketMonitor`에만 남긴다.
- `OnErrorAsync(...)`의 payload는 `ZLinkStreamError`로 두고,
  `GetErrorCode()`, `GetErrorMessage()`로 기존 `.NET zlink` errno 체계에 다시
  접근할 수 있게 하는 편이 자연스럽다.

## 5. 그 밖의 남은 항목

- stream dispatch가 어떤 스레드 문맥에서 호출되는지
- send queue와 backpressure 정책
- attribute 등록과 명시 등록을 둘 다 열지
- raw session과 packet session을 같은 node에 같이 등록할지
- serializer helper에 zero-copy를 어느 수준까지 보장할지

## 6. 현재 판단

지금 단계에서 새로운 큰 기능을 더 붙일 필요는 크지 않다.
대신 아래 세 항목을 먼저 닫는 것이 중요하다.

1. serializer 계층의 책임과 패키지 경계
2. sync `Write(...)` API 표면
3. monitor event -> session lifecycle 계약

이 세 가지가 정리되면 `.NET` `STREAM` 초안은 큰 틀에서 구현 가능한 수준으로
가까워진다.
