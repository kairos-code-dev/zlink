# Getting Started — NestJS에 붙이기

가장 작은 시작점은 `ZLinkModule.forRoot(...)` 이다. 빈 옵션으로도 framework
runtime 과 기본 client provider 가 등록된다.

```ts
import { Module } from '@nestjs/common';
import { ZLinkModule } from '@zlink-systems/nestjs';

@Module({
  imports: [
    ZLinkModule.forRoot({
      channels: {
        api: {
          server: { bind: 'tcp://0.0.0.0:7101' },
          client: { manualConnections: ['tcp://127.0.0.1:7101'] },
        },
      },
    }),
  ],
})
export class AppModule {}
```

## 1. provider 사용

channel client 는 NestJS provider token 으로 주입한다.

```ts
import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import type { ZLinkChannelClient } from '@zlink-systems/framework';

@Injectable()
export class ProfileClient {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT)
    private readonly client: ZLinkChannelClient,
  ) {}

  getProfile(id: string) {
    return this.client
      .requestToChannel('api', { id })
      .packetName('GetProfile')
      .timeout(1000)
      .submit();
  }
}
```

## 2. 종료

`ZLinkFrameworkRuntimeHost` 는 NestJS lifecycle hook 을 가진다. NestJS 애플리케이션이
종료될 때 framework context 도 같이 정리된다.

## 회귀 테스트

`ZLinkModule.forRoot(...)` provider 노출과 lifecycle idempotency 는
`test/contract/nestjs-module.test.js` 에서 확인한다.
