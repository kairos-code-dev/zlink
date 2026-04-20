[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [SPOT](./fastapi-spot.ko.md)

# Draft -- ZLink Framework Python SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python` `SPOT` 초안을 샘플로 보기 위한 문서다.

```python
class StageHandlers:
    @zlink_spot_request()
    async def get_stage_state(
        self,
        request: GetStageStateRequest,
        context: ZLinkSpotRequestContext,
    ) -> GetStageStateReply:
        return GetStageStateReply(context.self.spot_rid, 10)

    @zlink_spot_subscription(topic="stage.state.updated")
    async def on_stage_state(
        self,
        event: StageStateUpdated,
        context: ZLinkSpotSubscriptionContext,
    ) -> None:
        return None
```
