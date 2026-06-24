# Kotlin PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. 현재 추적된 Kotlin
PubSub runner/source가 없으므로 Kotlin 전용 stdout marker로 구현 완료를 주장하지 않는다.

## 구현됨

- 없음.

## public API/harness 대기

- `PS-A1`: fanout basic delivery Kotlin runner와 marker가 아직 없다.
- `PS-A2`: topic filter Kotlin runner와 marker가 아직 없다.
- `PS-A3`: late subscriber Kotlin runner와 marker가 아직 없다.
- `PS-A4`: subscriber 재연결/재구독 Kotlin harness가 아직 없다.
- `PS-B1`: 느린 subscriber handler Kotlin harness가 아직 없다.
- `PS-B2`: publisher 재시작 Kotlin harness가 아직 없다.
- `PS-C1`: 미등록 message name publish negative path Kotlin runner와 marker가 아직 없다.
