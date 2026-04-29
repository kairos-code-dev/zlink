[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [Stream Connector](./streaming-client.ko.md) | [공통 Stream Connector](../../../streaming-client.ko.md)

# Draft -- ZLink Stream Connector For Unity

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, Unity에서 `ZLink STREAM` 서버에 접속하는
> `Systems.Zlink.Stream.Connector.Unity` package를 어떤 모양으로 노출할지 정리하기 위한
> 문서다.

## 1. 목적

`Systems.Zlink.Stream.Connector.Unity`는 일반 `.NET` package인
`Systems.Zlink.Stream.Connector` 위에 얇게 얹는 Unity 전용 adapter다. Unity package는
새로운 wire protocol을 만들지 않는다. TCP, TLS, WS, WSS transport, STREAM
`header + body` framing, helper header, codec, compression 의미는
[.NET Stream Connector](./streaming-client.ko.md)를 따른다.

Unity adapter가 추가로 책임지는 범위는 아래와 같다.

- Unity Package Manager 배포 구조
- `asmdef` 구성
- `MonoBehaviour` wrapper
- Unity main thread callback dispatch
- Unity lifecycle 처리
- Unity play mode test

## 2. 패키지 구성

Unity package는 일반 `.NET` connector package를 의존한다.

| 패키지 | 역할 |
|--------|------|
| `Systems.Zlink.Stream.Connector` | core connector, transport, framing, helper header, codec, compression |
| `Systems.Zlink.Stream.Connector.Unity` | Unity wrapper, main thread dispatch, Unity package metadata |

Unity package는 `Systems.Zlink.Stream.Connector` core를 재구현하지 않는다. core packet 의미를
바꾸면 일반 `.NET` client와 Unity client가 서로 다른 protocol을 쓰게 되므로 허용하지
않는다.

## 3. Unity Package 구조

권장 package 이름:

- package id: `systems.zlink.stream.connector.unity`
- display name: `ZLink Stream Connector`

권장 폴더 구조:

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

`Runtime/` 코드는 Unity 프로젝트에서 바로 참조할 수 있어야 한다. 샘플은
`Samples~/` 아래에 두어 Unity Package Manager에서 가져올 수 있게 한다.

## 4. Public Surface 초안

Unity 사용자는 `MonoBehaviour` wrapper를 통해 연결과 callback을 다룰 수 있어야 한다.

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

Unity wrapper는 `Task`를 노출할 수 있지만, 사용자 callback은 Unity main thread에서
호출해야 한다. 사용자는 `On<TBody>(...)`로 등록한 typed callback 안에서
`GameObject`, `Transform`, `UI` 같은 Unity 객체를 직접 다룰 수 있어야 한다.

typed `Send`, `Request`, codec, compression helper는 core connector와 같은 의미를
유지한다. Unity wrapper가 별도 이름 규칙이나 별도 helper header를 만들면 안 된다.

## 5. Callback Dispatch

core connector의 receive loop는 worker thread에서 실행될 수 있다. Unity adapter는
worker thread에서 사용자 callback을 직접 호출하지 않는다.

권장 동작:

1. core connector가 typed packet, error, disconnect event를 받는다.
2. Unity adapter가 event를 thread-safe queue에 넣는다.
3. `Update()`에서 queue를 비운다.
4. 사용자 callback을 Unity main thread에서 호출한다.

callback queue에는 최대 pending 개수를 둘 수 있다. 초과 시 drop할지 disconnect할지
옵션으로 정한다.

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

기본값은 `Disconnect`가 적합하다. callback queue overflow는 사용자가 packet을
처리하지 못하고 있다는 뜻이므로 조용히 유실시키면 문제를 찾기 어렵다.

dispatcher 구현은 아래 표면을 만족해야 한다.

```csharp
public sealed class ZlinkUnityCallbackDispatcher : MonoBehaviour
{
    public int PendingCount { get; }

    public void Enqueue(Action callback);

    public void Drain();
}
```

`Drain()`은 `Update()`에서 호출한다. package 내부 구현은 다른 타입을 사용할 수
있지만, 사용자 callback이 Unity main thread에서 호출된다는 계약은 유지해야 한다.

## 6. Lifecycle

Unity adapter는 아래 lifecycle을 지켜야 한다.

- `Start()`에서 `ConnectOnStart`가 켜져 있으면 연결을 시작한다.
- `OnDestroy()`에서 connector를 닫는다.
- `OnApplicationPause(true)` 동작은 자동 disconnect로 고정하지 않는다.
- pause/resume 정책은 옵션으로 둔다.
- domain reload와 play mode 종료 중 dispose가 여러 번 호출되어도 안전해야 한다.

```csharp
public enum ZlinkUnityPausePolicy
{
    KeepConnection,
    CloseOnPause,
    CloseAndReconnectOnResume
}
```

모바일 환경에서는 pause 중 네트워크가 끊길 수 있으므로 `Disconnected` callback과
명시적 reconnect helper를 제공해야 한다. 자동 reconnect를 기본값으로 켜지는 않는다.

## 7. Transport

Unity adapter는 core connector의 transport 의미를 바꾸지 않는다.

- TCP
- TLS over TCP
- WebSocket
- WebSocket over TLS

Unity target별 실제 지원 여부는 Unity runtime과 package 구현이 제공하는 transport
능력을 따른다. 이 draft는 특정 Unity build target의 네트워크 제한을 공개 계약으로
고정하지 않는다.

IL2CPP와 AOT 환경에서는 reflection 기반 codec 사용에 제약이 있을 수 있다. Unity
adapter는 MessagePack, Protobuf 같은 codec helper를 사용할 때 필요한 AOT 설정을
샘플과 guide에서 드러내야 한다.

## 8. Sample 기준

Unity sample은 게임 도메인을 넣지 않는다. 아래만 보여 준다.

1. endpoint 입력
2. connect 버튼
3. packet name과 body 입력
4. send 버튼
5. received packet log
6. disconnect 버튼

채팅방, room, actor 같은 샘플은 Unity adapter sample이 아니라 별도 application
sample에서 다룬다.

## 9. 테스트 기준

필수 테스트:

- play mode에서 TCP echo
- play mode에서 WebSocket echo
- callback이 Unity main thread에서 호출되는지 검증
- `OnDestroy()`가 close를 호출하는지 검증
- callback queue overflow 정책 검증

TLS/WSS 테스트는 인증서 fixture가 준비된 CI 환경에서 실행한다.
