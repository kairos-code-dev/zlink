# .NET PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 구현 | fanout basic delivery marker가 있다. |
| PS-A2 | 구현 | topic filter marker가 있다. |
| PS-A3 | 구현 | late subscriber marker가 있다. |
| PS-A4 | 구현 | subscriber 프로세스를 끊었다가 같은 endpoint로 다시 띄우고, 끊긴 동안 replay 없이 복구 후 발행분만 받는 marker가 있다. |
| PS-B1 | 구현 | 한 subscriber handler에 지연을 넣어도 다른 subscriber가 계속 받는 marker가 있다. |
| PS-B2 | 구현 | publisher 프로세스를 종료·재기동한 뒤 복구 후 발행분이 subscriber에 도달하는 marker가 있다. |
| PS-C1 | 구현 | publish missing message name marker가 있다. |
