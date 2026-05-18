# .NET Stream Connector 연결 생명주기 초안

이 문서는 릴리스 전 초안이며 현재 배포된 공개 계약이 아니다.
아래 내용은 `Systems.Zlink.Stream.Connector`의 생성, 연결, heartbeat,
reconnect, 종료 의미를 정리하기 위한 설계안이다.
정식 spec 문서와 릴리스에 반영되기 전까지 응용은 이 동작에 의존하면 안 된다.

## 목적

기존 `ZlinkStreamConnectorOptions`에는 `HeartbeatInterval`, `HeartbeatTimeout`,
`IdleTimeout` 같은 평면 옵션이 있었지만, 이 값들은 완전한 연결 생명주기 계약으로
이어지지 않았다. 특히 heartbeat timeout 뒤에 자동 reconnect를 수행한다는 공개
계약이 없었다. 이 상태에서 옵션만 공개하면 사용자는 timeout 감지와 reconnect가
이미 동작한다고 오해할 수 있다.

이 초안의 목적은 Connector가 어떤 상태를 가지고, 연결이 끊겼을 때 무엇을 보장하며,
자동 reconnect를 제공한다면 어떤 범위까지 책임지는지 명확히 정하는 것이다.

## 범위

이 문서는 .NET Stream Connector 패키지의 상위 API 계약을 기준으로 쓰지만,
reconnect 상태 전이와 delay 계산은 언어별 Connector가 동일하게 따라야 하는
공통 알고리즘으로 정의한다.
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

Connector는 아래 상태를 가진다. 상태는 공개 enum으로 노출한다.

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

```csharp
public enum ZlinkStreamConnectionState
{
    Created,
    Connecting,
    Connected,
    Reconnecting,
    Disconnected,
    Closed
}
```

`IZlinkStreamConnector`는 현재 상태를 읽을 수 있어야 한다.

```csharp
public interface IZlinkStreamConnector : IAsyncDisposable
{
    bool IsConnected { get; }

    ZlinkStreamConnectionState State { get; }
}
```

## ConnectAsync 상태별 동작

`ConnectAsync()`는 명시적인 연결 시작 API다. 같은 Connector 객체를 재사용할 수 있는
상태와 없는 상태를 아래처럼 고정한다.

| 현재 상태 | 동작 |
|-----------|------|
| `Created` | 초기 연결을 시작한다 |
| `Disconnected` | 수동 재연결을 시작한다 |
| `Connecting` | 이미 진행 중인 연결 시도가 끝날 때까지 기다린다 |
| `Connected` | 성공으로 즉시 반환한다 |
| `Reconnecting` | 자동 reconnect loop가 끝날 때까지 기다린다 |
| `Closed` | `ObjectDisposedException`으로 실패한다 |

자동 reconnect가 꺼져 있어 연결이 끊긴 Connector는 `Disconnected` 상태가 된다.
이 상태에서는 호출자가 `ConnectAsync()`를 다시 호출할 수 있다.
반면 `CloseAsync()` 또는 `DisposeAsync()`가 호출된 Connector는 `Closed` 상태가 되며
다시 연결할 수 없다. 새 연결이 필요하면 새 Connector를 만들어야 한다.

`Connecting` 또는 `Reconnecting` 상태에서 `ConnectAsync()`가 기존 작업을 기다리다가
호출자의 `cancellationToken`이 취소되면, 그 `ConnectAsync()` 호출만 취소된다.
이미 진행 중인 초기 연결이나 reconnect loop는 취소하지 않는다.

## 상태 전이

상태 전이는 아래 표를 따른다.

| 원인 | 전이 |
|------|------|
| `Create()` 성공 | 없음. 초기 상태는 `Created` |
| 초기 `ConnectAsync()` 시작 | `Created` -> `Connecting` |
| 초기 연결 성공 | `Connecting` -> `Connected` |
| 초기 연결 실패 | `Connecting` -> `Disconnected` |
| 수동 재연결 시작 | `Disconnected` -> `Connecting` |
| 연결 끊김, reconnect 꺼짐 | `Connected` -> `Disconnected` |
| 연결 끊김, reconnect 켜짐 | `Connected` -> `Reconnecting` |
| reconnect 성공 | `Reconnecting` -> `Connected` |
| reconnect 최종 실패 | `Reconnecting` -> `Disconnected` |
| `CloseAsync()` 또는 `DisposeAsync()` | 현재 상태 -> `Closed` |

