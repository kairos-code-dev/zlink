# Getting Started — NestJS에 붙이기

가장 작은 시작점은 `ZLinkModule.forRoot(...)` 이다. 빈 옵션으로도 framework
runtime 과 기본 client provider 가 등록된다.
`@zlink-systems/nestjs` 는 `@nestjs/common` 의 실제 `DynamicModule`, provider,
lifecycle hook 을 사용한다. 따라서 아래 모듈은 NestJS 애플리케이션 컨텍스트 안에서
생성되고, provider 주입도 NestJS DI 컨테이너가 처리한다.

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

## 2. handler annotation

handler 는 NestJS provider 로 등록하고, `@ZLinkHandlerGroup(...)` 과
`@ZLinkRequest(...)` 로 packet handler 를 표시할 수 있다. `handlerGroups` 를 channel
options 에 지정하면 `ZLinkModule` 이 NestJS provider 목록에서 해당 group 의 handler 를
찾아 request handler 로 연결한다.

```ts
import { Injectable } from '@nestjs/common';
import { ZLinkHandlerGroup, ZLinkRequest } from '@zlink-systems/framework';

@Injectable()
@ZLinkHandlerGroup('api')
export class GetProfileHandler {
  @ZLinkRequest('GetProfile')
  handle(request: { id: string }) {
    return { id: request.id };
  }
}

@Module({
  imports: [
    ZLinkModule.forRoot({
      channels: {
        api: {
          server: { bind: 'tcp://0.0.0.0:7101' },
          handlerGroups: ['api'],
        },
      },
    }),
  ],
  providers: [GetProfileHandler],
})
export class AppModule {}
```

## 3. 종료

`ZLinkFrameworkRuntimeHost` 는 NestJS lifecycle hook 으로 시작되고 닫힌다.
`NestFactory.createApplicationContext(...)` 또는 일반 NestJS 애플리케이션 부트스트랩이
끝나면 runtime 이 시작되고, `app.close()` 또는 shutdown hook 에서 framework context 도
같이 정리된다. 샘플도 직접 `new ZLinkFrameworkRuntimeHost(...)` 를 만들지 않고
NestJS application context 를 통해 runtime 과 client provider 를 받는다.

## 회귀 테스트

`ZLinkModule.forRoot(...)` provider 노출, 실제 NestJS application context 주입,
annotation handler discovery, lifecycle idempotency 는
`test/contract/nestjs-module.test.js` 에서 확인한다.
