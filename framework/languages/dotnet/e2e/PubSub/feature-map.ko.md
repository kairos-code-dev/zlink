# .NET PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 구현 | fanout basic delivery marker를 실제 subscriber 역할 server의 `/evidence/wait`로 확인한다. |
| PS-A2 | 구현 | topic filter marker를 실제 subscriber 역할 server의 `/evidence/wait`로 확인한다. |
| PS-A3 | 구현 | late subscriber marker와 pre-join non-replay를 실제 subscriber 역할 server evidence로 확인한다. |
| PS-A4 | 구현 | subscriber 프로세스를 끊었다가 같은 endpoint로 다시 띄우고, 복구 후 발행분과 disconnect-gap non-replay marker를 실제 subscriber 역할 server evidence로 확인한다. |
| PS-B1 | 구현 | 한 subscriber handler에 지연을 넣어도 다른 subscriber가 계속 받는 marker를 실제 subscriber 역할 server evidence로 확인한다. |
| PS-B2 | 구현 | publisher 프로세스 재기동 후 복구 발행분 marker를 실제 subscriber 역할 server evidence로 확인한다. |
| PS-C1 | 구현 | publish missing message name drop marker와 정상 publish 복구 marker를 실제 subscriber 역할 server evidence로 확인한다. |

## 검증 경로 판정

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. 따라서 이 feature map은 별도 client stream connector observer를 요구하지 않는다.
