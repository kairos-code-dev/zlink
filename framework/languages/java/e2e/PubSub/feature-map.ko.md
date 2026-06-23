# Java PubSub E2E feature map

이 앱은 `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`의 P0 시나리오를 Java public API로 검증한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 fanout basic delivery | 구현 | `ZLinkFanoutClient.publish(...).await()` 후 세 subscriber의 evidence에서 공통 연속 sequence를 확인한다. |
| PS-A2 topic filter | 구현 | subscriber handler가 `ZLinkPublishContext.topic()`을 보고 관심 topic만 evidence에 기록한다. |
| PS-A3 late subscriber | 구현 | 세 번째 subscriber를 pre-late publish 뒤에 시작하고, 이후 publish만 받는지 확인한다. |
| PS-C1 publish 미등록 message name | 구현 | 미등록 packet name publish가 subscriber observer에 `HANDLER_MISSING/DROP`으로 남고 이후 정상 publish가 유지되는지 확인한다. |
| PS-A4, PS-B1, PS-B2 | 미구현 | P1 lifecycle 시나리오는 다음 PubSub 확장 단계에서 process restart/slow handler harness와 함께 추가한다. |
