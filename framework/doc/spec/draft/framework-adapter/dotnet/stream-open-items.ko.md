[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework .NET STREAM Open Items

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` `STREAM` 표면에서 아직 닫지 않은 항목을
> 체크리스트처럼 모아 둔 문서다.

## 1. 목적

`STREAM`은 packet handler와 raw handler의 큰 방향은 정리됐지만, 실제 구현 전에
몇 가지 중요한 계약을 더 좁혀야 한다. 이 문서는 남은 항목을 우선순위 기준으로
정리한다.

현재 기준에서 우선순위가 높은 축은 아래 세 가지다.

1. serializer 계층
2. write API
3. connection / error lifecycle

## 2. serializer 계층

### 2.1 현재 정리된 방향

- `STREAM` handler는 `Message`를 받는다.
- typed packet handler는 주로 `header`만 고정 타입으로 올린다.
- body는 `header.MsgId`를 보고 각 packet 타입으로 parse한다.
- protobuf/json/messagepack 같은 객체 변환은 transport 본체보다
  `playhouse/extensions` 같은 extension 계층으로 두는 방향을 기본으로 본다.

### 2.2 남은 결정 항목

- `ParseProto<T>()`, `ParseJson<T>()`, `ParseMessagePack<T>()`를 `Message` extension으로
  둘지, serializer service를 통해 노출할지
- serializer 선택 기준을 `msgId`, `content-type`, 등록 순서 중 무엇으로 둘지
- serializer helper를 `framework` 기본 패키지에 둘지, 확장 패키지로 나눌지
- parse 실패 시 예외를 던질지, `TryParse...`를 기본으로 둘지
- encode helper도 같은 계층에 둘지

### 2.3 현재 추천 방향

- transport 본체는 `Message`까지만 책임진다.
- serializer는 extension 패키지로 분리한다.
- handler 샘플은 `body.ParseProto<T>()` 같은 helper를 기준으로 쓴다.
- helper 내부는 `Message.AsReadOnlySpan()`를 사용해서 추가 복사를 피한다.

## 3. write API

### 3.1 현재 비어 있는 점

현재 문서는 수신 handler에 더 초점이 맞춰져 있다. 하지만 실제 `STREAM` 구현에서는
송신 표면도 같이 있어야 한다.

### 3.2 남은 결정 항목

- raw write와 framed write를 별도 함수로 나눌지
- framed write는 `Message header, Message body`로 받을지
- typed header + raw body write를 기본 표면으로 둘지
- reply 성격의 helper를 둘지
- header encode와 body encode를 serializer helper가 맡을지

### 3.3 현재 추천 방향

- raw write와 framed write는 분리한다.
- framed write는 아래처럼 두는 방향이 자연스럽다.

```csharp
ValueTask WriteAsync(
    Message header,
    Message body,
    CancellationToken cancellationToken = default);

ValueTask WriteAsync<THeader>(
    THeader header,
    Message body,
    CancellationToken cancellationToken = default);
```

- typed body까지 바로 받는 overload는 나중에 convenience 계층으로 검토한다.

## 4. Connection / Error Lifecycle

### 4.1 현재 비어 있는 점

`STREAM`은 일반 request-response보다 연결 수명과 parse 실패 같은 상황이 더 중요할
수 있다. 그런데 현재 문서에는 open/close/error hook이 아직 없다.

### 4.2 남은 결정 항목

- connection open hook을 framework 기본 표면에 둘지
- close hook을 둘지
- parse 실패를 handler 예외로 볼지, 별도 error callback으로 뺄지
- 알 수 없는 `msgId` 처리 규칙을 둘지
- peer disconnect를 `ZLinkStreamContext`에서 어떻게 노출할지

### 4.3 현재 추천 방향

- open/close/error를 하나의 lifecycle 인터페이스로 묶는 쪽이 읽기 쉽다.
- 단, packet/raw handler보다 우선순위는 낮다.
- 첫 단계에서는 packet/raw handler를 먼저 닫고, lifecycle은 별도 확장 축으로 두는
  편이 구현 리스크가 낮다.

## 5. 그 밖의 남은 항목

- stream dispatch가 어떤 스레드 문맥에서 호출되는지
- send queue와 backpressure 정책
- attribute 등록과 명시 등록을 둘 다 열지
- raw handler와 packet handler를 같은 node에 같이 등록할지
- serializer helper에 zero-copy를 어느 수준까지 보장할지

## 6. 현재 판단

지금 단계에서 새로운 큰 기능을 더 붙일 필요는 크지 않다.
대신 아래 세 항목을 먼저 닫는 것이 중요하다.

1. serializer 계층의 책임과 패키지 경계
2. framed write API 표면
3. connection / error lifecycle 계약

이 세 가지가 정리되면 `.NET` `STREAM` 초안은 큰 틀에서 구현 가능한 수준으로
가까워진다.
