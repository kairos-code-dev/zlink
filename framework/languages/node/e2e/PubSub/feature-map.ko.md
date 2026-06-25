# Node PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `PS-A1`: TCP fanout publisher와 subscriber 3개가 warm-up 뒤 같은 연속 sequence를 같은 순서로 받는다.
- `PS-A2`: subscriber handler가 public publish context topic을 보고 관심 topic만 evidence에 기록한다.
- `PS-A3`: 발행 시작 후 합류한 late subscriber가 합류 이후 publish만 받고 이전 sequence를 받지 않는다.
- `PS-C1`: 미등록 message name publish가 subscriber observer에 handlerMissing/drop evidence를 남기고 정상 publish는 계속 동작한다.

## public API/harness 대기

- `PS-A4`: subscriber 재연결/재구독 Node harness가 아직 없다.
- `PS-B1`: 느린 subscriber handler Node harness가 아직 없다.
- `PS-B2`: publisher 재시작 Node harness가 아직 없다.
