[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [channel](./nestjs-channel-messaging.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- ZLink Framework Node.js Channel Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js` channel messaging 초안을 코드 흐름으로
> 보기 위한 샘플 문서다.

## 1. 자동 연결

```ts
ZLinkModule.forRoot({
  channelName: 'api',
  outboundChannels: ['profile', 'account'],
  discovery: {
    registries: ['tcp://registry1:5551', 'tcp://registry2:5551'],
  },
});
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
