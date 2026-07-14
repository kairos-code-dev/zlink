# 인터페이스 카탈로그

이 장은 사용자가 자주 찾는 public interface 위치를 정리한다. 전체 계약의 단일 기준은
정식 spec 인 [handler-interfaces](../../spec/server/languages/node/02-handler-interfaces.ko.md) 다.

## 1. NestJS module

- `ZLinkModule`
- `ZLINK_CHANNEL_CLIENT`
- `ZLINK_FANOUT_CLIENT`
- `ZLINK_SPOT_MANAGER`
- `ZLINK_ACTOR_MANAGER`
- `ZLINK_LOCATION_RUNTIME_QUERY`

## 2. channel

- `ZLinkChannelClient`
- `ZLinkFanoutClient`
- `ZLinkSendCall`
- `ZLinkRequestCall`
- `ZLinkPublishCall`

## 3. Spot과 actor

- `ZLinkSpot`
- `ZLinkSpotContext`
- `ZLinkSpotManager`
- `ZLinkActor`
- `ZLinkActorContext`
- `ZLinkActorManager`
- `ZLinkBoundSession`

## 4. stream과 connector

- `ZLinkSession`
- `ZLinkSessionContext`
- `ZLinkSessionClient`
- `ZlinkStreamConnector`
- `ZlinkStreamFrameCodec`

## 회귀 테스트

catalog export 는 `test/contract/contract-surface.test.js` 에서 확인한다.