`Disconnected` event는 활성 transport가 사라진 시점에 한 번 호출한다.
`Reconnecting` 상태에서 개별 reconnect attempt가 실패할 때마다 `Disconnected`를 반복 호출하지 않는다.
`CloseAsync()`가 활성 transport를 닫는 경우에도 `Disconnected` event를 한 번 호출한다.
이미 활성 transport가 없는 상태에서 `CloseAsync()`를 호출하면 `Disconnected` event를 호출하지 않는다.

## 옵션 구조

Heartbeat와 reconnect 옵션은 기본 연결 옵션에서 분리한다.
옵션 객체는 항상 존재하고 기본으로 켜져 있다. 기능을 끄려면 각 옵션의 `Enabled`를
`false`로 설정한다.

```csharp
public sealed class ZlinkStreamConnectorOptions
{
    public required Uri Endpoint { get; init; }

    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public ZlinkStreamHeartbeatOptions Heartbeat { get; init; } = new();

    public ZlinkStreamReconnectOptions Reconnect { get; init; } = new();
}

public sealed class ZlinkStreamHeartbeatOptions
{
    public bool Enabled { get; init; } = true;

    public TimeSpan Interval { get; init; } = TimeSpan.FromSeconds(1);

    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(5);
}

public sealed class ZlinkStreamReconnectOptions
{
    public bool Enabled { get; init; } = true;

    public TimeSpan InitialDelay { get; init; } = TimeSpan.FromMilliseconds(250);

    public TimeSpan MaxDelay { get; init; } = TimeSpan.FromSeconds(5);

    public double BackoffFactor { get; init; } = 2.0;

    public int? MaxAttempts { get; init; } = 3;
}
```

`Heartbeat.Enabled = false`이면 Connector는 응용 계층 heartbeat를 보내지 않는다.
`Reconnect.Enabled = false`이면 자동 reconnect를 하지 않는다.
기본값은 둘 다 켜진 상태이며, heartbeat 기본값은 1초 간격과 5초 timeout이다.
이 값은 짧은 끊김을 빠르게 감지하되, heartbeat ping 몇 번이 지연되는 상황은
바로 장애로 보지 않기 위한 기준이다.

기존 `HeartbeatInterval`, `HeartbeatTimeout`, `IdleTimeout` 같은 평면 옵션은 제거한다.
이름만 있는 옵션보다, 기능 단위의 값 객체가 어떤 기능을 켜는지 더 분명하기 때문이다.

## 기본 옵션 검증

언어별 Connector는 기본 연결 옵션에 아래 검증 규칙을 동일하게 적용해야 한다.

| 옵션 | 유효 조건 | 실패 의미 |
|------|-----------|-----------|
| `Endpoint` | null이 아니고 지원하는 scheme | 구성 오류 |
| `ConnectTimeout` | `> TimeSpan.Zero` | 0 이하이면 구성 오류 |
| `RequestTimeout` | `> TimeSpan.Zero` | 0 이하이면 구성 오류 |

## Heartbeat 옵션 검증

언어별 Connector는 `Heartbeat.Enabled = true`일 때 아래 검증 규칙을 동일하게 적용해야 한다.
`Enabled = false`이면 heartbeat timer를 만들지 않으므로 interval과 timeout은 사용하지 않는다.

| 옵션 | 유효 조건 | 실패 의미 |
|------|-----------|-----------|
| `Interval` | `> TimeSpan.Zero` | 0 이하이면 구성 오류 |
| `Timeout` | `> TimeSpan.Zero` | 0 이하이면 구성 오류 |
| `Timeout` | `> Interval` | interval 이하이면 구성 오류 |

timeout은 interval보다 길어야 한다.
같거나 더 짧으면 정상적인 timer 지연만으로도 timeout이 발생할 수 있기 때문이다.

## Heartbeat 계약

Heartbeat는 transport keep-alive와 구분되는 응용 계층 생존 확인이다.
WebSocket의 `KeepAliveInterval`이나 TCP keep-alive만으로는 `HeartbeatTimeout` 계약을
정확히 구현할 수 없으므로, Connector가 직접 control frame을 사용한다.
transport keep-alive는 연결 유지 보조 수단일 뿐이며, heartbeat timeout 판정의 기준이 아니다.

Heartbeat를 켠 Connector는 `Heartbeat.Interval`마다 heartbeat ping을 보낸다.
상대 endpoint는 heartbeat ping을 받으면 사용자 handler로 전달하지 않고 heartbeat pong을
자동으로 응답해야 한다.

Heartbeat timeout은 아래 조건에서 발생한다.

1. Connector가 `Connected` 상태로 들어간다.
2. 마지막 inbound frame 이후 `Heartbeat.Timeout` 동안 새 inbound frame을 받지 못한다.
3. Connector는 현재 transport를 끊긴 것으로 처리한다.

