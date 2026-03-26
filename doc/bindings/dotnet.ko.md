[English](dotnet.md) | [한국어](dotnet.ko.md)

# .NET 바인딩

## 1. 개요

- .NET 8+ 바인딩
- 최신 `core/include/zlink.h` public contract 기준 정렬
- raw socket은 `Message` 중심 `Send` / `Receive` overload 사용
- socket monitor와 service monitor를 분리
- service 계층은 `Registry`, `Discovery`, `SpotNode`, `Spot` 기준으로 재구성

## 2. 주요 클래스

| 클래스 | 설명 |
|--------|------|
| `Context` | 컨텍스트 (IDisposable) |
| `Socket` | 소켓 (IDisposable) |
| `Message` | payload convenience + ownership 경계 |
| `Poller` | 이벤트 폴러 |
| `SocketMonitor` | raw socket monitor |
| `Registry` | registry bind / topology query |
| `Discovery` | service view / metadata / member peer query |
| `ServiceMonitor` | discovery / spot monitor |
| `SpotNode` | topology / discovery attach |
| `Spot` | unified SPOT publish / subscribe facade |

## 3. 기본 예제

```csharp
using var ctx = new Context();
using var server = new Socket(ctx, SocketType.Pair);
using var client = new Socket(ctx, SocketType.Pair);

server.Bind("inproc://pair-example");
client.Connect("inproc://pair-example");

using var request = Message.FromString("hello");
client.Send(request);

server.Receive(out Message received);
using (received)
{
    Console.WriteLine(received.GetString());
}
```

## 4. 샘플

- 샘플 솔루션: `bindings/dotnet/samples/Zlink.Samples.sln`
- 전체 실행 스크립트:
  - `bindings/dotnet/samples/run_samples.sh`
  - `bindings/dotnet/samples/run_samples.ps1`
- 대표 샘플:
  - `PairRecv`, `PairCallback`
  - `PubSubRecv`, `PubSubCallback`
  - `DealerRouterRecv`, `DealerRouterCallback`
  - `StreamRecv`, `StreamCallback`
  - `SpotRecv`, `SpotCallback`
  - `RegistryDiscoveryMonitor`

## 5. STREAM API

STREAM 전용 public 이름은 제거되었다. 현재 계약은 아래와 같다.

- direct recv:
  - `Receive(out string routingId, out Message message)`
- callback mode:
  - `AttachStreamRaw(StreamPacketHandler handler)`
  - `DetachStream()`
- directed send:
  - `Send(string routingId, Message message, SendFlags flags = SendFlags.None)`

콜백 attach 이후 direct recv/poll consume을 섞지 않는다.

```csharp
using var stream = new Socket(ctx, SocketType.Stream);

stream.AttachStreamRaw((routingId, payload) =>
{
    stream.Send(routingId, payload, SendFlags.None);
    return 0;
});
```

## 6. 테스트

- contract test: `bindings/dotnet/tests/Zlink.Tests/`
- 전체 실행:
  - `dotnet test /home/hep7/project/kairos/zlink/bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj`

## 7. 마이그레이션 메모

breaking change 요약:

1. `Receiver`, `Timers`, `AttachStreamLen32Be`는 제거되었다.
2. raw socket 송수신은 `byte[]` 중심 helper가 아니라 `Message` 중심 `Send` /
   `Receive` overload로 수렴했다.
3. topic path는 `Publish` / `Subscribe` / `SubscribeHandler`로 정리되었다.
4. monitor는 `SocketMonitor`와 `ServiceMonitor`로 분리되었다.
5. discovery attach 이후 socket lifecycle은 discovery 인스턴스가 소유한다.
   attach 상태에서 manual `Connect` / `Disconnect` / `Unbind` / close는 실패한다.

NuGet 패키지에는 `runtimes/` 하위 네이티브 라이브러리가 포함된다.
