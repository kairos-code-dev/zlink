# C++ Pub/Sub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 C++ framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

`logs/20260708-123833-1298240`은 Redis location store를 공유하는 현재 source의
Publisher/Subscriber 역할로 PS-A1~PS-C1을 실행한 기록이다. 10.0.0의 manual endpoint topology를
검증한 기록은 아니므로 아래 시나리오의 완료 증거로 사용하지 않는다.

10.0.0 목표에서는 classic fanout이 location store를 사용하지 않는다. publisher는
`enable_publisher(endpoint)`, subscriber는 `enable_subscriber(endpoint)`로 PUB/SUB 연결을 구성한다.
현재 source와 runner는 Redis discovery를 제거하고 manual endpoint를 적용해야 한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `PS-A1` | 10.0.0 전환 대상 | manual endpoint에서 Publisher role server가 fanout 측정 sequence를 발행하고, 세 subscriber role server의 bounded `/evidence/wait`가 공통 sequence 수신 line을 확인해야 한다. |
| `PS-A2` | 10.0.0 전환 대상 | manual endpoint에서 subscriber handler가 publish context의 topic을 보고 관심 topic은 accepted evidence로, 비관심 topic은 ignored evidence로 기록하는지 확인해야 한다. |
| `PS-A3` | 10.0.0 전환 대상 | manual endpoint에서 late subscriber가 연결된 이후 발행분만 받고 연결 전 발행분은 replay되지 않는지 확인해야 한다. |
| `PS-A4` | 10.0.0 전환 대상 | 같은 subscriber process를 유지한 채 transport만 단절·복구하고, 기존 subscription 자동 재적용과 단절 구간 non-replay를 확인해야 한다. 현재 process 재시작 방식은 이 계약을 검증하지 않는다. |
| `PS-B1` | 10.0.0 전환 대상 | manual endpoint에서 한 subscriber handler에 지연을 주입해도 다른 subscriber가 같은 발행 sequence를 계속 수신하는지 확인해야 한다. |
| `PS-B2` | 10.0.0 전환 대상 | Publisher를 같은 manual endpoint로 다시 시작한 뒤 기존 subscriber가 복구 이후 발행분을 받는지 확인해야 한다. |
| `PS-C1` | 10.0.0 전환 대상 | manual endpoint에서 미등록 message name의 `no_handler`/`drop` evidence와 후속 정상 publish 복구를 확인해야 한다. |

## 검증 경로 판정

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. C++ PubSub도 실제 subscriber role server의 `/evidence/wait`로 accepted, ignored, dispatch
error line을 기다리므로 별도 client stream connector observer gap은 남기지 않는다. 최신 runner는
검증 stdout을 `verify.log`에 저장하고, publisher role의 `/health`, `/evidence`,
`/evidence/clear`, `/shutdown` endpoint 동작도 operational log와 final evidence snapshot으로 남긴다.
