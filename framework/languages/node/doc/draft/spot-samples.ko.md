<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework NestJS STREAM](./nestjs-stream.ko.md) | [다음: Draft -- Node.js Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [SPOT](../spec/nestjs-spot.ko.md)

# Draft -- ZLink Framework Node.js SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `SPOT` 초안을 샘플로 보기 위한 문서다.

## 1. 등록과 Spot type

```ts
ZLinkModule.forRoot({
  spotMeshes: {
    'game.stage': {
      discovery: {
        registries: ['tcp://registry1:5551'],
      },
      nodes: {
        'stage-node': {
          router: { bind: 'tcp://0.0.0.0:9000' },
          pubSub: { pubBind: 'tcp://0.0.0.0:9001' },
          channelClients: {
            profile: {},
          },
          spotPublishers: {
            'game.stage': {},
          },
          entrySpot: GameEntrySpot,
          spotFactories: [StageSpot, RoomSpot],
        },
      },
    },
  },
});
```

Spot factory는 문자열 이름이 아니라 Spot type 기준으로 등록한다.

## 2. manager로 생성과 조회

```ts
@Injectable()
export class StageBootstrap {
  constructor(private readonly spotManager: ZLinkSpotManager) {}

  async warmup(): Promise<void> {
    const created = await this.spotManager.create(StageSpot);
    const createdWithRid = await this.spotManager.create(
      RoomSpot,
      '01HZZSPOT...',
    );
    const info = await this.spotManager.get(created.spotRid);
    const all = await this.spotManager.list();
  }
}
```

## 3. spot 객체와 timer

```ts
export class StageSpot implements ZLinkSpot {
  constructor(readonly context: ZLinkSpotContext) {}

  configure(): void {
    this.context.timers.addTimer('heartbeat', {
      periodMs: 1000,
      handler: StageHeartbeatHandler,
    });
  }
}
```

## 4. request, subscription, channel 호출

```ts
@Injectable()
export class StageHandlers {
  @ZLinkSpotRequest()
  async getStageState(
    request: GetStageStateRequest,
    context: ZLinkSpotRequestContext,
  ): Promise<GetStageStateReply> {
    const profile = await context.outbound
      .requestToChannel('profile', new GetProfileRequest(request.accountId))
      .submit<GetProfileReply>();

    return new GetStageStateReply(context.spotRid, profile.nickname);
  }

  @ZLinkSpotSubscription('stage.state.updated')
  async onStageState(
    event: StageStateUpdated,
    context: ZLinkSpotSubscriptionContext,
  ): Promise<void> {
  }
}
```

다른 channel 호출은 `context.outbound.requestToChannel('profile', ...)` 같은
Spot outbound 표면으로 설명한다.

## 5. 외부 노드에서 `SPOT` publish

```ts
@Controller('stage')
export class StagePublishController {
  constructor(
    private readonly spotPublisherClient: ZLinkSpotPublisherClient,
  ) {}

  @Post('publish')
  async publish(@Body() request: PublishStageStateHttpRequest) {
    await this.spotPublisherClient
      .publishSpot(
        'game.stage',
        'stage.state.updated',
        new StageStateUpdated(request.stageRid, request.userCount),
      )
      .submit();
  }
}
```

## 6. 회귀 테스트

이 샘플 문서는 아래 회귀 항목과 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| duplicate Spot factory type | Spot factory가 type key 기준으로 등록된다. |
| spot context channel request 경로 | channel request가 `context.outbound.requestToChannel(...).submit()`으로 호출된다. |
