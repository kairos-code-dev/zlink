<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Stream Connector For .NET](streaming-client.ko.md) | [다음: ZLink Framework ASP.NET Core Monitoring](aspnet-core-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework .NET STREAM Decisions

> 이 문서는 **구현 전 초안**이다.
> 즉 아직 공개 계약[^public-contract]이 아니며, `.NET` `STREAM` 표면에서 구현
> 이전에 미리 닫아 둔 결정을 한곳에 모아 정리한다.

## 1. 목적

`STREAM`[^stream] 은 framework Header 기반 packet session 이 핵심이다. 이 범위를
미리 닫아 두지 않으면, 구현 단계에 들어가서도 결정이 다시 흔들리기 쉽다.

이 문서는 serializer, write, lifecycle 의 기준을 먼저 정해 둔다. 그래야
registration surface 와 테스트 기준도 나중에 같이 흔들리지 않는다.

지금 단계에서 먼저 고정한 축은 다음 세 가지다.

1. serializer[^serializer] 계층
2. write API
3. monitor[^monitor] event -> session lifecycle 매핑

## 2. serializer 계층

### 2.1 현재 정리된 방향

serializer 계층은 다음 네 가지 방향으로 잡혀 있다.

- `STREAM` session callback 은 `ZlinkStreamHeader header` 와 `Message payload` 를
  인자로 받는다.
- packet session 에서는 framework 가 먼저 내부 header 를 읽고, packet name 과
  metadata 를 만들어 둔다.
- application 코드는 `header.Name` 을 보고 각각의 packet 타입으로
  decode 한다.
- protobuf, json, messagepack 같은 객체 변환은 transport 본체에 두지 않는다.
  `playhouse/extensions` 같은 extension 계층에 두는 방향을 기본으로 본다.

### 2.2 확정 기준

지금 단계에서 닫아 두는 기준은 네 가지다.

- serializer helper 의 기본 진입점은 `Parse<T>()` 하나로 통일한다.
- serializer 선택은 target type 을 기준으로 한다. generated protobuf 타입은
  `IMessage<T>` 계열인지 보고 protobuf parser 를 고르고, 그 외의 일반 POCO
  클래스는 json parser 를 기본값으로 둔다.
- serializer helper 와 encode helper 는 framework 기본 패키지에 두지 않는다.
  대신 확장 패키지에 둔다.
- parse 실패는 예외로 본다. 기본 경로는 `TryParse...` 대신 `Parse<T>()` 를
  쓴다. 필요하면 확장 패키지가 별도의 `TryParse...` helper 를 추가할 수 있다.

### 2.3 정리

위 결정을 한 줄씩 요약하면 다음과 같다.

- transport 본체는 `Message` 까지만 책임진다.
- serializer 는 extension 패키지로 분리한다.
- handler 샘플은 payload serializer helper 를 기준으로 작성한다.
- generated protobuf 타입은 `IMessage<T>` 계열인지 보고 protobuf 로 해석한다.
- 그 외의 일반 클래스는 json 으로 해석하는 규칙을 기본값으로 둔다.
- helper 내부에서는 `Message.AsReadOnlySpan()` 을 사용해, 추가 복사가 일어나지
  않게 한다.

## 3. write API

**확정 -- 본문은 [handler-interfaces.ko.md](./handler-interfaces.ko.md) §4.4 와
[aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md) §3 을 본다.**

요지는 다음과 같다.

- `IZLinkStream` 은 `bool Write(Message payload, SendFlags flags = None)` 만
  노출한다. header 는 framework 가 packet 을 만들 때 내부에서 작성하므로
  application 이 임의로 넘기는 overload 를 두지 않는다.
- application 이 packet 을 보내야 할 때는 session context 의 `Send(...)`,
  `Reply(...)` 또는 actor context 의 `IZLinkSessionProxy` 를 쓴다.
- 일시적인 backpressure[^backpressure] 는 `Write(...)` 의 `false` 반환으로만
  표현한다. framework 내부의 session/actor 전송 helper 는 이 결과를 명확한
  실패로 바꿔 호출자에게 전달한다.
- stream connector 의 public 옵션에는 별도의 `SendTimeout` 을 두지 않는다.
  connector 의 request reply 대기에는 `RequestTimeout` 만 사용한다.

## 4. Monitor Event -> Session Lifecycle

### 4.1 현재 비어 있는 점

`STREAM` session 의 lifecycle 자체는 이미 정리되어 있다. 다만 실제로 어떤
monitor 이벤트를 `OnConnectedAsync(...)`, `OnDisconnectedAsync(...)`,
`OnErrorAsync(...)` 로 승격할지에 대한 규칙은 아직 완전히 닫혀 있지 않다.

특히 `OnErrorAsync(...)` 는 `ZLinkStreamError` 를 받는 방향으로 정리됐지만,
어떤 monitor 이벤트를 `Internal`, `TransportError`, `HandshakeFailed` 로
나눌지는 더 좁혀야 한다.

### 4.2 확정 기준

monitor 이벤트를 session callback 으로 어떻게 올릴지는 다음과 같이 닫는다.

- `OnConnectedAsync(...)` 는 `ConnectionReady` 이벤트에 매핑한다.
- `Disconnected` 는 `OnDisconnectedAsync(...)` 로 올린다.
- session 과 연관 지을 수 있는 `TransportError` 는 두 단계로 다룬다. 먼저
  `OnErrorAsync(...)` 로 올리고, 이어서 연결이 끝났음이 확인되면
  `OnDisconnectedAsync(...)` 로 연결한다.
- handshake[^handshake] 실패는 session callback 으로 올리지 않는다. 대신
  runtime monitoring 에만 기록한다.
- bind, accept, close 실패 같은 socket-level 오류는 session callback 에서
  제외한다.
- `NativeCode` 가 0 인 경우에도 `Internal` category 로 묶는다. 다만 "추가 원시
  errno 가 없다" 는 의미로 읽는다.

### 4.3 정리

위 결정을 한 줄씩 요약하면 다음과 같다.

- `OnConnectedAsync(...)` 는 `ConnectionReady` 에 매핑하는 편이 더 자연스럽다.
- `OnDisconnectedAsync(...)` 는 `Disconnected` 에 매핑한다.
- `OnErrorAsync(...)` 는 session 으로 매핑 가능한 transport 오류만 받는다.
- application handler 내부에서 발생한 예외는 `OnErrorAsync(...)` 로 올리지
  않는다.
- bind, accept, close 실패 같은 node / socket 단위 오류는 `SocketMonitor` 에만
  남긴다.
- `OnErrorAsync(...)` 의 payload 는 `ZLinkStreamError` 로 둔다. framework
  error kind 를 먼저 보여 주고, native errno 와 메시지는 optional diagnostic
  detail 로만 남긴다.

## 5. 그 밖의 남은 항목

추가로 필요한 구현 기준은 다음과 같이 고정한다.

- session callback 은 같은 session 안에서 직렬로 실행한다. 같은 연결에 대해
  `OnDispatchAsync(...)`, `OnDispatchAsync(...)`, lifecycle callback 이 서로
  병렬로 겹치지 않게 하는 편을 기본으로 둔다.
- stream socket 은 같은 session 의 frame 도착 순서를 보존한다. framework 는
  이 순서를 session 별 내부 실행 queue 에서 callback 순서로 그대로 이어 준다.
  이 queue 는 framework 내부 구현이다. 즉 별도의 application session mailbox
  를 공개하거나 요구하지는 않는다.
- session callback 은 native / socket callback 안에서 직접 실행하지 않는다.
  framework 가 callback 을 managed task 로 넘긴 뒤에 application callback 을
  호출한다. 이렇게 두는 이유는, transport callback 이 application 의 처리
  시간이나 예외에 직접 묶이지 않게 하기 위해서다.
- send queue 와 backpressure 는 하부 socket 동작을 따른다. application 이
  보는 낮은 수준 계약은 `Write(...)` 의 성공 여부이고, 일반 application 코드는
  session/actor helper 의 submit API 를 통해 같은 의미를 받는다.
- `STREAM` session 등록은 attribute 기반으로 열지 않는다. 명시 등록만 지원한다.
- serializer helper 는 `Message.AsReadOnlySpan()` 을 기반으로 구현한다. 불필요
  한 복사를 줄이려는 의도다.

## 6. 현재 결정 요약

지금 단계에서 새로 큰 기능을 더 붙일 필요는 크지 않다. 대신 아래 네 가지를
기준으로 바로 구현에 들어가면 된다.

1. serializer 계층의 책임과 패키지 경계
2. `Write(...)` API 표면
3. monitor event -> session lifecycle 계약
4. `OnErrorAsync(...)`와 `OnDisconnectedAsync(...)`의 경계

이 네 항목이 정리되어 있으면, `.NET` `STREAM` 초안은 큰 틀에서 바로 구현이
가능한 수준에 가깝다.

## 7. 회귀 테스트

STREAM open item 은 이미 결정이 끝난 항목과 아직 남아 있는 항목을 구분해서
테스트로 닫는다. serializer, write, monitor mapping 결정을 변경할 때, 이미
완료된 항목은 실제로 실행되는 regression[^regression] test 와 함께 유지한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamIntegrationTests.HeaderStreamSession_Receives_Replies_And_Tracks_Lifecycle` | header session의 write/reply와 lifecycle 매핑이 현재 결정과 일치한다. |
| `StreamIntegrationTests.StreamSessionRuntime_Only_Exposes_Enqueue_Callback_Entrypoints` | callback dispatch 정책이 transport 직접 호출로 다시 되돌아가지 않는다. |
| `StreamConnectorTests.HeaderCodecRoundTripsMetadataAndRequestSeq` | header metadata와 request sequence encoding이 round-trip된다. |
| `TopologyMultiProcessTests.StreamRawSession_OnConnected_Emits_Metadata_Once_From_TestHostProcess` | 실제 프로세스 경계에서 connection ready가 session connected metadata로 한 번 매핑된다. |
| `TopologyMultiProcessTests.StreamRawSession_OnError_Reports_TransportError_For_RemoteDisconnect` | 실제 프로세스 경계에서 disconnect가 transport error callback으로 매핑된다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^stream]: `STREAM` 은 클라이언트와 서버 사이에 지속 연결을 유지하면서 framework Header 기반 packet 을 주고받는 세션형 통신 추상이다.
[^serializer]: serializer 는 객체와 바이트 표현을 서로 변환해 주는 컴포넌트로, protobuf/json/messagepack 같은 직렬화 포맷별 구현이 있다.
[^monitor]: monitor 는 소켓이나 노드의 상태 변화 이벤트(connection ready, disconnected, transport error 등)를 외부에 알리는 컴포넌트를 가리킨다.
[^backpressure]: backpressure 는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^handshake]: handshake 는 연결을 본격적으로 사용하기 전에 양쪽이 프로토콜 버전, 인증, 옵션 등을 합의하는 초기 교환 절차다.
[^regression]: regression(회귀) 은 이전 버전에서 잘 동작하던 기능이 새 변경 때문에 다시 깨지는 현상을 가리킨다. regression test 는 그런 일을 막기 위해 항상 돌리는 테스트 묶음이다.
