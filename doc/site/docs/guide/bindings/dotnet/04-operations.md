[← 서비스](./03-services.md) · [.NET 가이드](./index.md) · [다음: 레퍼런스 →](./05-reference.md)

# 운영 — 옵션 · TLS · 모니터링 · 폴러/타이머 · 스레딩 · 네이티브

이 문서는 소켓을 실제로 운영할 때 필요한 설정·관찰·동시성·배포 표면을 모읍니다.
각 항목의 개념은 코어 가이드의 해당 챕터가 소유합니다.

---

## 소켓 옵션

`socket.Options`로 공통 옵션을, 소켓별 `Options`로 전용 옵션을 설정합니다. 전체
의미는 [소켓 옵션](../../12-socket-options.md)을 참고하세요.

```csharp
var o = socket.Options;                          // CommonSocketOptions
o.SendHighWaterMark    = 10_000;                 // 송신 큐 상한
o.ReceiveHighWaterMark = 10_000;
o.Linger               = TimeSpan.Zero;          // 종료 시 미전송 폐기
o.ReceiveTimeout       = TimeSpan.FromSeconds(5);
o.SendTimeout          = TimeSpan.FromSeconds(5);
o.TcpNoDelay           = true;
o.HeartbeatInterval    = TimeSpan.FromSeconds(10);
string last = o.LastEndpoint;                    // 바인딩된 실제 주소(읽기)
```

자주 쓰는 공통 옵션:

| 범주 | 속성 |
|---|---|
| 큐/버퍼 | `SendHighWaterMark`, `ReceiveHighWaterMark`, `SendBufferSize`, `ReceiveBufferSize`, `MaxMessageSize` |
| 타임아웃 | `ReceiveTimeout`, `SendTimeout`, `ConnectTimeout`, `Linger`, `ReconnectInterval`, `ReconnectIntervalMax` |
| 네트워킹 | `Backlog`, `TcpKeepAlive`, `TcpNoDelay`, `IPv6`, `Immediate` |
| 하트비트 | `HeartbeatInterval`, `HeartbeatTtl`, `HeartbeatTimeout` |

소켓별 전용 옵션(예): `RouterSocketOptions.Mandatory`/`Handover`,
`DealerSocketOptions.RequestTimeout`, `PubSocketOptions.NoDrop`/`WelcomeMessage`.

---

## TLS 보안

`Bind`/`Connect` **전에** 설정합니다. 자세한 내용은
[TLS 보안](../../05-tls-security.md)을 참고하세요.

```csharp
// 서버
server.SetTlsServer("server.crt", "server.key", requireClientCert: false);
server.Bind("tls://0.0.0.0:5561");

// 클라이언트
client.SetTlsClient("ca.crt", "server.example.com", trustSystem: false);
client.Connect("tls://server.example.com:5561");
```

---

## 모니터링 (Monitor)

소켓의 연결 수명 이벤트(연결됨·끊김·핸드셰이크 실패 등)를 받습니다.

```csharp
using var monitor = socket.MonitorOpen(SocketEvent.ConnectionReady | SocketEvent.Disconnected);

// 블로킹 수신
MonitorEvent? ev = monitor.Recv();
if (ev is { Event: MonitorEventType.ConnectionReady })
    Console.WriteLine($"연결됨: {ev.RemoteAddr}");

// 논블로킹 — 이벤트 없으면 null
MonitorEvent? maybe = monitor.Recv(RecvFlags.DontWait);

// 또는 콜백
monitor.OnEvent(e => Console.WriteLine($"{e.Event} {e.RemoteAddr}"));
```

주요 이벤트 플래그: `Connected`, `ConnectionReady`, `Accepted`, `Disconnected`,
`Closed`, `BindFailed`, `HandshakeFailedAuth`, `All` 등. 개념은
[모니터링](../../06-monitoring.md) 참고.

---

## 폴러 / 타이머

### 폴러 — 여러 소켓을 한 스레드에서

소켓마다 스레드를 두지 않고 여러 소켓을 한 루프에서 다중화할 때 씁니다. 각 소켓에
`slot`(정수 태그)을 붙여 어느 소켓이 readable 한지 식별합니다.

