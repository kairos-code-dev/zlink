한국어 | [English](README.en.md)

[.NET 바인딩 스펙](../../spec/dotnet/README.ko.md) · [.NET 바인딩 가이드](../../guide/dotnet/index.ko.md)

# .NET bindings 레퍼런스

작성 규칙은 [레퍼런스 문서 작성 가이드](../../../../doc/principal/documentation/reference-writing-guide.ko.md)를
따른다. 이는 bindings 계층(Core C ABI의 언어별 투영)이다 — framework 계층(`Systems.Zlink.Framework`)이
아니다. Framework는 `framework/doc/framework/dotnet/reference/`에 이미 자신의 레퍼런스 트리가
있다.

Category는 [.NET 바인딩 스펙](../../spec/dotnet/README.ko.md)의 "Contract 폴더 레이아웃" 절이
정의하는 "Contract folder layout"(`Contracts/Core`, `Contracts/Messaging`, `Contracts/Sockets`,
`Contracts/Eventing`, `Contracts/Service`, `Contracts/Errors`)을 그대로 따른다 — 그 스펙은
다른 모든 wrapper binding(cpp/java/node/rust)이 자신의 contract category를 맞추는 parity
참조 lane이기도 하므로, 이 category 순서와 분할은 그 언어들의 레퍼런스 트리를 작성할 때도
변경 없이 이어진다.

## 로케일 관례

`bindings/doc/spec/<lang>/`의 모든 문서는 English 원본, Korean 번역이다(framework의
interface-catalog 관례와 반대). 이 레퍼런스 트리도 같은 방향을 따른다 — `.en.md`를 먼저,
`.ko.md`를 나중에 쓰고, 모든 spec 인용은 같은 로케일의 spec 파일을 가리킨다.

## Category

| Category | 상태 | Contract 원본 |
|---|---|---|
| [Core](01-core.ko.md) | 작성 완료 | `Contracts/Core/`(`Context.cs`, `ContextOptions.cs`, `RoutingId.cs`, `Zlink.cs`) |
| Messaging | 미착수 | `Contracts/Messaging/`(`Message.cs`, `Received.cs`, `TopicMessage.cs`, `SubscriptionEvent.cs`, `OperationContracts.cs`) |
| Sockets | 미착수 | `Contracts/Sockets/`(`ISocket.cs`, `MessageSocketContracts.cs`, `RoutedSocketContracts.cs`, `PubSubSocketContracts.cs`, `IStreamSocket.cs`, `SocketOptionFacades.cs`) |
| Eventing | 미착수 | `Contracts/Eventing/`(`Monitor.cs`, `Poller.cs`, `PollEvent.cs`, `Timer.cs`, `ZlinkPoll.cs`) |
| Service | 미착수 | `Contracts/Service/`(`SpotNode.cs`, `Spot.cs`, `Actor.cs`, `SpotNodeModels.cs`) |
| Errors | 미착수 | `Contracts/Errors/`(`Errors.cs`) |

이 문서 트리는 아직 `mkdocs.yml` nav에 올리지 않았다.