다른 inbound frame도 liveness 신호로 본다. 이미 데이터가 오가고 있으면 별도 pong이
늦어도 연결이 살아 있다고 판단할 수 있기 때문이다.

Heartbeat timer는 `Connected` 상태에서만 동작한다.
`Connecting`, `Reconnecting`, `Disconnected`, `Closed` 상태에서는 heartbeat ping을 보내지 않는다.
연결이 새로 성공하면 heartbeat 기준 시간은 새 transport의 연결 완료 시점으로 초기화한다.

## Heartbeat 공통 알고리즘

언어별 Connector는 heartbeat timeout을 같은 기준으로 판정해야 한다.
구현은 아래 값을 유지한다.

| 값 | 의미 |
|----|------|
| `lastInboundAt` | 마지막 inbound frame을 받은 시각 |
| `lastPingSentAt` | 마지막 heartbeat ping을 보낸 시각 |

연결이 `Connected`가 되면 두 값은 모두 연결 완료 시각으로 초기화한다.
모든 inbound frame은 control frame인지 사용자 packet인지와 관계없이 `lastInboundAt`을 갱신한다.

Heartbeat loop는 `Heartbeat.Interval`마다 아래 순서를 실행한다.

```text
now = clock.now()

if now - lastInboundAt >= Heartbeat.Timeout:
    fail connection with heartbeat timeout
    stop heartbeat loop

if now - lastPingSentAt >= Heartbeat.Interval:
    send heartbeat ping
    lastPingSentAt = now
```

timeout 검사를 ping 전송보다 먼저 한다.
이미 timeout을 넘은 연결에 새 ping을 보내면 장애 감지가 늦어지기 때문이다.

heartbeat ping 전송이 실패하면 transport 오류로 처리한다.
이 경우도 연결 끊김으로 보고 reconnect 정책을 적용한다.

## Control frame 계약

Heartbeat frame은 응용 handler에 노출하지 않는 control frame이다.
`ZlinkStreamMessageKind.Control`을 추가하고, 아래 이름을 예약한다.

| 이름 | 의미 |
|------|------|
| `$zlink.heartbeat.ping` | heartbeat 요청 |
| `$zlink.heartbeat.pong` | heartbeat 응답 |

Control frame은 `ZlinkStreamCodec.Raw`, `ZlinkStreamHeaderFlags.None`, 빈 payload를 사용한다.
응용은 `$zlink.`로 시작하는 packet name을 직접 사용하면 안 된다.
Connector는 이 prefix를 내부 예약 영역으로 검증해야 한다.

Heartbeat control frame은 아래 header 값을 사용한다.

| 필드 | 값 |
|------|----|
| `Kind` | `ZlinkStreamMessageKind.Control` |
| `Codec` | `ZlinkStreamCodec.Raw` |
| `Flags` | `ZlinkStreamHeaderFlags.None` |
| `RequestSeq` | `null` |
| `Name` | `$zlink.heartbeat.ping` 또는 `$zlink.heartbeat.pong` |
| `Metadata` | empty |
| payload | empty |

`RequestSeq`는 사용하지 않는다. heartbeat는 request-response API가 아니라
연결 생존 확인 control frame이기 때문이다.

Connector는 알 수 없는 control frame을 사용자 handler로 넘기지 않고 protocol 오류로
처리한다. 이 오류는 `ErrorReceived`로 알리고 현재 transport를 끊는다.
`Kind == Control`인데 이름이 `$zlink.`로 시작하지 않는 frame도 protocol 오류로 처리한다.
`Kind == Control`인데 `Flags`가 `None`이 아니거나 metadata, request sequence, payload가
들어 있으면 protocol 오류로 처리한다.

Connector는 `Heartbeat.Enabled = false`여도 inbound heartbeat ping에는 pong으로 응답해야 한다.
`Heartbeat.Enabled = false`는 로컬 Connector가 ping을 시작하지 않는다는 뜻이지,
peer가 보낸 control frame을 이해하지 않는다는 뜻이 아니다.

Connector는 사용자 packet name에 `$zlink.` prefix를 허용하지 않는다.
이 검증은 `Send(...).PacketName(...)`, `Request(...).PacketName(...)`, `On(...)`,
그리고 packet name resolver가 만든 기본 이름에 모두 적용한다.

## Reconnect 계약

자동 reconnect는 다음 상황에서 시작한다.

- heartbeat timeout
- receive loop에서 transport close 감지
- receive loop에서 decode나 transport 오류 발생
- send 중 transport 오류 발생

