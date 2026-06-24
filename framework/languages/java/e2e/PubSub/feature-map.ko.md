# Java PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

## 구현됨

- `PS-A1`: `ZLinkFanoutClient.publish(...).await()` 후 세 subscriber의 evidence에서 공통 연속
  sequence를 확인한다.
- `PS-A2`: subscriber handler가 `ZLinkPublishContext.topic()`을 보고 관심 topic만 evidence에
  기록하는지 검증한다.
- `PS-A3`: 세 번째 subscriber를 pre-late publish 뒤에 시작하고, 이후 publish만 받는지 확인한다.
- `PS-C1`: 미등록 packet name publish가 subscriber observer에 `HANDLER_MISSING`/`DROP`으로 남고
  이후 정상 publish가 유지되는지 확인한다.

## public API/harness 대기

- `PS-A4`: subscriber restart와 재구독 이후의 비replay 관측을 고정하는 process restart 단계가
  아직 없다.
- `PS-B1`: 느린 subscriber handler와 빠른 subscriber 격리를 동시에 단언하려면 subscriber별 처리
  지연을 주입하는 harness 옵션이 필요하다.
- `PS-B2`: publisher process restart 뒤 기존 subscriber의 재수신을 검증하는 장기 실행 publisher
  runner가 아직 없다.
