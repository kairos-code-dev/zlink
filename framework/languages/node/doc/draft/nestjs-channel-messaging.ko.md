<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Node.js Interface Catalog](./handler-interfaces.ko.md) | [다음: Draft -- ZLink Framework NestJS Monitoring](./nestjs-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [정식 channel spec](../spec/nestjs-channel-messaging.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md)

# Draft -- ZLink Framework NestJS Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 구현 기준은 [정식 channel spec](../spec/nestjs-channel-messaging.ko.md)이다.
> 이 draft는 기존 초안의 위치를 유지하되, 오래된 flat client 표면을 사용하지 않는다.

## 1. 기준 표면

channel outbound 호출은 fluent builder를 사용한다.

```ts
const reply = await client
  .requestToChannel('profile', new GetProfileRequest(accountId))
  .timeout(200)
  .submit<GetProfileReply>();

await client
  .sendToChannel('profile', new RefreshProfileCacheCommand(accountId))
  .packetName('profile.refresh-cache')
  .submit();
```

fanout publish도 같은 builder + terminator 결을 따른다.

```ts
await fanoutClient
  .publish('profile', 'profile.cache-refreshed', new ProfileCacheRefreshed(accountId))
  .submit();
```

## 2. 회귀 테스트

이 draft는 아래 회귀 항목과 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| channel client public surface | `ZLinkChannelClient`가 `sendToChannel`/`requestToChannel` fluent call을 제공한다. |
| event handler group mapping | fanout publish가 `ZLinkFanoutClient.publish(...).submit()` 표면을 사용한다. |
