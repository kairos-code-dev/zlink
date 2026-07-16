# .NET PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

10.0.0 목표에서는 classic fanout이 location store를 사용하지 않는다. publisher는
`EnablePublisher(endpoint)`, subscriber는 `ConnectSubscriber(endpoint)`로 PUB/SUB 연결을 구성한다.
현재 source와 runner는 Redis discovery를 사용하므로 아래 행의 10.0.0 완료 증거가 아니다. Redis
discovery를 제거하고 manual endpoint로 같은 시나리오를 다시 실행해야 한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 10.0.0 전환 대상 | manual endpoint에서 fanout basic delivery marker를 실제 subscriber 역할 server의 `/evidence/wait`로 확인해야 한다. |
| PS-A2 | 10.0.0 전환 대상 | manual endpoint에서 topic filter marker를 실제 subscriber 역할 server의 `/evidence/wait`로 확인해야 한다. |
| PS-A3 | 10.0.0 전환 대상 | manual endpoint에서 late subscriber marker와 pre-join non-replay를 실제 subscriber 역할 server evidence로 확인해야 한다. |
| PS-A4 | 10.0.0 전환 대상 | 같은 subscriber process를 유지한 채 외부 TCP fault proxy로 transport만 단절·복구하고, 기존 subscription 재적용과 disconnect-gap non-replay를 확인해야 한다. 현재 scenario의 fault 방식은 적합하지만 Redis discovery를 제거한 뒤 다시 실행해야 한다. |
| PS-B1 | 10.0.0 전환 대상 | manual endpoint에서 한 subscriber handler에 지연을 넣어도 다른 subscriber가 계속 받는 marker를 확인해야 한다. |
| PS-B2 | 10.0.0 전환 대상 | 같은 manual endpoint로 publisher를 재기동한 뒤 기존 subscriber의 복구 발행분 marker를 확인해야 한다. |
| PS-C1 | 10.0.0 전환 대상 | manual endpoint에서 publish missing message name drop marker와 정상 publish 복구 marker를 확인해야 한다. |

## 검증 경로 판정

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. 따라서 이 feature map은 별도 client stream connector observer를 요구하지 않는다.
