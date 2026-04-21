[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md)

# Draft -- ZLink Framework NestJS Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `NestJS`에서 channel messaging을 어떤 표면으로
> 드러낼지 정리한다.

## 1. Module 등록

```ts
@Module({
  imports: [
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
    }),
  ],
})
export class AppModule {}
```

수동 연결은 아래처럼 둔다.

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
})
```

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.
같은 `profile` channel이라도 `profile.subscriber`는 별도 연결 집합으로 본다.
client manual 연결은 remote `RoutingId`를 따로 받지 않고 endpoint 집합만 관리한다.

## 2. Handler 모델

일반 `PUB/SUB` event publish는 `ZLinkEventPublisher` 같은 별도 provider로
설명하는 편이 맞다. 이 표면도 `channelName + topic` 기준으로 동작한다.

```ts
@Injectable()
export class UserHandlers {
  constructor(private readonly client: ZLinkClient) {}

  @ZLinkRequest()
  async getUser(
    request: GetUserRequest,
    context: ZLinkRequestContext,
  ): Promise<GetUserReply> {
    const account = await this.client.request<GetAccountReply>(
      'account',
      new GetAccountRequest(request.accountId),
    );

    return new GetUserReply(request.accountId, account.nickname);
  }
}
```

## 3. Dispatch 기준

- 일반 request/send dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 pending request의 reply correlation 경로다.
- 같은 capability에서 discovery와 manual을 같이 섞지 않는다.
- manual capability는 런타임 `connect`, `disconnect`, `listConnections`도 지원해야 한다.

## 4. Outbound-only 앱

local handler 없이 outbound client만 쓰는 module도 가능해야 한다.
이 경우 local `ROUTER(server)`는 열지 않고 outbound `DEALER(client)`만 만든다.
