# .NET Stream Connector 연결 생명주기 초안

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 `Systems.Zlink.Stream.Connector`의 생성, 연결, heartbeat,
reconnect, 종료 의미를 정리하기 위한 설계안이다.
정식 spec 문서와 구현에 반영되기 전까지 응용은 이 동작에 의존하면 안 된다.

## 목적

현재 `ZlinkStreamConnectorOptions`에는 `HeartbeatInterval`, `HeartbeatTimeout`,
`IdleTimeout`이 있지만, 이 값들은 완전한 연결 생명주기 계약으로 이어지지 않는다.
특히 heartbeat timeout 뒤에 자동 reconnect를 수행한다는 공개 계약이 없다.
이 상태에서 옵션만 공개하면 사용자는 timeout 감지와 reconnect가 이미 동작한다고
오해할 수 있다.

이 초안의 목적은 Connector가 어떤 상태를 가지고, 연결이 끊겼을 때 무엇을 보장하며,
자동 reconnect를 제공한다면 어떤 범위까지 책임지는지 명확히 정하는 것이다.

## 범위

이 문서는 .NET Stream Connector 패키지의 상위 API 계약만 다룬다.
Core `STREAM` socket 자체의 bind, routing id, wire framing 계약은 별도 core spec을 따른다.

포함하는 내용:

- Connector 생성과 명시적 연결
- heartbeat 송수신과 timeout 판정
- reconnect 정책
- pending request 처리
- callback handler 유지 여부
- close와 dispose 의미

포함하지 않는 내용:

- core socket의 일반 reconnect option
- Registry, Discovery, Spot의 heartbeat
- 응용 메시지 payload 스키마
- 서버 선택, endpoint discovery, load balancing

## 설계 원칙

1. Connector 생성과 네트워크 연결은 분리한다.
2. 자동 reconnect는 명시적으로 켠 경우에만 수행한다.
3. heartbeat와 reconnect는 호출자가 아니라 Connector 내부 책임으로 둔다.
4. timeout이나 reconnect 중인 상태에서 pending request를 조용히 유지하지 않는다.
5. handler 등록은 연결보다 긴 생명주기를 가진다.
6. 서버가 heartbeat control frame을 지원하지 않으면 heartbeat 기능을 켜면 안 된다.

## 생성 계약

Connector 생성 API는 하나만 둔다.

```csharp
public static class ZlinkStreamConnectorFactory
{
    public static IZlinkStreamConnector Create(
        ZlinkStreamConnectorOptions options);
}
```

`Create()`는 Connector 객체만 만든다. 네트워크 연결은 열지 않는다.
호출자는 handler와 event callback을 먼저 등록한 뒤 `ConnectAsync()`를 호출한다.

```csharp
await using var connector = ZlinkStreamConnectorFactory.Create(options);

connector.ErrorReceived += OnErrorAsync;
connector.Disconnected += OnDisconnectedAsync;
connector.On("packet", OnPacketAsync);

await connector.ConnectAsync(cancellationToken);
```

정적 `ConnectAsync(options)` 편의 함수는 두지 않는다.
그 함수는 생성과 연결을 섞어서 handler를 먼저 등록하는 안전한 흐름을 흐리기 때문이다.

## 상태 모델

Connector는 아래 상태를 가진다. 상태 이름은 구현 단계에서 공개 enum으로 둘 수 있다.

| 상태 | 의미 |
|------|------|
| `Created` | 생성됐지만 아직 연결을 시도하지 않은 상태 |
| `Connecting` | `ConnectAsync()`가 초기 연결을 진행하는 상태 |
| `Connected` | transport가 열려 있고 송수신이 가능한 상태 |
| `Reconnecting` | 기존 transport가 끊겼고 자동 reconnect를 시도하는 상태 |
| `Disconnected` | transport가 없고 자동 reconnect도 진행하지 않는 상태 |
| `Closed` | `CloseAsync()` 또는 `DisposeAsync()`가 끝나 더 이상 사용할 수 없는 상태 |

`IsConnected`는 `Connected` 상태에서만 `true`를 반환한다.
`Reconnecting`은 활성 transport가 없으므로 `false`를 반환한다.

## 옵션 구조

Heartbeat와 reconnect 옵션은 기본 연결 옵션에서 분리한다.
옵션이 없으면 기능이 꺼진 것으로 해석한다.

