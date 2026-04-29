[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework .NET STREAM Decisions

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` `STREAM` 표면에서 구현 전에 닫아 둔 결정을
> 한곳에 모아 둔 문서다.

## 1. 목적

`STREAM`은 packet session과 raw session의 큰 방향만 잡아 두면 구현 단계에서 다시
흔들리기 쉽다. 이 문서는 serializer, write, lifecycle 기준을 미리 닫아서
registration surface와 테스트 기준이 바뀌지 않게 만드는 데 목적이 있다.

현재 기준에서 먼저 고정한 축은 아래 세 가지다.

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

### 2.2 확정 기준

- serializer helper의 기본 진입점은 `Parse<T>()` 하나로 둔다.
- serializer 선택은 target type 기준으로 한다.
  generated protobuf 타입은 `IMessage<T>` 계열인지 보고 protobuf parser를 고르고,
  그 밖의 일반 POCO class는 json parser를 기본값으로 둔다.
- serializer helper와 encode helper는 framework 기본 패키지가 아니라 확장
  패키지에 둔다.
- parse 실패는 예외로 보고, 기본 경로는 `TryParse...`보다 `Parse<T>()`를 쓴다.
  필요하면 확장 패키지가 별도 `TryParse...` helper를 추가할 수 있다.

### 2.3 정리

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

### 3.2 확정 기준

- raw write와 framed write는 overload만으로 구분한다.
- framed write는 `Message header, Message body` 시그니처를 사용한다.
- 현재 `STREAM` 기본 표면에는 별도 reply helper를 두지 않는다.
- header encode와 body encode는 serializer 확장 helper가 맡고, session은 이미 만든
  `Message`를 `Write(...)`로 넘긴다.
- `SendFlags`는 session write 표면에 그대로 노출한다.

### 3.3 정리

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

### 4.2 확정 기준

- `OnConnectedAsync(...)`는 `ConnectionReady`에 대응시킨다.
- `Disconnected`는 `OnDisconnectedAsync(...)`로 올린다.
- session-correlatable `TransportError`는 먼저 `OnErrorAsync(...)`로 올리고,
  이어서 연결 종료가 확인되면 `OnDisconnectedAsync(...)`로 이어진다.
- handshake 실패는 session callback으로 올리지 않고 runtime monitoring에만 남긴다.
- bind/accept/close 실패 같은 socket-level 오류는 session callback에서 제외한다.
- `NativeCode`가 0인 경우에도 `Internal` category로 묶되, 추가 원시 errno가
  없다는 뜻으로 읽는다.

### 4.3 정리

- `OnConnectedAsync(...)`는 `ConnectionReady`에 대응시키는 편이 더 자연스럽다.
- `OnDisconnectedAsync(...)`는 `Disconnected`에 대응시킨다.
- `OnErrorAsync(...)`는 session으로 매핑 가능한 transport 오류만 받는다.
- application handler 내부 예외는 `OnErrorAsync(...)`로 올리지 않는다.
- bind/accept/close 실패 같은 node/socket 단위 오류는 `SocketMonitor`에만 남긴다.
- `OnErrorAsync(...)`의 payload는 `ZLinkStreamError`로 두고, framework error kind를
  먼저 보여 주되 native errno와 메시지는 optional diagnostic detail로만 남긴다.

## 5. 그 밖의 남은 항목

추가 구현 기준은 아래처럼 고정한다.

- session callback은 같은 session 안에서는 직렬로 실행한다.
  같은 연결에 대해 `OnDispatchAsync(...)`, `OnDispatchAsync(...)`, lifecycle callback이
  서로 병렬로 겹치지 않게 하는 편을 기본으로 본다.
- session callback은 native/socket callback 안에서 직접 실행하지 않는다.
  framework는 callback을 managed task로 넘긴 뒤 application callback을 호출한다.
  transport callback이 application 처리 시간이나 예외에 직접 묶이지 않게 하기 위한
  계약이다.
- send queue와 backpressure는 하부 socket 동작을 따르되, application이 보는 계약은
  `WithDontWait()` 또는 `Write(...)`의 `false` 반환으로만 읽히게 한다.
- `STREAM` session 등록은 attribute 기반으로 열지 않고 명시 등록만 지원한다.
- serializer helper는 `Message.AsReadOnlySpan()` 기반으로 구현해서 불필요한 복사를
  줄이는 편을 기본으로 본다.

## 6. 현재 결정 요약

지금 단계에서 새로운 큰 기능을 더 붙일 필요는 크지 않다.
대신 아래 항목을 기준으로 구현을 바로 진행하면 된다.

1. serializer 계층의 책임과 패키지 경계
2. sync `Write(...)` API 표면
3. monitor event -> session lifecycle 계약
4. `OnErrorAsync(...)`와 `OnDisconnectedAsync(...)`의 경계

이 항목들이 정리되면 `.NET` `STREAM` 초안은 큰 틀에서 구현 가능한 수준으로
가까워진다.