`CloseAsync()` 또는 `DisposeAsync()`로 닫은 경우에는 reconnect를 시작하지 않는다.
사용자가 명시적으로 닫은 연결은 장애가 아니라 종료 요청이기 때문이다.

send 중 transport 오류가 발생하면 해당 submit은 오류로 실패하고, Connector는 같은 오류를
연결 끊김 원인으로 사용해 lifecycle 정리를 시작한다. receive loop가 나중에 같은 끊김을
감지해도 상태 전이와 event를 중복 실행하지 않는다.

Reconnect가 꺼져 있으면 Connector는 `Disconnected` 상태로 들어가고 더 이상 자동 동작을
하지 않는다. 호출자가 다시 `ConnectAsync()`를 호출할 수 있다.

Reconnect가 켜져 있으면 Connector는 `Reconnecting` 상태로 들어가고, 같은 `Endpoint`로
다시 연결을 시도한다. delay는 `InitialDelay`에서 시작해 실패할 때마다
`BackoffFactor`를 곱하되 `MaxDelay`를 넘지 않는다.

`MaxAttempts`의 기본값은 3이다. 일반 모바일이나 PC client 연결은 장기 백그라운드
daemon 연결이 아니므로, 기본 동작은 짧게 복구를 시도한 뒤 응용이 사용자에게 재시도
선택지를 줄 수 있게 끝내는 쪽이 더 알기 쉽다.
`MaxAttempts == null`로 설정하면 `CloseAsync()` 또는 `DisposeAsync()`가 호출될 때까지
계속 시도한다. `MaxAttempts`에 도달하면 Connector는 `Disconnected` 상태로 끝난다.

초기 `ConnectAsync()` 실패는 자동 reconnect 대상이 아니다.
초기 연결 실패는 `ConnectAsync()` 호출자에게 예외로 반환한다. 자동 reconnect는 한 번
이상 `Connected`가 된 뒤 끊긴 연결을 복구하는 기능이다.

## 송신 API 상태별 동작

`Send(...)`와 `Request(...)`는 builder를 만드는 API이므로 `Closed`가 아닌 상태에서는
호출할 수 있다.
실제 transport가 필요한 시점은 `Submit()` 또는 `SubmitAsync()`다.
`Closed` 상태에서는 새 builder 생성도 실패한다. 이미 만든 builder를 close 뒤에 submit하는
경우도 실패한다.

| Submit 시점의 상태 | 동작 |
|--------------------|------|
| `Connected` | 현재 transport로 전송한다 |
| `Created` | `Disconnected` 오류로 실패한다 |
| `Connecting` | `Disconnected` 오류로 실패한다 |
| `Reconnecting` | `Disconnected` 오류로 실패한다 |
| `Disconnected` | `Disconnected` 오류로 실패한다 |
| `Closed` | `ObjectDisposedException` 또는 동일 의미의 Connector 오류로 실패한다 |

Connector는 `Reconnecting` 상태에서 submit을 내부 queue에 저장하지 않는다.
queue에 저장하면 reconnect 뒤 전송 시점이 호출자가 예상한 시점과 달라지고,
request timeout 계산도 모호해지기 때문이다.

`On(...)`은 `Closed`가 아닌 상태에서 호출할 수 있다.
`Created`, `Connecting`, `Connected`, `Reconnecting`, `Disconnected` 상태에서 등록한 handler는
같은 Connector 객체의 이후 연결에도 유지된다.

## Reconnect 공통 알고리즘

언어별 Connector는 같은 옵션 값으로 같은 reconnect 시도 순서를 만들어야 한다.
타이머 정밀도나 scheduler 차이로 실제 실행 시간이 조금 달라질 수는 있지만,
시도 횟수, delay 계산, 종료 조건은 동일해야 한다.

Reconnect 시도는 1부터 센다. `MaxAttempts == 3`이면 최대 세 번 연결을 다시 시도한다.
각 시도 전 delay는 아래 규칙으로 계산한다.

```text
attempt = 1
delay = min(InitialDelay, MaxDelay)

while connector is not closed:
    wait(delay)
    result = try_connect_once()

    if result == success:
        state = Connected
        reset attempt and delay
        stop reconnect loop

    if MaxAttempts is not null and attempt >= MaxAttempts:
        state = Disconnected
        stop reconnect loop

    attempt = attempt + 1
    delay = min(delay * BackoffFactor, MaxDelay)
```

기본값을 사용하면 delay 순서는 아래와 같다.

| 시도 | 대기 시간 |
|------|-----------|
| 1 | 250ms |
| 2 | 500ms |
| 3 | 1s |

`MaxAttempts` 기본값이 3이므로 기본 설정에서는 세 번째 실패 뒤 최종
`Disconnected` 상태가 된다.

