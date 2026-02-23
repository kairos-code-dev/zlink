[English](dotnet.md) | [한국어](dotnet.ko.md)

# .NET 바인딩

## 1. 개요

- **LibraryImport** (.NET 8+, source-generated P/Invoke)
- SafeHandle 기반 리소스 관리
- Span<byte> 지원

## 2. 주요 클래스

| 클래스 | 설명 |
|--------|------|
| `Context` | 컨텍스트 (IDisposable) |
| `Socket` | 소켓 (IDisposable) |
| `Message` | 메시지 |
| `Poller` | 이벤트 폴러 |
| `Monitor` | 모니터링 |
| `ServiceDiscovery` | 서비스 디스커버리 |
| `Spot` | SPOT PUB/SUB |

## 3. 기본 예제

```csharp
using var ctx = new Context();
using var server = new Socket(ctx, SocketType.Pair);
server.Bind("tcp://*:5555");

using var client = new Socket(ctx, SocketType.Pair);
client.Connect("tcp://127.0.0.1:5555");

client.Send(Encoding.UTF8.GetBytes("Hello"));

byte[] reply = server.Recv();
Console.WriteLine(Encoding.UTF8.GetString(reply));
```

## 4. NuGet 패키지

`runtimes/` 디렉토리에 플랫폼별 네이티브 라이브러리:
- `runtimes/linux-x64/native/libzlink.so`
- `runtimes/osx-arm64/native/libzlink.dylib`
- `runtimes/win-x64/native/zlink.dll`

## 5. 테스트

xUnit 프레임워크 사용: `bindings/dotnet/tests/`

## 6. STREAM 콜백 API

`Socket`에서 STREAM 콜백 헬퍼를 제공합니다.
- `AttachStream(StreamPacketsHandler handler, StreamDispatchMode mode = StreamDispatchMode.None)`
- `DetachStream()`
- `StreamPeerRoutingId(int index = 0)`
- `StreamSend(ReadOnlySpan<byte> routingId, ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)`

모드 규칙:
- attach 상태에서는 콜백에서 STREAM 페이로드를 소비합니다.
- attach 상태에서 STREAM 페이로드 수신에 `Receive()`/`TryReceive()`를 혼용하지 않습니다.
- `DetachStream()` 이후에는 기존 receive 호출로 복귀할 수 있습니다.

```csharp
using var stream = new Socket(ctx, SocketType.Stream);

stream.AttachStream((rid, payload) =>
{
    var copy = payload.ToArray();   // echo 전 명시적 복사
    stream.StreamSend(rid, copy, SendFlags.None);
    return 0;
}, StreamDispatchMode.Len32Be);
```
