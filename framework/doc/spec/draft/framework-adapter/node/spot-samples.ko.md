[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [SPOT](./nestjs-spot.ko.md)

# Draft -- ZLink Framework Node.js SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `SPOT` 초안을 샘플로 보기 위한 문서다.

```ts
@Injectable()
export class StageHandlers {
  @ZLinkSpotRequest()
  async getStageState(
    request: GetStageStateRequest,
    context: ZLinkSpotRequestContext,
  ): Promise<GetStageStateReply> {
    return new GetStageStateReply(context.self.spotRid, 10);
  }

  @ZLinkSpotSubscription('stage.state.updated')
  async onStageState(
    event: StageStateUpdated,
    context: ZLinkSpotSubscriptionContext,
  ): Promise<void> {
  }
}
```

다른 channel 호출은 `spotClient.requestChannel('profile', ...)` 같은 표면으로
설명하는 편이 맞다.
