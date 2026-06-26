# 기능 맵 — .NET에서 Node로

이 장은 .NET guide 의 기능 이름을 Node/NestJS 표면으로 옮긴다.

| .NET | Node/NestJS |
|------|-------------|
| `AddZLinkFramework(...)` | `ZLinkModule.forRoot(...)` |
| `AddZLinkRegistry(...)` | `ZLinkRegistryModule.forRoot(...)` |
| `IZLinkChannelClient` | `ZLinkChannelClient` |
| `IZLinkFanoutClient` | `ZLinkFanoutClient` |
| `IZLinkSpotManager` | `ZLinkSpotManager` |
| `YieldAsync(...)` | `yieldSubmit(...)` |
| `IZLinkActorManager` | `ZLinkActorManager` |
| `IZLinkRegistryQuery` | `ZLinkRegistryQuery` |
| `IZLinkRegistryQueryClient` | `ZLinkRegistryQueryClient` |
| `IZLinkRuntimeEventHandler<T>` | `ZLinkRuntimeEventHandler<T>` |
| `Systems.Zlink.Stream.Connector` | `@zlink-systems/stream-connector` |

Node 쪽 타입 이름은 TypeScript 관례를 따른다. connector 패키지는 dotnet 과 같이
client 전용이므로 framework runtime codec registry 와 섞지 않는다.

## 회귀 테스트

계약 타입 export 는 `test/contract/contract-surface.test.js` 에서 확인한다.