```csharp
public sealed class ZlinkStreamConnectorOptions
{
    public required Uri Endpoint { get; init; }

    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public ZlinkStreamHeartbeatOptions? Heartbeat { get; init; }

    public ZlinkStreamReconnectOptions? Reconnect { get; init; }
}

public sealed class ZlinkStreamHeartbeatOptions
{
    public TimeSpan Interval { get; init; } = TimeSpan.FromSeconds(10);

    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(30);
}

public sealed class ZlinkStreamReconnectOptions
{
    public bool Enabled { get; init; }

    public TimeSpan InitialDelay { get; init; } = TimeSpan.FromMilliseconds(250);

    public TimeSpan MaxDelay { get; init; } = TimeSpan.FromSeconds(5);

    public double BackoffFactor { get; init; } = 2.0;

    public int? MaxAttempts { get; init; }
}
```

`Heartbeat == null`이면 Connector는 응용 계층 heartbeat를 보내지 않는다.
`Reconnect == null` 또는 `Reconnect.Enabled == false`이면 자동 reconnect를 하지 않는다.

기존 `HeartbeatInterval`, `HeartbeatTimeout`, `IdleTimeout` 같은 평면 옵션은 제거한다.
이름만 있는 옵션보다, 기능 단위의 값 객체가 어떤 기능을 켜는지 더 분명하기 때문이다.

## Heartbeat 계약

Heartbeat는 transport keep-alive와 구분되는 응용 계층 생존 확인이다.
WebSocket의 `KeepAliveInterval`이나 TCP keep-alive만으로는 `HeartbeatTimeout` 계약을
정확히 구현할 수 없으므로, Connector가 직접 control frame을 사용한다.

Heartbeat를 켠 Connector는 `Heartbeat.Interval`마다 heartbeat ping을 보낸다.
상대 endpoint는 heartbeat ping을 받으면 사용자 handler로 전달하지 않고 heartbeat pong을
자동으로 응답해야 한다.

Heartbeat timeout은 아래 조건에서 발생한다.

1. Connector가 heartbeat ping을 보낸다.
2. `Heartbeat.Timeout` 안에 heartbeat pong 또는 다른 inbound frame을 받지 못한다.
3. Connector는 현재 transport를 끊긴 것으로 처리한다.

다른 inbound frame도 liveness 신호로 본다. 이미 데이터가 오가고 있으면 별도 pong이
늦어도 연결이 살아 있다고 판단할 수 있기 때문이다.

## Control frame 계약

Heartbeat frame은 응용 handler에 노출하지 않는 control frame이다.
구현 단계에서는 `ZlinkStreamMessageKind.Control`을 추가하고, 아래 이름을 예약한다.

| 이름 | 의미 |
|------|------|
| `$zlink.heartbeat.ping` | heartbeat 요청 |
| `$zlink.heartbeat.pong` | heartbeat 응답 |

Control frame은 `ZlinkStreamCodec.Raw`와 빈 payload를 사용한다.
응용은 `$zlink.`로 시작하는 packet name을 직접 사용하면 안 된다.
Connector는 이 prefix를 내부 예약 영역으로 검증해야 한다.

## Reconnect 계약

자동 reconnect는 다음 상황에서 시작한다.

- heartbeat timeout
- receive loop에서 transport close 감지
- receive loop에서 decode나 transport 오류 발생
- send 중 transport 오류 발생

Reconnect가 꺼져 있으면 Connector는 `Disconnected` 상태로 들어가고 더 이상 자동 동작을
하지 않는다. 호출자가 다시 `ConnectAsync()`를 호출할 수 있다.

Reconnect가 켜져 있으면 Connector는 `Reconnecting` 상태로 들어가고, 같은 `Endpoint`로
다시 연결을 시도한다. delay는 `InitialDelay`에서 시작해 실패할 때마다
`BackoffFactor`를 곱하되 `MaxDelay`를 넘지 않는다.

`MaxAttempts == null`이면 `CloseAsync()` 또는 `DisposeAsync()`가 호출될 때까지 계속
시도한다. `MaxAttempts`에 도달하면 Connector는 `Disconnected` 상태로 끝난다.

초기 `ConnectAsync()` 실패는 자동 reconnect 대상이 아니다.
초기 연결 실패는 `ConnectAsync()` 호출자에게 예외로 반환한다. 자동 reconnect는 한 번
이상 `Connected`가 된 뒤 끊긴 연결을 복구하는 기능이다.

## Pending request 처리

연결이 끊긴 순간 아직 완료되지 않은 request는 모두 실패한다.
reconnect 뒤에 같은 request를 자동 재전송하지 않는다.

이 규칙은 중복 요청을 막기 위한 것이다. Connector는 서버가 요청을 이미 처리했는지
알 수 없으므로, 자동 재전송을 하면 같은 명령이 두 번 실행될 수 있다.
재시도 여부는 응용이 요청 의미를 알고 결정해야 한다.

