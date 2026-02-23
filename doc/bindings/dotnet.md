[English](dotnet.md) | [한국어](dotnet.ko.md)

# .NET Binding

## 1. Overview

- **LibraryImport** (.NET 8+, source-generated P/Invoke)
- Resource management based on SafeHandle
- Span<byte> support

## 2. Main Classes

| Class | Description |
|-------|-------------|
| `Context` | Context (IDisposable) |
| `Socket` | Socket (IDisposable) |
| `Message` | Message |
| `Poller` | Event poller |
| `Monitor` | Monitoring |
| `ServiceDiscovery` | Service discovery |
| `Spot` | SPOT PUB/SUB |

## 3. Basic Example

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

## 4. NuGet Package

Platform-specific native libraries in the `runtimes/` directory:
- `runtimes/linux-x64/native/libzlink.so`
- `runtimes/osx-arm64/native/libzlink.dylib`
- `runtimes/win-x64/native/zlink.dll`

## 5. Testing

Uses the xUnit framework: `bindings/dotnet/tests/`

## 6. STREAM Callback API

`Socket` provides STREAM callback helpers:
- `AttachStream(StreamPacketsHandler handler, StreamDispatchMode mode = StreamDispatchMode.None)`
- `DetachStream()`
- `StreamPeerRoutingId(int index = 0)`
- `StreamSend(ReadOnlySpan<byte> routingId, ReadOnlySpan<byte> payload, SendFlags flags = SendFlags.None)`

Mode rules:
- While attached, consume STREAM payloads in the callback.
- Do not mix `Receive()`/`TryReceive()` for STREAM payload consumption while attached.
- After `DetachStream()`, you can return to normal receive calls.

```csharp
using var stream = new Socket(ctx, SocketType.Stream);

stream.AttachStream((rid, payload) =>
{
    var copy = payload.ToArray();   // explicit copy before echo
    stream.StreamSend(rid, copy, SendFlags.None);
    return 0;
}, StreamDispatchMode.Len32Be);
```
