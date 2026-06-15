<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Node.js Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: Draft -- ZLink Framework Node.js SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Node.js 묶음](./README.ko.md) | [정식 interface spec](../spec/handler-interfaces.ko.md)

# Draft -- ZLink Framework Node.js Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 구현 기준은 [정식 interface spec](../spec/handler-interfaces.ko.md)이다.
> 기존 draft의 오래된 flat client, raw stream session, string-keyed Spot factory
> 표면은 채택하지 않는다.

## 1. 기준 public surface

| 분류 | 기준 타입 |
|------|-----------|
| channel client | `ZLinkChannelClient`, `ZLinkSendCall`, `ZLinkRequestCall` |
| fanout client | `ZLinkFanoutClient`, `ZLinkPublishCall` |
| route client | `ZLinkRouteClient` |
| handler | `ZLinkRequestHandler`, `ZLinkSendHandler`, `ZLinkPublishHandler` |
| Spot | `ZLinkSpotManager`, `ZLinkSpotOutbound`, `ZLinkSpotPublisherClient` |
| Actor | `ZLinkActor`, `ZLinkActorManager`, `ZLinkBoundSession` |
| Stream | `ZLinkSession`, `ZLinkSessionContext`, `ZLinkSessionActors` |
| Monitoring | `ZLinkRuntimeEventHandler<TEvent>` |

## 2. 호출 예시

```ts
const reply = await client
  .requestToChannel('profile', new GetProfileRequest(accountId))
  .timeout(200)
  .submit<GetProfileReply>();

await fanoutClient
  .publish('profile', 'profile.cache-refreshed', new ProfileCacheRefreshed(accountId))
  .submit();
```

## 3. 회귀 테스트

이 draft는 아래 회귀 항목과 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| public surface scope | 비목표 API가 public surface에 없다. |
| channel client public surface | fluent call builder 표면을 제공한다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Node.js Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: Draft -- ZLink Framework Node.js SPOT Samples](./spot-samples.ko.md)
<!-- framework-adapter-nav:bottom:end -->
