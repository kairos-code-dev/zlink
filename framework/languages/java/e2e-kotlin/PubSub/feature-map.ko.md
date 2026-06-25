# Kotlin PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. runner와 scenario
code는 Kotlin public framework API로 작성해 Kotlin 호출 표면에서 검증한다.

## 구현됨

- `PS-A1`: Kotlin fanout publisher가 모든 subscriber에 연속 sequence를 전달하는지 확인한다.
- `PS-A2`: Kotlin subscriber topic filter가 관심 topic만 기록하는지 확인한다.
- `PS-A3`: late subscriber가 이전 publish를 replay 받지 않고 이후 publish만 받는지 확인한다.
- `PS-A4`: subscriber 중단 중 publish된 event는 재시작 후 replay되지 않고 이후 event는 수신되는지 확인한다.
- `PS-B1`: 느린 subscriber가 있어도 다른 subscriber가 마지막 sequence까지 받는지 확인한다.
- `PS-B2`: publisher 재시작 뒤 subscriber들이 새 event를 받는지 확인한다.
- `PS-C1`: 미등록 packet publish가 dispatch error/drop으로 기록되고 정상 publish가 회복되는지 확인한다.

## public API/harness 대기

- 없음.