`MaxAttempts == null`이면 같은 계산식을 계속 사용하되, delay는 `MaxDelay`에 도달한 뒤
그 값을 유지한다. 예를 들어 기본 delay 값에 `MaxAttempts == null`이면
`250ms -> 500ms -> 1s -> 2s -> 4s -> 5s -> 5s ...` 순서가 된다.

Reconnect가 성공하면 다음 연결 끊김에서 attempt와 delay는 다시 초기값으로 시작한다.
이전 reconnect 실패 횟수를 다음 장애에 이어서 사용하지 않는다.

Reconnect loop는 한 Connector에서 동시에 하나만 실행된다.
연결 끊김 이벤트가 여러 경로에서 동시에 감지되더라도 첫 경로만 reconnect loop를 시작하고,
나머지 경로는 같은 상태 전이를 중복 실행하지 않는다.

`ConnectAsync()` 호출자가 `Reconnecting` 상태를 기다리고 있을 때 reconnect가 성공하면
그 호출은 성공으로 반환한다. reconnect가 최종 실패하면 그 호출은 마지막 reconnect 실패
원인으로 실패한다. 같은 reconnect loop를 기다리는 여러 호출자는 같은 결과를 받는다.
기다리는 중 `CloseAsync()`가 호출되면 해당 `ConnectAsync()` 호출은 `ObjectDisposedException`
또는 동일 의미의 Connector 오류로 실패한다.

## Reconnect 옵션 검증

언어별 Connector는 `Reconnect.Enabled = true`일 때 아래 검증 규칙을 동일하게 적용해야 한다.
`Enabled = false`이면 reconnect loop를 만들지 않으므로 delay와 attempt 값은 사용하지 않는다.

| 옵션 | 유효 조건 | 실패 의미 |
|------|-----------|-----------|
| `InitialDelay` | `> TimeSpan.Zero` | 0 이하이면 구성 오류 |
| `MaxDelay` | `> TimeSpan.Zero` | 0 이하이면 구성 오류 |
| `BackoffFactor` | `>= 1.0` | 1보다 작으면 구성 오류 |
| `MaxAttempts` | `null` 또는 `>= 1` | 0 이하이면 구성 오류 |

`BackoffFactor == 1.0`은 모든 reconnect 시도 간격이 `InitialDelay`로 고정된다는 뜻이다.
`InitialDelay > MaxDelay`이면 첫 delay부터 `MaxDelay`로 clamp한다.

## 언어별 Connector 일관성 요구사항

언어별 Connector가 reconnect를 제공한다면 아래 계약을 모두 따라야 한다.

| 항목 | 요구사항 |
|------|----------|
| 기본값 | `InitialDelay = 250ms`, `MaxDelay = 5s`, `BackoffFactor = 2.0`, `MaxAttempts = 3` |
| 시도 번호 | 첫 reconnect 시도를 1로 센다 |
| delay 계산 | 각 실패 뒤 `min(previousDelay * BackoffFactor, MaxDelay)`를 적용한다 |
| 성공 처리 | 성공하면 attempt와 delay를 초기화한다 |
| close 처리 | `CloseAsync()` 또는 대응 API가 호출되면 reconnect loop를 즉시 중단한다 |
| pending request | 연결 끊김 시점에 모두 실패시키며 reconnect 뒤 자동 재전송하지 않는다 |
| handler 등록 | reconnect 뒤에도 유지한다 |
| 초기 연결 실패 | 자동 reconnect하지 않고 호출자에게 실패를 반환한다 |
| attempt별 connect timeout | 각 reconnect 시도마다 `ConnectTimeout`을 적용한다 |

.NET 외 언어는 `TimeSpan` 대신 각 언어의 시간 타입을 사용할 수 있다.
단위가 정수 millisecond인 언어에서는 위 기본값을 millisecond 값으로 표현한다.
delay에 `BackoffFactor`를 곱한 결과가 정수 millisecond가 아니면 올림 처리한다.
언어별 반올림 차이로 재시도 간격이 달라지는 것을 막기 위한 규칙이다.

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

event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? ConnectionStateChanged;
```

`ErrorReceived`는 decode 실패, heartbeat timeout, reconnect 실패 같은 오류를 알린다.
`Disconnected`는 활성 transport가 사라질 때마다 한 번 호출된다.
자동 reconnect가 켜져 있어도 끊김 자체는 응용이 알아야 하므로 `Disconnected`를 호출한다.

`ConnectionStateChanged`는 모든 상태 전이를 알린다.
payload는 아래 형태로 둔다.

```csharp
public sealed record ZlinkStreamConnectionStateChanged(
    ZlinkStreamConnectionState Previous,
    ZlinkStreamConnectionState Current,
    ZlinkStreamError? Error = null);
