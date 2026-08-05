[한국어](README.ko.md) | English

[.NET binding spec](../../spec/dotnet/README.en.md) · [.NET binding guide](../../guide/dotnet/index.en.md)

# .NET bindings reference

The writing rules follow the
[Reference-writing guide](../../../../doc/principal/documentation/reference-writing-guide.ko.md)
(Korean-only). This is the bindings layer (the Core C ABI's language projection) — not the
framework layer (`Systems.Zlink.Framework`), which already has its own reference tree under
`framework/doc/framework/dotnet/reference/`.

Categories follow the "Contract folder layout" the
[.NET binding spec](../../spec/dotnet/README.en.md#contract-folder-layout) defines
(`Contracts/Core`, `Contracts/Messaging`, `Contracts/Sockets`, `Contracts/Eventing`,
`Contracts/Service`, `Contracts/Errors`) verbatim — that spec is also the parity-reference lane
every other wrapper binding (cpp/java/node/rust) aligns its own contract categories to, so this
category order and split carries over unchanged when those languages' reference trees are
written.

## Locale convention

Every `bindings/doc/spec/<lang>/` document is English-original, Korean-translation (unlike the
framework's interface-catalog convention). This reference tree follows the same direction: write
`.en.md` first, `.ko.md` second, and every spec citation links to the same-locale spec file.

## Category

| Category | Status | Contract source |
|---|---|---|
| [Core](01-core.en.md) | Drafted | `Contracts/Core/` (`Context.cs`, `ContextOptions.cs`, `RoutingId.cs`, `Zlink.cs`) |
| Messaging | Not started | `Contracts/Messaging/` (`Message.cs`, `Received.cs`, `TopicMessage.cs`, `SubscriptionEvent.cs`, `OperationContracts.cs`) |
| Sockets | Not started | `Contracts/Sockets/` (`ISocket.cs`, `MessageSocketContracts.cs`, `RoutedSocketContracts.cs`, `PubSubSocketContracts.cs`, `IStreamSocket.cs`, `SocketOptionFacades.cs`) |
| Eventing | Not started | `Contracts/Eventing/` (`Monitor.cs`, `Poller.cs`, `PollEvent.cs`, `Timer.cs`, `ZlinkPoll.cs`) |
| Service | Not started | `Contracts/Service/` (`SpotNode.cs`, `Spot.cs`, `Actor.cs`, `SpotNodeModels.cs`) |
| Errors | Not started | `Contracts/Errors/` (`Errors.cs`) |

This document tree is not yet listed in the `mkdocs.yml` nav.