실패 코드는 `ZlinkStreamErrorCode.Disconnected`를 사용한다.
heartbeat timeout으로 끊긴 경우에는 error message에 timeout 원인을 포함한다.

## Handler와 callback 유지

`On(...)`으로 등록한 handler는 reconnect 중에도 유지된다.
handler 등록은 Connector 객체의 생명주기에 속하고, transport 연결 하나에 속하지 않는다.

`ErrorReceived`와 `Disconnected` event 등록도 유지된다.
reconnect가 성공해도 handler를 다시 등록할 필요가 없다.

## Event 계약

기존 event는 아래 의미를 가진다.

```csharp
event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

event Func<CancellationToken, ValueTask>? Disconnected;
```

`ErrorReceived`는 decode 실패, heartbeat timeout, reconnect 실패 같은 오류를 알린다.
`Disconnected`는 활성 transport가 사라질 때마다 한 번 호출된다.
자동 reconnect가 켜져 있어도 끊김 자체는 응용이 알아야 하므로 `Disconnected`를 호출한다.

상태 변화를 더 세밀하게 관찰해야 하면 별도 event를 추가한다.

```csharp
event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? ConnectionStateChanged;
```

이 event는 구현 선택 사항이 아니라 reconnect를 공개 기능으로 제공한다면 함께 제공해야 하는
계약이다. `Disconnected`만으로는 `Reconnecting`과 최종 `Disconnected`를 구분하기 어렵기
때문이다.

## Close와 dispose 계약

`CloseAsync()`는 현재 연결과 진행 중인 reconnect를 모두 중단한다.
호출이 끝난 뒤 `IsConnected`는 `false`다.

`DisposeAsync()`는 `CloseAsync()`를 포함한다.
dispose 뒤에는 `ConnectAsync()`, `Send()`, `Request()`, `On()`을 호출하면
`ObjectDisposedException` 또는 Connector 오류로 실패한다.

`CloseAsync()`로 닫은 Connector를 다시 연결할 수 있을지는 구현 단계에서 하나로 정해야 한다.
이 초안은 단순한 계약을 위해 `CloseAsync()` 뒤 재사용을 허용하지 않는 쪽을 권장한다.
재사용이 필요하면 `DisconnectAsync()`와 `CloseAsync()`를 별도로 두어야 한다.

## 서버 요구사항

Heartbeat를 켠 client와 통신하는 서버는 heartbeat control frame을 처리해야 한다.
서버가 control frame을 모르면 ping packet이 사용자 handler로 흘러가거나 무시되어
client가 timeout을 발생시킬 수 있다.

서버 쪽 Connector 또는 Framework stream runtime은 다음을 보장해야 한다.

- heartbeat ping을 사용자 handler로 전달하지 않는다.
- heartbeat ping을 받으면 heartbeat pong을 자동으로 보낸다.
- heartbeat pong을 사용자 handler로 전달하지 않는다.
- `$zlink.` 예약 packet name을 사용자 packet name으로 허용하지 않는다.

## 검증 항목

구현이 끝나면 최소한 아래 동작을 테스트해야 한다.

| 항목 | 기대 결과 |
|------|-----------|
| `Create()`는 연결하지 않는다 | handler 등록 후 `ConnectAsync()`를 호출할 수 있다 |
| heartbeat ping/pong 성공 | 연결이 유지되고 사용자 handler가 control frame을 받지 않는다 |
| heartbeat timeout | pending request가 `Disconnected`로 실패한다 |
| reconnect disabled | timeout 뒤 `Disconnected` 상태로 남는다 |
| reconnect enabled | timeout 뒤 backoff에 따라 재연결을 시도한다 |
| reconnect 성공 | 기존 handler로 새 packet을 받을 수 있다 |
| reconnect 중 send | 활성 transport가 없으면 실패한다 |
| reconnect 중 close | 추가 reconnect 시도를 중단한다 |
| `MaxAttempts` 초과 | 최종 `Disconnected` 상태가 된다 |
| 서버 예약 이름 검증 | `$zlink.` packet name 등록 또는 전송이 실패한다 |

## 열린 결정

아래 항목은 구현 전에 결정해야 한다.

1. `CloseAsync()` 뒤 같은 Connector를 다시 `ConnectAsync()`할 수 있게 할지 여부.
2. `ConnectionStateChanged` event의 정확한 payload 형식.
3. heartbeat control frame에 `RequestSeq`를 사용할지 여부.
4. WebSocket transport keep-alive와 응용 heartbeat를 동시에 켰을 때 기본값.
5. reconnect 실패가 `ErrorReceived`로만 전달될지, 최종 예외를 저장해 조회할 수 있을지.
