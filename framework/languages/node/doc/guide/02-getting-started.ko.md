# Getting Started — NestJS에 붙이기

가장 작은 시작점은 `ZLinkModule.forRoot(...)` 이다. 빈 옵션으로도 framework
runtime 과 기본 client provider 가 등록된다.
`@zlink-systems/nestjs` 는 `@nestjs/common` 의 실제 `DynamicModule`, provider,
lifecycle hook 을 사용한다. 따라서 아래 모듈은 NestJS 애플리케이션 컨텍스트 안에서
생성되고, provider 주입도 NestJS DI 컨테이너가 처리한다.

```ts
import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';

@Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .clientServerChannel('api', (channel) => channel
          .server('tcp://0.0.0.0:7101')
          .client('tcp://127.0.0.1:7101'))
        .build()
    ),
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

## 2. handler group

handler 는 NestJS provider 로 등록한다. ZLink 쪽에서는 `zlinkHandlers(...)` 으로
provider 를 논리 그룹에 묶고, channel options 의 `handlerGroups` 로 그 group 을
선택한다. handler class 에 ZLink decorator 를 붙이지 않아도 NestJS DI 로 생성된
provider 인스턴스의 `handle(...)` 이 호출된다.

`main.ts` 에서 handler 나 service 를 직접 `new` 로 조립하지 않는다. channel 에서
받을 node handler 만 `zlinkHandlers(...)` 에 넣고, handler class 자체는
NestJS `providers` 로 등록한다. Spot, Entry Spot, actor factory, stream session,
Spot 내부 handler 도 같은 원칙을 따른다. 다만 Spot 내부 handler 는 channel group
이 아니라 해당 Spot 또는 Entry Spot 의 registry 에서 handler type 으로 등록한다.

```ts
import { Injectable } from '@nestjs/common';
import { ZLinkModule, zlinkFramework, zlinkHandlers } from '@zlink-systems/nestjs';

@Injectable()
export class GetProfileHandler {
  handle(request: { id: string }) {
    return { id: request.id };
  }
}

@Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .clientServerChannel('api', (channel) => channel
          .server('tcp://0.0.0.0:7101')
          .handlerGroup('api'))
        .build()
    ),
  ],
  providers: [
    ...zlinkHandlers('api')
      .request(GetProfileHandler, 'GetProfile')
      .providers(),
  ],
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
