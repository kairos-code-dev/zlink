[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [channel](./nestjs-channel-messaging.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Node.js Channel Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` channel messaging 초안을 코드 흐름으로
> 보기 위한 샘플 문서다.

## 1. 자동 연결

```ts
ZLinkModule.forRoot({
  channels: {
    api: {
      server: {},
    },
    profile: {
      client: {},
    },
    account: {
      client: {},
    },
  },
  discovery: {
    registries: ['tcp://registry1:5551', 'tcp://registry2:5551'],
  },
});
```

## 1.1 수동 연결과 런타임 제어

```ts
ZLinkModule.forRoot({
  channels: {
    profile: {
      client: {
        manualConnections: [
          'tcp://10.0.10.15:7101',
        ],
      },
    },
  },
});
```

```ts
@Injectable()
export class WarmupService {
  constructor(
    private readonly connections: ZLinkChannelConnectionManager,
  ) {}

  warmup() {
    this.connections
      .getClient('profile')
      .connect('tcp://10.0.10.17:7101');
  }
}
```

## 2. Controller 안에서 호출

```ts
@Controller('profiles')
export class ProfileController {
  constructor(private readonly client: ZLinkClient) {}

  @Post('get')
  async get(@Body() request: GetProfileHttpRequest) {
    return this.client.request<GetProfileReply>(
      'profile',
      new GetProfileRequest(request.accountId),
    );
  }
}
```

## 3. Options 예시

```ts
await client.send(
  'profile',
  new RefreshProfileCacheCommand(accountId),
  { packetName: 'profile.refresh-cache' },
);
```

기본은 payload 타입 이름이고, `packetName`은 override 용도다.

## 4. 일반 event publish

```ts
await eventPublisher.publish(
  'profile',
  'profile.cache-refreshed',
  new ProfileCacheRefreshed(accountId),
  { packetName: 'profile.cache-refreshed' },
);
```
