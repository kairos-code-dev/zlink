[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [SPOT](./nestjs-spot.ko.md)

# Draft -- ZLink Framework Node.js SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` `SPOT` 초안을 샘플로 보기 위한 문서다.

## 1. 등록과 `spotName`

```ts
ZLinkModule.forRoot({
  spotDiscovery: {
    'game.stage': {
      registries: ['tcp://registry1:5551'],
    },
  },
  spotNodes: {
    'stage-node': {
      bind: 'tcp://0.0.0.0:9000',
      router: {},
      pubSub: {},
      channelClients: {
        profile: {},
      },
      spotPublishers: {
        'game.stage': {},
      },
      spotFactories: [
        { spotName: 'stage', spotType: StageSpot },
        { spotName: 'room', spotType: RoomSpot },
      ],
    },
  },
});
```

## 2. manager로 생성과 조회

```ts
@Injectable()
export class StageBootstrap {
  constructor(private readonly spotManager: ZLinkSpotManager) {}

  async warmup(): Promise<void> {
    const created = await this.spotManager.create('stage');
    const createdWithRid = await this.spotManager.create(
      'room',
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
  constructor(readonly spotRid: string) {}

  async initialize(): Promise<void> {
    await this.addTimer('heartbeat', 1000, StageHeartbeatHandler);
  }

  async addTimer(
    name: string,
    periodMs: number,
    handlerType: Function,
  ): Promise<ZLinkTimer> {
    throw new Error('sample');
  }
}
```

## 4. request, subscription, channel 호출

```ts
@Injectable()
export class StageHandlers {
  constructor(private readonly spotClient: ZLinkSpotClient) {}

  @ZLinkSpotRequest()
  async getStageState(
    request: GetStageStateRequest,
    context: ZLinkSpotRequestContext,
  ): Promise<GetStageStateReply> {
    const profile = await this.spotClient.requestChannel<GetProfileReply>(
      'profile',
      new GetProfileRequest(request.accountId),
    );

    return new GetStageStateReply(context.self.spotRid, profile.nickname);
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

## 5. 외부 노드에서 `SPOT` publish

```ts
@Controller('stage')
export class StagePublishController {
  constructor(
    private readonly spotPublisherClient: ZLinkSpotPublisherClient,
  ) {}

  @Post('publish')
  async publish(@Body() request: PublishStageStateHttpRequest) {
    await this.spotPublisherClient.publish(
      'game.stage',
      'stage.state.updated',
      new StageStateUpdated(request.stageRid, request.userCount),
    );
  }
}
```
