<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Stream Connector For .NET](streaming-client.ko.md) | [다음: ZLink Framework .NET STREAM Decisions](stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [Stream Connector](./streaming-client.ko.md) | [공통 Stream Connector](../../../streaming-client.ko.md)

# Draft -- ZLink Stream Connector For Unity

> 이 문서는 **구현 전 초안**이다.
> 아직 공개된 계약[^public-contract]이 아니며, Unity에서 `ZLink STREAM` 서버에
> 접속하는 `Systems.Zlink.Stream.Connector.Unity` package를 어떤 모양으로 노출할지
> 정리해 둔 문서다.

## 1. 목적

`Systems.Zlink.Stream.Connector.Unity`는 일반 `.NET` package인
`Systems.Zlink.Stream.Connector` 위에 얇게 얹는 Unity 전용 adapter다. Unity package는
새로 wire protocol[^wire-protocol]을 만들지 않는다. TCP, TLS, WS, WSS transport,
STREAM `header + body` framing[^framing], helper header, codec[^codec],
compression의 의미는 모두 [.NET Stream Connector](./streaming-client.ko.md)를 그대로
따른다.

Unity adapter가 추가로 책임지는 영역은 다음과 같다.

- Unity Package Manager 배포 구조
- `asmdef`[^asmdef] 구성
- `MonoBehaviour`[^monobehaviour] wrapper
- Unity main thread[^main-thread] callback dispatch
- Unity lifecycle 처리
- Unity play mode[^play-mode] test

## 2. 패키지 구성

Unity package는 일반 `.NET` connector package를 의존성으로 가져간다.

| 패키지 | 역할 |
|--------|------|
| `Systems.Zlink.Stream.Connector` | core connector, transport, framing, helper header, codec, compression |
| `Systems.Zlink.Stream.Connector.Unity` | Unity wrapper, main thread dispatch, Unity package metadata |

Unity package는 `Systems.Zlink.Stream.Connector` core를 다시 구현하지 않는다. core
packet 의미를 바꾸면 일반 `.NET` client와 Unity client가 서로 다른 protocol을 쓰게
되므로 허용하지 않는다.

## 3. Unity Package 구조

권장 package 이름은 다음과 같다.

- package id: `systems.zlink.stream.connector.unity`
- display name: `ZLink Stream Connector`

권장 폴더 구조는 다음과 같다.

```text
Systems.Zlink.Stream.Connector.Unity/
  package.json
  Runtime/
    Systems.Zlink.Stream.Connector.Unity.asmdef
    ZlinkStreamConnectorBehaviour.cs
    ZlinkUnityCallbackDispatcher.cs
  Samples~/
    BasicStreamConnector/
      BasicStreamConnector.cs
```

`Runtime/` 아래의 코드는 Unity 프로젝트에서 바로 참조할 수 있어야 한다. 샘플은
`Samples~/` 아래에 두어 Unity Package Manager에서 가져올 수 있게 한다.

## 4. Public Surface 초안

Unity 사용자는 `MonoBehaviour` wrapper를 통해 연결과 callback을 다루게 된다.

```csharp
public sealed class ZlinkUnityStreamConnectorOptions
{
    public ZlinkStreamConnectorOptions ConnectorOptions { get; init; }

    public bool ConnectOnStart { get; init; }

    public ZlinkUnityDispatchOptions Dispatch { get; init; }

    public ZlinkUnityPausePolicy PausePolicy { get; init; }
}
```

```csharp
public sealed class ZlinkStreamConnectorBehaviour : MonoBehaviour
{
    public string Endpoint { get; set; } = "tcp://127.0.0.1:18082";

    public bool ConnectOnStart { get; set; }

    public ZlinkUnityPausePolicy PausePolicy { get; set; }

    public bool IsConnected { get; }

    public ZlinkStreamConnector? Connector { get; }

    public event Action<ZlinkStreamError>? ErrorReceived;

    public event Action? Disconnected;

    public void Configure(ZlinkUnityStreamConnectorOptions options);

    public Task ConnectAsync(CancellationToken cancellationToken = default);

    public Task CloseAsync(CancellationToken cancellationToken = default);

    public ZlinkStreamSendBuilder<TBody> Send<TBody>(
        TBody body);

    public ZlinkStreamRequestBuilder<TBody> Request<TBody>(
        TBody body);

    public IDisposable On<TBody>(
        Action<ZlinkStreamMessage<TBody>> handler);

    public IDisposable On<TBody>(
        string name,
        Action<ZlinkStreamMessage<TBody>> handler);
}
```

Unity wrapper는 `Task`를 노출할 수는 있지만, 사용자 callback은 반드시 Unity main
thread에서 호출되어야 한다. 사용자는 `On<TBody>(...)`로 등록한 typed callback
안에서 `GameObject`, `Transform`, `UI` 같은 Unity 객체를 그대로 다룰 수 있어야
하기 때문이다.

typed `Send`, `Request`, codec, compression helper는 core connector와 같은 의미를
유지한다. Unity wrapper가 자체적으로 별도의 이름 규칙이나 helper header를 새로
만들지 않는다.

## 5. Callback Dispatch

core connector의 receive loop는 worker thread[^worker-thread]에서 실행될 수 있다.
Unity adapter는 worker thread에서 사용자 callback을 직접 호출하지 않는다.

권장 동작은 다음 순서다.

1. core connector가 typed packet, error, disconnect event를 받는다.
2. Unity adapter가 그 event를 thread-safe queue[^thread-safe-queue]에 넣는다.
3. `Update()`에서 queue를 비운다.
4. 사용자 callback을 Unity main thread에서 호출한다.

callback queue에는 최대 pending 개수를 둘 수 있다. 초과했을 때 drop할지
disconnect할지를 옵션으로 정한다.

```csharp
public sealed class ZlinkUnityDispatchOptions
{
    public int MaxPendingCallbacks { get; init; } = 4096;

    public ZlinkUnityCallbackOverflowPolicy OverflowPolicy { get; init; }
}

public enum ZlinkUnityCallbackOverflowPolicy
{
    Disconnect,
    DropNewest,
    DropOldest
}
```

기본값으로는 `Disconnect`가 적합하다. callback queue overflow는 사용자가 packet을
제때 처리하지 못하고 있다는 신호이므로, 조용히 유실시키면 문제를 추적하기
어려워지기 때문이다.

dispatcher 구현은 다음 표면을 만족해야 한다.

```csharp
public sealed class ZlinkUnityCallbackDispatcher : MonoBehaviour
{
    public int PendingCount { get; }

    public void Enqueue(Action callback);

    public void Drain();
}
```

`Drain()`은 `Update()`에서 호출한다. package 내부 구현은 다른 타입을 사용해도
무방하지만, "사용자 callback은 Unity main thread에서 호출된다"는 계약만은 반드시
유지해야 한다.

## 6. Lifecycle

Unity adapter는 다음 lifecycle 규칙을 지켜야 한다.

- `Start()`에서 `ConnectOnStart`가 켜져 있으면 연결을 시작한다.
- `OnDestroy()`에서 connector를 닫는다.
- `OnApplicationPause(true)` 동작은 자동 disconnect로 고정하지 않는다.
- pause/resume 정책은 옵션으로 둔다.
- domain reload[^domain-reload]나 play mode 종료 중에 dispose가 여러 번 호출되어도
  안전하게 동작해야 한다.

```csharp
public enum ZlinkUnityPausePolicy
{
    KeepConnection,
    CloseOnPause,
    CloseAndReconnectOnResume
}
```

모바일 환경에서는 pause 중에 네트워크가 끊길 수 있으므로 `Disconnected` callback과
명시적 reconnect helper를 함께 제공해야 한다. 자동 reconnect를 기본값으로 켜두지는
않는다.

## 7. Transport

Unity adapter는 core connector의 transport 의미를 바꾸지 않는다.

- TCP
- TLS over TCP
- WebSocket
- WebSocket over TLS

Unity build target별 실제 지원 여부는 Unity runtime과 package 구현이 제공하는
transport 능력을 따른다. 이 draft는 특정 Unity build target의 네트워크 제한을
공개 계약으로 못 박지 않는다.

IL2CPP[^il2cpp]와 AOT[^aot] 환경에서는 reflection 기반 codec 사용에 제약이 있을 수
있다. Unity adapter는 MessagePack, Protobuf 같은 codec helper를 쓸 때 필요한 AOT
설정을 샘플과 가이드에서 분명히 드러내야 한다.

## 8. Sample 기준

Unity sample은 게임 도메인을 끌고 들어오지 않는다. 다음만 보여 준다.

1. endpoint 입력
2. connect 버튼
3. packet name과 body 입력
4. send 버튼
5. received packet log
6. disconnect 버튼

채팅방, room, actor 같은 샘플은 Unity adapter sample이 아니라 별도의 application
sample에서 다룬다.

## 9. 테스트 기준

필수 테스트는 다음과 같다.

- play mode에서 TCP echo
- play mode에서 WebSocket echo
- callback이 Unity main thread에서 호출되는지 검증
- `OnDestroy()`가 close를 호출하는지 검증
- callback queue overflow 정책 검증

TLS/WSS 테스트는 인증서 fixture가 준비된 CI[^ci] 환경에서 실행한다.

## 10. 회귀 테스트

Unity adapter 항목은 공통 Stream Connector 계약을 그대로 지키면서, Unity main
thread callback dispatch와 wrapper lifecycle만 추가로 검증한다. 현재 저장소에는
순수 Unity runtime 테스트가 없으므로, 이 표에는 공통 connector 계약을 실제로
검증하는 테스트만 둔다. Unity runtime 전용 테스트가 새로 추가되면, 실행 가능한
테스트 이름으로 이 표에 추가한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | Unity wrapper가 감싸는 기본 connector의 request/reply 의미가 유지된다. |
| `StreamConnectorTests.TypedCallbackDecompressesServerPacket` | callback의 codec과 compression 의미가 Unity adapter 아래에서도 동일하게 동작한다. |
| `StreamConnectorTests.TcpReceiveDispatchesMultipleHeaderPacketsInOrder` | wrapper 아래의 connector가 여러 server packet callback을 도착 순서대로 처리한다. |
| `StreamConnectorTests.DisconnectedSendFailsBeforeTransportWrite` | 연결이 끊긴 뒤 send가 transport write 전에 실패해서, wrapper lifecycle 정리와 충돌하지 않는다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^wire-protocol]: wire protocol은 네트워크 위에서 바이트가 실제로 어떤 형식으로 흐르는지를 정의한 규약이다.
[^framing]: framing은 연속된 바이트 스트림에서 메시지의 시작과 끝을 구분하는 방식을 가리킨다. STREAM에서는 header와 body를 묶어 하나의 packet 단위로 자른다.
[^codec]: codec은 객체와 바이트 표현 사이의 직렬화/역직렬화를 담당하는 컴포넌트다. 예: Protobuf, MessagePack, JSON.
[^asmdef]: `asmdef`는 Unity에서 코드 일부를 별도의 어셈블리로 떼어내기 위해 사용하는 Assembly Definition 파일이다.
[^monobehaviour]: `MonoBehaviour`는 Unity의 컴포넌트 기본 타입으로, `GameObject`에 붙어 lifecycle 메서드(`Start`, `Update` 등)를 통해 동작한다.
[^main-thread]: main thread는 Unity의 게임 루프와 모든 `GameObject` 접근이 허용되는 단일 스레드를 가리킨다. Unity 객체는 이 스레드 밖에서 직접 다룰 수 없다.
[^play-mode]: play mode는 Unity Editor에서 게임을 실제로 실행한 상태를 뜻한다. play mode test는 그 상태에서 돌리는 통합 테스트다.
[^worker-thread]: worker thread는 main thread와 별개로 백그라운드 작업을 처리하는 스레드를 가리킨다.
[^thread-safe-queue]: thread-safe queue는 여러 스레드가 동시에 enqueue/dequeue를 호출해도 내부 상태가 깨지지 않도록 동기화된 큐를 뜻한다.
[^domain-reload]: domain reload는 Unity가 스크립트 컴파일이나 play mode 진입/종료 시 .NET 도메인을 다시 로드하는 동작이다. 정적 상태가 초기화되므로 dispose가 다시 일어날 수 있다.
[^il2cpp]: IL2CPP는 Unity가 .NET IL을 C++ 코드로 변환해 네이티브로 빌드하는 백엔드다. 런타임 코드 생성이 제한된다.
[^aot]: AOT(Ahead-Of-Time) 컴파일은 실행 전에 모든 코드를 미리 네이티브로 컴파일하는 방식이다. reflection 기반 동적 코드 생성에 제약이 생긴다.
[^ci]: CI(Continuous Integration)는 코드 변경이 들어올 때마다 자동으로 빌드와 테스트를 실행해 회귀를 빠르게 잡아내는 파이프라인을 가리킨다.