```csharp
using var poller = Zlink.CreatePoller();
poller.Add(socketA, PollEventFlags.PollIn, slot: 0);
poller.Add(socketB, PollEventFlags.PollIn, slot: 1);

var events = new PollEvent[2];
int n = poller.Wait(events, TimeSpan.FromSeconds(5));
foreach (var e in events.AsSpan(0, n))
{
    if (e.Revents.HasFlag(PollEventFlags.PollIn))
        DrainSocket(e.Slot);                     // slot으로 어느 소켓인지 식별
}
```

타이머도 폴러에 등록할 수 있어(`poller.Add(timer, slot)`), 소켓 readable과 타이머
발화를 **한 루프에서** 함께 기다릴 수 있습니다. 간단한 일회성 폴링에는 정적 헬퍼
`ZlinkPoll.Poll(sockets, timeoutMs)`도 있습니다.

### 타이머 — 생성과 수신 모드 고르기

타이머는 `Start(interval, repeatCount)`로 켜고, `repeatCount: 0`이면 **무한 반복**,
`N`이면 N회 발화 후 멈춥니다(주기 작업은 0, 카운트다운/백오프는 N).

```csharp
using var timer = Zlink.CreateTimer();
timer.Start(TimeSpan.FromMilliseconds(100), repeatCount: 5);  // 0 = 무한
```

**생성 — 일반 vs SPOT 타이머**

| 생성 | 스케줄러 | 언제 |
|------|----------|------|
| `Zlink.CreateTimer()` | 프로세스 전역 | 일반적인 주기 작업·타임아웃 |
| `Zlink.CreateTimer(ISpot spot)` | 해당 SpotNode 공유 | SPOT 디스패치 루프 안에서 도는 타이머. 같은 노드의 타이머들이 스케줄러를 공유해 오버헤드가 준다(개념: [02-core-api §타이머](../../02-core-api.md)) |

**수신 — 세 가지 방식 (택일)**

| 방식 | 호출 | 언제 |
|------|------|------|
| pull | `ulong? n = timer.Recv()` (논블로킹: `RecvFlags.DontWait`) | 전용 루프에서 발화를 직접 당겨 처리 |
| 콜백 | `timer.OnFire((t, count) => …)` | 백그라운드 디스패치 스레드에서 처리(콜백 규칙은 아래) |
| 폴러 | `poller.Add(timer, slot)` | 소켓 수신과 타이머를 한 루프에서 함께 |

```csharp
// pull
ulong? fires = timer.Recv(RecvFlags.DontWait);   // 발화 누적 횟수, 없으면 null
// 콜백
timer.OnFire((t, count) => Console.WriteLine($"발화 {count}회"));
timer.Stop();
```

> **콜백 규칙**: `OnFire`/스트림 콜백 등 모든 콜백은 백그라운드 디스패치 스레드에서
> 돕니다. 콜백 안에서 그 핸들을 닫지 마세요. 콜백은 하나만 등록되며 다시 호출하면
> 교체됩니다. pull과 콜백을 같은 핸들에 섞지 마세요.

---

## 스레딩

`IContext`는 여러 스레드에서 공유해도 안전합니다. **소켓은 안전하지 않습니다** —
같은 소켓을 둘 이상의 스레드에서 동시에 호출하지 마세요. 스레드당 소켓 하나를
두거나, 명확한 소유권 경계를 두고 넘기세요. 전체 규칙:
[스레드 안전성](../../11-thread-safety.md).

---

## 네이티브 라이브러리

`Systems.Zlink`는 네이티브 코어를 `runtimes/<rid>/native` 아래 번들하므로 일반
빌드에서는 추가 설정이 필요 없습니다. 환경변수 `ZLINK_LIBRARY_PATH`로 로드 경로를
지정할 수 있습니다. **self-contained**/single-file/**Native AOT** 게시 시에는 대상
RID 자산이 출력에 포함되는지 확인하세요 (`dotnet publish -r <rid>`).

---

다음: [레퍼런스 — 에러 · 코덱 · C API 대응표 · API 레퍼런스 · 샘플 →](./05-reference.md)
