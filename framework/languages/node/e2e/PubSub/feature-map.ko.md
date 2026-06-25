# Node PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- 없음.

## public API/harness 대기

- `PS-A1`: multi-subscriber 연속 sequence 검증 Node runner와 marker가 아직 없다.
- `PS-A2`: multiple topic과 interest-topic filtering Node runner와 marker가 아직 없다.
- `PS-A3`: late subscriber Node runner와 marker가 아직 없다.
- `PS-A4`: subscriber 재연결/재구독 Node harness가 아직 없다.
- `PS-B1`: 느린 subscriber handler Node harness가 아직 없다.
- `PS-B2`: publisher 재시작 Node harness가 아직 없다.
- `PS-C1`: public publish 호출에서 미등록 message name rejection 계약이 확인되지 않아 marker를 두지 않는다.
