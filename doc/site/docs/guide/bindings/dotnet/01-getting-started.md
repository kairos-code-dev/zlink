[.NET 가이드](./index.md) · [다음: 메시징 →](./02-messaging.md)

# 시작하기

이 문서는 설치부터 첫 메시지 송수신, 그리고 모든 기능이 공유하는 핵심 타입과
소유권 규칙까지 다룹니다. 메시징 개념 자체는 [코어 가이드](../../01-overview.md)가
소유하며, 여기서는 .NET 표면만 설명합니다.

---

## 설치

단일 NuGet 패키지 **`Systems.Zlink`** 로 제공되며, 네이티브 코어가 함께 번들됩니다.

```bash
dotnet add package Systems.Zlink
```

- **.NET 8.0** 이상 (`net8.0`).
- 네이티브 설치 불필요 — RID별 바이너리를 자동 로드합니다.
  ([네이티브 라이브러리](./04-operations.md#네이티브-라이브러리) 참고)

```csharp
using Systems.Zlink;   // 모든 공개 API는 이 네임스페이스에 있습니다
```

---

## 5분 예제 — PING/ACK

`Pair` 소켓으로 한쪽이 `PING`을 보내고 다른 쪽이 `ACK`로 답하는 최소 예제입니다.
서버는 bind, 클라이언트는 connect 합니다.

```csharp
// 서버
using var ctx = Zlink.CreateContext();
using var server = ctx.CreatePairSocket();
using var mon = server.MonitorOpen(SocketEvent.ConnectionReady);
server.Bind("tcp://127.0.0.1:5555");
mon.Recv();   // 연결될 때까지 대기

using var received = Received.Create();
server.Recv(received);
Console.WriteLine(received.FirstPart().GetString());   // PING

using var reply = Message.From("ACK");
server.Send().Message(reply).Submit();
```

```csharp
// 클라이언트
using var ctx = Zlink.CreateContext();
using var client = ctx.CreatePairSocket();
using var mon = client.MonitorOpen(SocketEvent.ConnectionReady);
client.Connect("tcp://127.0.0.1:5555");
mon.Recv();

using var ping = Message.From("PING");
client.Send().Message(ping).Submit();

using var received = Received.Create();
client.Recv(received);
Console.WriteLine(received.FirstPart().GetString());   // ACK
```

`Pair`는 가장 단순한 패턴(피어 1:1)이라 첫 프로그램에 적합합니다. 확장 가능한
요청/응답·팬아웃·파이프라인은 [메시징](./02-messaging.md)을 참고하세요.

---

## 핵심 타입

모든 기능이 공유하는 4가지 기본 타입입니다.

### 1. 컨텍스트 (Context)

프로세스의 런타임 진입점입니다. 보통 하나만 만들고 모든 소켓·서비스를 여기서
생성합니다.

```csharp
using var ctx = Zlink.CreateContext();
ctx.Options.IoThreads  = 4;     // I/O 스레드 수
ctx.Options.MaxSockets = 1024;  // 최대 소켓 수
// 옵션은 소켓을 만들기 전에 설정하세요.
```

`IContext`는 `IDisposable`/`IAsyncDisposable`입니다. 종료 시 `Shutdown()`으로
진행 중인 작업을 멈출 수 있고, `using`으로 자동 해제됩니다.

### 2. 메시지 (Message)

하나의 페이로드 프레임입니다. 문자열·바이트·미리 할당 버퍼로 만들 수 있습니다.

```csharp
byte[] buffer = GetPayload();

using var fromText  = Message.From("payload");      // 문자열(UTF-8)
using var fromBytes = Message.From(buffer);         // byte[] / ReadOnlySpan<byte> 복사
using var sized     = new Message(1024);            // 미리 할당 후 AsSpan()에 채움

int    size = fromText.Size;
string text = fromText.GetString();                  // UTF-8 디코딩
ReadOnlySpan<byte> view = fromText.AsReadOnlySpan();  // 복사 없이 읽기
byte[] copy             = fromText.ToArray();         // 복사해서 꺼내기
```

`Message`는 네이티브 저장소를 소유하므로 `IDisposable`입니다. `AsSpan()` /
`AsReadOnlySpan()`이 주는 span은 메시지가 살아있는 동안만 유효합니다. 메시지 모델
개념은 [메시지 API](../../09-message-api.md)를 참고하세요.

### 3. 수신 (Received)

수신 결과를 담는 **재사용 가능한 봉투**입니다. 핫 패스에서 한 번 만들어
`Recv(...)` 루프에서 재사용하면 할당이 사라집니다.

```csharp
using var received = Received.Create();
socket.Recv(received);

Message      first = received.FirstPart();   // 첫 파트(소유권 이전 없음)
string       body  = first.GetString();
RoutingId?   from  = received.RoutingId;     // 라우팅 경로가 있으면
ulong?       seq   = received.RequestSeq;    // 요청/응답이면
IReadOnlyList<Message> parts = received.Parts;  // 멀티파트 전체
```

### 4. 라우팅 ID (RoutingId)

피어·스팟·액터를 식별하는 바이너리 안전 값 타입입니다. 정적 팩토리로만 만듭니다.
개념과 정책은 [라우팅 ID](../../08-routing-id.md)를 참고하세요.

```csharp
RoutingId a = RoutingId.From("order-client");       // UTF-8 문자열
RoutingId b = RoutingId.From(0xC0FFEEu);             // uint32(빅엔디안)
RoutingId c = RoutingId.From(Guid.NewGuid());        // 16바이트 UUID
RoutingId d = RoutingId.FromHex("0a1b2c");           // 원시 hex
string    s = a.ToString();                          // 표시용 문자열
string    h = a.ToHex();                             // 원시 바이트 보존용
```

---

## 소유권과 수명 (공통 규칙)

`IContext`·소켓·`Message`·`Received`는 모두 네이티브 리소스를 감싸며
`IDisposable`(및 대부분 `IAsyncDisposable`)을 구현합니다. **만든 것은 반드시
해제하세요** — 항상 `using`(또는 `await using`).

- 소켓은 그것을 만든 컨텍스트보다 **먼저** dispose 하세요.
- `Request().SubmitAsync()`·`Join(...).SubmitAsync()`로 받은 `Message`는
  **호출자 소유**입니다 — 사용 후 dispose 하세요.
- span 보관이 필요하면 `ToArray()`/`CopyTo(...)`로 복사하세요.

스레드 안전성 규칙은 [운영 — 스레딩](./04-operations.md#스레딩)을 참고하세요.

---

다음: [메시징 — 소켓 패턴별 사용법 →](./02-messaging.md)
