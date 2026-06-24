# Java PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

## 구현됨

- `PS-A1`: `ZLinkFanoutClient.publish(...).await()` 후 세 subscriber의 evidence에서 공통 연속
  sequence를 확인한다.
- `PS-A2`: subscriber handler가 `ZLinkPublishContext.topic()`을 보고 관심 topic만 evidence에
  기록하는지 검증한다.
- `PS-A3`: 세 번째 subscriber를 pre-late publish 뒤에 시작하고, 이후 publish만 받는지 확인한다.
- `PS-A4`: `sub-1` 프로세스를 종료한 동안 publish된 이벤트는 재시작 후 evidence에 없고,
  재시작 뒤 publish된 이벤트는 다시 받는지 확인한다.
- `PS-B1`: `sub-1` handler에 지연을 주입해도 `sub-2`/`sub-3`가 최신 이벤트를 계속 받는지
  확인한다.
- `PS-B2`: publisher/client 프로세스를 한 번 종료한 뒤 새 publisher 프로세스가 보낸 이벤트를
  기존 subscriber들이 다시 받는지 확인한다.
- `PS-C1`: 미등록 packet name publish가 subscriber observer에 `HANDLER_MISSING`/`DROP`으로 남고
  이후 정상 publish가 유지되는지 확인한다.

## public API/harness 대기
