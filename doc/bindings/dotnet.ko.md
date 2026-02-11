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
