# .NET PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 구현 | fanout basic delivery marker가 있다. |
| PS-A2 | 구현 | topic filter marker가 있다. |
| PS-A3 | 구현 | late subscriber marker가 있다. |
| PS-A4 | 미구현 | subscriber reconnect/resubscribe marker가 없다. |
| PS-B1 | 미구현 | slow subscriber isolation marker가 없다. |
| PS-B2 | 미구현 | publisher restart marker가 없다. |
| PS-C1 | 구현 | publish missing message name marker가 있다. |
