# .NET PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

10.0.0 목표에서는 classic fanout이 location store를 사용하지 않는다. publisher는
`EnablePublisher(endpoint)`, subscriber는 `ConnectSubscriber(endpoint)`로 PUB/SUB 연결을 구성한다.
`.NET` E2E는 Redis location store를 등록하지 않고 manual endpoint로 연결한다.
`run_e2e.sh` 전체 실행에서 아래 일곱 시나리오가 모두 통과했다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 구현 | 세 subscriber의 `ConnectionReady` 뒤 공통 연속 sequence 수신 marker를 확인한다. |
| PS-A2 | 구현 | 서로 다른 packet name의 typed handler가 각 event를 한 번씩 처리하고 다른 handler가 처리하지 않은 marker를 확인한다. |
| PS-A3 | 구현 | 늦게 시작한 subscriber의 `ConnectionReady` 뒤 발행분 수신과 연결 전 발행분 non-replay를 확인한다. |
| PS-A4 | 구현 | 같은 subscriber process에서 외부 TCP fault proxy로 `Disconnected`·`ConnectionReady`를 만들고, 복구 뒤 수신과 disconnect 구간 non-replay를 확인한다. |
| PS-B1 | 구현 | 한 subscriber handler의 처리 지연 중에도 다른 subscriber가 계속 수신하는 marker를 확인한다. |
| PS-B2 | 구현 | 정상 종료한 publisher를 같은 manual endpoint로 재시작하고, 기존 subscriber의 `Disconnected`·`ConnectionReady`와 복구 뒤 수신 marker를 확인한다. |
| PS-C1 | 구현 | 미등록 packet name의 `no_handler`·`drop` marker와 이후 정상 event 수신을 확인한다. |

## 검증 경로 판정

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. 따라서 이 feature map은 별도 client stream connector observer를 요구하지 않는다.
