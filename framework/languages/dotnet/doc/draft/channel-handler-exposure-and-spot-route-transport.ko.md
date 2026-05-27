# Draft -- channel handler exposure and Spot route transport

이 문서는 구현 전 검토에 사용했던 초안을 보관하기 위한 자리다. 현재 공개 계약은
정식 spec 문서와 `Zlink.Framework.Contracts` 소스가 기준이다.

현재 결정은 다음과 같다.

- channel handler 노출은 handler group 과 typed handler 등록으로 처리한다.
- `IZLinkChannelClient`, `IZLinkFanoutClient`, `IZLinkRouteClient` 는 public DI client 로 유지한다.
- route client 의 send/request 종결자는 channel client 와 같은 `IZLinkSendCall`,
  `IZLinkRequestCall` 을 사용한다.
- current Spot callback 안의 outbound 는 `spot.Context.Outbound` 로 접근한다.
- current Spot 이 없는 코드에는 target Spot 으로 직접 send/request 하는 별도 public client 를
  두지 않는다. actor 생성 또는 Entry Spot join 으로 `ActorRef` 를 얻고, session 이 필요하면
  session actor handle 로 bind 하는 흐름을 사용한다.

세부 계약은 아래 문서를 따른다.

- [`../spec/handler-interfaces.ko.md`](../spec/handler-interfaces.ko.md)
- [`../spec/aspnet-core-spot.ko.md`](../spec/aspnet-core-spot.ko.md)
- [`../guide/05-spot.ko.md`](../guide/05-spot.ko.md)

## 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ChannelContracts.Route_client_addresses_a_target_node_through_a_router_channel` | route client 가 공통 send/request call 표면을 사용한다. |
| `SpotContracts.Spot_outbound_context_exposes_all_routed_channel_and_publish_methods` | Spot outbound 가 current Spot callback 안에서만 필요한 outbound 표면을 제공한다. |
| `HandlerExposureTests.RemovedSpotEgressClient_PublicSurface_IsRemoved` | current Spot 밖 target Spot 직접 호출 client 가 public contract 에 노출되지 않는다. |