```

상태 전이가 오류 때문에 발생하면 `Error`에 원인을 담는다.
정상 연결, 정상 close처럼 오류가 없는 전이는 `Error == null`이다.

reconnect attempt 실패는 `ErrorReceived`로 알린다.
최종 attempt까지 실패해 `Disconnected`로 끝날 때는 `ConnectionStateChanged`의 `Error`에도
마지막 실패 원인을 담는다. 별도의 `LastError` 조회 API는 두지 않는다.

연결 끊김을 감지했을 때 이벤트와 정리는 아래 순서로 실행한다.

1. 활성 transport를 더 이상 사용하지 못하게 분리한다.
2. 상태를 `Reconnecting` 또는 `Disconnected`로 바꾸고 `ConnectionStateChanged`를 호출한다.
3. pending request를 `Disconnected` 오류로 실패시킨다.
4. `Disconnected` event를 호출한다.
5. reconnect가 켜져 있으면 reconnect loop를 유지하거나 시작한다.

이 순서는 handler가 `Disconnected` event 안에서 `State`를 읽을 때 이미 새 상태를
볼 수 있게 하기 위한 것이다.
reconnect loop는 상태가 `Reconnecting`으로 바뀐 뒤 하나만 존재해야 한다.
첫 reconnect attempt는 `InitialDelay`를 기다린 뒤 시작하므로, pending request 정리와
`Disconnected` event 호출을 막지 않아야 한다.

## Close와 dispose 계약

`CloseAsync()`는 현재 연결과 진행 중인 reconnect를 모두 중단한다.
호출이 끝난 뒤 상태는 `Closed`이고 `IsConnected`는 `false`다.
아직 완료되지 않은 request는 `Disconnected` 오류로 실패시키며, error message는 Connector가
닫혔다는 사실을 포함한다.

`DisposeAsync()`는 `CloseAsync()`를 포함한다.
dispose 뒤에는 `ConnectAsync()`, `Send()`, `Request()`, `On()`을 호출하면
`ObjectDisposedException` 또는 Connector 오류로 실패한다.

`CloseAsync()`로 닫은 Connector는 다시 연결할 수 없다.
일시적으로 끊었다가 같은 설정으로 다시 연결하려면 새 Connector를 만든다.
이 규칙은 `CloseAsync()`와 `DisposeAsync()`의 의미를 거의 같게 만들어 호출자가
객체 재사용 가능성을 따로 기억하지 않아도 되게 하기 위한 것이다.

`CloseAsync()`와 `DisposeAsync()`는 여러 번 호출해도 된다.
첫 호출만 실제 transport 종료와 상태 전이를 수행하고, 이후 호출은 성공으로 반환한다.

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

## 회귀 테스트

구현 뒤에는 아래 회귀 테스트를 유지해야 한다.
이 목록은 연결 생명주기 계약이 리팩토링 중에 조용히 바뀌지 않도록 막기 위한 최소 기준이다.

### 생성과 상태

| 테스트 | 확인할 내용 |
|--------|-------------|
| `Create_DoesNotConnect` | `Create()` 직후 상태는 `Created`이고 서버 연결 시도가 없다 |
| `ConnectAsync_TransitionsCreatedToConnected` | 성공한 초기 연결은 `Created -> Connecting -> Connected` 순서로 상태를 바꾼다 |
| `ConnectAsync_WhenAlreadyConnected_ReturnsSuccess` | `Connected` 상태에서 다시 호출하면 새 transport를 만들지 않고 성공한다 |
| `ConnectAsync_WhenClosed_Fails` | `Closed` 상태에서 `ConnectAsync()`는 실패한다 |
| `CloseAsync_IsIdempotent` | `CloseAsync()`를 여러 번 호출해도 첫 호출만 상태 전이를 수행하고 이후 호출은 성공한다 |
| `CloseAsync_PreventsReuse` | `CloseAsync()` 뒤 같은 Connector는 다시 연결할 수 없다 |

### 옵션 검증

| 테스트 | 확인할 내용 |
|--------|-------------|
| `Options_RejectInvalidConnectTimeout` | `ConnectTimeout <= 0`이면 구성 오류가 난다 |
| `Options_RejectInvalidRequestTimeout` | `RequestTimeout <= 0`이면 구성 오류가 난다 |
| `HeartbeatOptions_RejectInvalidInterval` | `Interval <= 0`이면 구성 오류가 난다 |
| `HeartbeatOptions_RejectInvalidTimeout` | `Timeout <= 0`이면 구성 오류가 난다 |
| `HeartbeatOptions_RejectTimeoutNotGreaterThanInterval` | `Timeout <= Interval`이면 구성 오류가 난다 |
| `ReconnectOptions_RejectInvalidInitialDelay` | `InitialDelay <= 0`이면 구성 오류가 난다 |
| `ReconnectOptions_RejectInvalidMaxDelay` | `MaxDelay <= 0`이면 구성 오류가 난다 |
| `ReconnectOptions_RejectInvalidBackoffFactor` | `BackoffFactor < 1.0`이면 구성 오류가 난다 |
| `ReconnectOptions_RejectInvalidMaxAttempts` | `MaxAttempts <= 0`이면 구성 오류가 난다 |

### Heartbeat

| 테스트 | 확인할 내용 |
|--------|-------------|
| `Heartbeat_Disabled_DoesNotSendPing` | `Heartbeat.Enabled = false`이면 Connector가 heartbeat ping을 시작하지 않는다 |
| `Heartbeat_RespondsToPingEvenWhenDisabled` | `Heartbeat.Enabled = false`여도 inbound heartbeat ping에는 pong을 보낸다 |
| `Heartbeat_SendsPingAtInterval` | `Connected` 상태에서 interval마다 heartbeat ping을 보낸다 |
| `Heartbeat_PongIsNotDispatchedToUserHandler` | heartbeat pong은 사용자 handler로 전달되지 않는다 |
| `Heartbeat_PingIsNotDispatchedToUserHandler` | heartbeat ping은 사용자 handler로 전달되지 않는다 |
| `Heartbeat_AnyInboundFrameRefreshesLiveness` | 사용자 packet을 받으면 heartbeat timeout 기준 시간이 갱신된다 |
| `Heartbeat_TimeoutDisconnectsTransport` | timeout 동안 inbound frame이 없으면 transport를 끊긴 것으로 처리한다 |
| `Heartbeat_PingSendFailureStartsDisconnectFlow` | heartbeat ping 전송 실패는 연결 끊김으로 처리된다 |

### Control frame과 예약 이름

| 테스트 | 확인할 내용 |
|--------|-------------|
| `ControlFrame_UsesReservedHeaderShape` | heartbeat control frame은 문서에 정의한 header 값과 빈 payload를 사용한다 |
| `ControlFrame_RejectsFlagsMetadataRequestSeqAndPayload` | control frame에 flags, metadata, request sequence, payload가 있으면 protocol 오류가 난다 |
| `ControlFrame_UnknownReservedNameFailsProtocol` | 알 수 없는 `$zlink.` control frame은 protocol 오류로 처리된다 |
| `ControlFrame_ControlKindWithoutReservedNameFailsProtocol` | `Kind == Control`인데 이름이 `$zlink.`로 시작하지 않으면 protocol 오류가 난다 |
| `PacketName_RejectReservedPrefixForSend` | `Send(...).PacketName("$zlink...")`는 실패한다 |
| `PacketName_RejectReservedPrefixForRequest` | `Request(...).PacketName("$zlink...")`는 실패한다 |
| `PacketName_RejectReservedPrefixForHandler` | `On("$zlink...", ...)`는 실패한다 |
| `PacketName_RejectReservedPrefixFromResolver` | packet name resolver가 `$zlink.` 이름을 만들면 전송이 실패한다 |

### Reconnect

| 테스트 | 확인할 내용 |
|--------|-------------|
| `Reconnect_Disabled_StopsAtDisconnected` | `Reconnect.Enabled = false`이면 연결 끊김 뒤 `Disconnected` 상태로 남는다 |
| `Reconnect_Enabled_TransitionsToReconnecting` | reconnect가 켜져 있으면 연결 끊김 뒤 `Reconnecting` 상태로 들어간다 |
| `Reconnect_UsesDefaultAttemptDelays` | 기본값은 `250ms -> 500ms -> 1s` 순서로 세 번 시도한다 |
| `Reconnect_ClampsInitialDelayToMaxDelay` | `InitialDelay > MaxDelay`이면 첫 delay부터 `MaxDelay`를 쓴다 |
| `Reconnect_ClampsBackoffToMaxDelay` | backoff 결과가 `MaxDelay`를 넘으면 `MaxDelay`를 유지한다 |
| `Reconnect_NullMaxAttemptsRetriesUntilClosed` | `MaxAttempts == null`이면 close 전까지 계속 시도한다 |
| `Reconnect_MaxAttemptsStopsAtDisconnected` | 최대 시도 횟수에 도달하면 최종 `Disconnected` 상태가 된다 |
| `Reconnect_SuccessResetsAttempts` | reconnect 성공 뒤 다음 끊김은 attempt와 delay를 초기값부터 다시 계산한다 |
| `Reconnect_OnlyOneLoopRuns` | 여러 오류 경로가 동시에 끊김을 감지해도 reconnect loop는 하나만 실행된다 |
| `Reconnect_AttemptUsesConnectTimeout` | 각 reconnect 시도에는 `ConnectTimeout`이 적용된다 |

### 송신과 요청

| 테스트 | 확인할 내용 |
|--------|-------------|
| `Submit_WhenCreated_FailsDisconnected` | 연결 전 submit은 `Disconnected` 오류로 실패한다 |
| `Submit_WhenReconnecting_FailsDisconnected` | reconnect 중 submit은 queue에 저장되지 않고 `Disconnected` 오류로 실패한다 |
| `Submit_WhenDisconnected_FailsDisconnected` | 최종 disconnected 상태에서 submit은 `Disconnected` 오류로 실패한다 |
| `Submit_WhenClosed_FailsDisposed` | closed 상태에서 submit은 dispose 계열 오류로 실패한다 |
| `PendingRequest_DisconnectFailsAll` | 연결 끊김 시 모든 pending request가 실패한다 |
| `PendingRequest_ReconnectDoesNotReplay` | reconnect 성공 뒤 기존 pending request를 자동 재전송하지 않는다 |

### Event와 callback

| 테스트 | 확인할 내용 |
|--------|-------------|
| `Events_DisconnectOrderMatchesSpec` | 연결 끊김 시 상태 전이, pending request 실패, `Disconnected` event 순서가 문서와 같다 |
| `Events_DisconnectedFiresOncePerLostTransport` | transport 하나가 끊길 때 `Disconnected` event는 한 번만 호출된다 |
| `Events_ReconnectAttemptFailurePublishesError` | reconnect attempt 실패는 `ErrorReceived`로 전달된다 |
| `Events_FinalReconnectFailureStateChangeCarriesError` | 최종 reconnect 실패의 상태 전이에는 마지막 오류가 포함된다 |
| `Events_CloseWithActiveTransportFiresDisconnectedOnce` | active transport가 있는 상태에서 close하면 `Disconnected` event가 한 번 호출된다 |
| `Events_CloseWithoutActiveTransportDoesNotFireDisconnected` | active transport가 없을 때 close하면 `Disconnected` event가 호출되지 않는다 |
| `Handlers_SurviveReconnect` | `On(...)`으로 등록한 handler는 reconnect 뒤에도 유지된다 |
| `Callbacks_SurviveReconnect` | error/disconnect/state callback 등록은 reconnect 뒤에도 유지된다 |

### 서버 stream runtime

| 테스트 | 확인할 내용 |
|--------|-------------|
| `Server_HeartbeatPingReceivesPong` | Framework stream runtime은 heartbeat ping을 사용자 handler로 넘기지 않고 pong을 보낸다 |
| `Server_HeartbeatPongIsNotDispatched` | Framework stream runtime은 heartbeat pong을 사용자 handler로 넘기지 않는다 |
| `Server_RejectReservedUserPacketName` | 서버 쪽 사용자 packet 송수신에서도 `$zlink.` 예약 이름은 허용하지 않는다 |

### ConnectAsync 대기

| 테스트 | 확인할 내용 |
|--------|-------------|
| `ConnectAsync_WhileConnectingWaitsForExistingAttempt` | `Connecting` 상태에서 호출한 `ConnectAsync()`는 기존 연결 결과를 기다린다 |
| `ConnectAsync_WhileReconnectingReturnsOnReconnectSuccess` | reconnect 대기 중인 `ConnectAsync()`는 reconnect 성공 시 성공으로 반환한다 |
| `ConnectAsync_WhileReconnectingFailsOnFinalFailure` | reconnect 최종 실패 시 대기 중인 `ConnectAsync()`도 같은 실패를 받는다 |
| `ConnectAsync_WaitCancellationDoesNotCancelSharedAttempt` | 대기 호출의 cancellation은 공유 연결 시도나 reconnect loop를 취소하지 않는다 |
| `ConnectAsync_WhileReconnectingFailsWhenClosed` | reconnect 대기 중 close되면 대기 호출은 dispose 계열 오류로 실패한다 |

## 구현 중 검토 방식

현재 초안은 구현에 필요한 생명주기 결정을 문서 안에서 고정한다.
구현 중 새 모호성이 발견되면 이 섹션에 보류하지 말고, 계약 본문을 수정해서
하나의 동작으로 확정한다.
