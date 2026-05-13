<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Python Channel Messaging Samples](channel-messaging-samples.ko.md) | [다음: ZLink Framework Python STREAM Samples](stream-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [SPOT](./fastapi-spot.ko.md)

# Draft -- ZLink Framework Python SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python` `SPOT` 초안을 샘플로 보기 위한 문서다.

## 1. 등록과 `spot_name`

```python
add_zlink_framework(
    app,
    spot_discovery={
        "game.stage": ["tcp://registry1:5551"],
    },
    spot_nodes={
        "stage-node": SpotNodeOptions(
            bind="tcp://0.0.0.0:9000",
            router=SpotRouterCapabilityOptions(),
            pub_sub=SpotPubSubCapabilityOptions(),
            channel_clients={
                "profile": SpotChannelClientCapabilityOptions(),
            },
            spot_publishers={
                "game.stage": SpotPublisherClientCapabilityOptions(),
            },
            spot_factories=[
                SpotFactoryEntry(spot_name="stage", spot_type=StageSpot),
                SpotFactoryEntry(spot_name="room", spot_type=RoomSpot),
            ],
        ),
    },
)
```

## 2. manager로 생성과 조회

```python
class StageBootstrap:
    def __init__(self, spot_manager: ZLinkSpotManager) -> None:
        self._spot_manager = spot_manager

    async def warmup(self) -> None:
        created = await self._spot_manager.create("stage")
        created_with_rid = await self._spot_manager.create(
            "room",
            "01HZZSPOT...",
        )
        info = await self._spot_manager.get(created.spot_rid)
        all_spots = await self._spot_manager.list()
```

## 3. spot 객체와 timer

```python
class StageSpot(ZLinkSpot):
    def __init__(self, spot_rid: str) -> None:
        self.spot_rid = spot_rid

    async def initialize(self) -> None:
        await self.add_timer("heartbeat", 1.0, StageHeartbeatHandler)
```

## 4. request, subscription, channel 호출

```python
class StageHandlers:
    def __init__(self, spot_client: ZLinkSpotClient) -> None:
        self._spot_client = spot_client

    @zlink_spot_request()
    async def get_stage_state(
        self,
        request: GetStageStateRequest,
        context: ZLinkSpotRequestContext,
    ) -> GetStageStateReply:
        profile = await self._spot_client.request_channel(
            "profile",
            GetProfileRequest(request.account_id),
        )
        return GetStageStateReply(context.self.spot_rid, profile.nickname)

    @zlink_spot_subscription(topic="stage.state.updated")
    async def on_stage_state(
        self,
        event: StageStateUpdated,
        context: ZLinkSpotSubscriptionContext,
    ) -> None:
        return None
```

## 5. 외부 노드에서 `SPOT` publish

```python
@app.post("/stage/publish")
async def publish_stage_state(
    request: PublishStageStateHttpRequest,
    spot_publisher_client: ZLinkSpotPublisherClient = Depends(
        get_spot_publisher_client
    ),
) -> None:
    await spot_publisher_client.publish(
        "game.stage",
        "stage.state.updated",
        StageStateUpdated(request.stage_rid, request.user_count),
    )
```
