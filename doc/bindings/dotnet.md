[English](dotnet.md) | [한국어](dotnet.ko.md)

# .NET Binding

## 1. Overview

- .NET 8+ binding
- Aligned to the current `core/include/zlink.h` public contract
- Raw sockets use `Message`-centric `Send` / `Receive` overloads
- Socket and service monitors are split
- Service APIs are centered on `Registry`, `Discovery`, `SpotNode`, and `Spot`

## 2. Main Classes

| Class | Description |
|-------|-------------|
| `Context` | Context (IDisposable) |
| `Socket` | Socket (IDisposable) |
| `Message` | Payload convenience and ownership boundary |
| `Poller` | Event poller |
| `SocketMonitor` | Raw socket monitor |
| `Registry` | Registry bind and topology query |
| `Discovery` | Service view, metadata, and member peer query |
| `ServiceMonitor` | Discovery / SPOT monitor |
| `SpotNode` | Topology and discovery attach |
| `Spot` | Unified SPOT publish / subscribe facade |

`Spot` is created from `SpotNode`; it is not a context-owned service
constructor.

## 3. Basic Example

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

## 4. Samples

- Sample solution: `bindings/dotnet/samples/Zlink.Samples.sln`
- Run-all scripts:
  - `bindings/dotnet/samples/run_samples.sh`
  - `bindings/dotnet/samples/run_samples.ps1`
- Representative samples:
  - `PairRecv`, `PairCallback`
  - `PubSubRecv`, `PubSubCallback`
  - `DealerRouterRecv`, `DealerRouterCallback`
  - `StreamRecv`, `StreamCallback`
  - `SpotRecv`, `SpotCallback`
  - `RegistryDiscoveryMonitor`

## 5. STREAM API

STREAM-specific public names were removed. The current contract is:

- direct recv:
  - `Receive(out string routingId, out Message message)`
- callback mode:
  - `AttachStreamRaw(StreamPacketHandler handler)`
  - `DetachStream()`
- directed send:
  - `Send(string routingId, Message message, SendFlags flags = SendFlags.None)`

Do not mix callback mode with direct STREAM receive consumption.

```csharp
using var stream = new Socket(ctx, SocketType.Stream);

stream.AttachStreamRaw((routingId, payload) =>
{
    stream.Send(routingId, payload, SendFlags.None);
    return 0;
});
```

## 6. Testing

- Contract tests: `bindings/dotnet/tests/Zlink.Tests/`
- Full run:
  - `dotnet test /home/hep7/project/kairos/zlink/bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj`

## 7. Migration Notes

Breaking changes summary:

1. `Receiver`, `Timers`, and `AttachStreamLen32Be` were removed.
2. Raw socket send/recv now converge on `Message`-centric `Send` /
   `Receive` overloads instead of byte-array helpers.
3. Topic paths are expressed as `Publish` / `Subscribe` /
   `SubscribeHandler`.
4. Monitoring is split into `SocketMonitor` and `ServiceMonitor`.
5. After discovery attach, the discovery instance owns the socket lifecycle.
   Manual `Connect` / `Disconnect` / `Unbind` / close operations fail while
   attached.

NuGet packages include native runtimes under `runtimes/`.
