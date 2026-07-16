# Node.js PubSub E2E feature map

| Scenario | 상태 | Node.js 검증 파일 | 비고 |
|----------|------|-------------------|------|
| `PS-A1` | 구현 | `Client/Scenarios/ps-a1-fanout-basic-delivery-scenario.ts` | warm-up barrier 뒤 모든 subscriber가 공유하는 연속 sequence marker를 실제 subscriber 역할 server의 `/evidence/wait`로 확인한다. |
| `PS-A2` | 구현 | `Client/Scenarios/ps-a2-topic-filter-scenario.ts` | accepted topic evidence와 ignored topic non-business-event marker를 실제 subscriber 역할 server evidence로 확인한다. |
| `PS-A3` | 구현 | `Client/Scenarios/ps-a3-late-subscriber-scenario.ts` | 차단된 transport에서 ready 전 event를 한 번 발행하고, subscriber의 실제 `ConnectionReady` 뒤 첫 event 수신과 이전 event replay 부재를 확인한다. |
| `PS-A4` | 구현 | `Client/Scenarios/ps-a4-subscriber-reconnect-scenario.ts` | subscriber process를 유지한 채 transport를 끊고 복구해 기존 subscription 자동 재적용, disconnect 구간 non-replay, fast subscriber 지속 수신을 확인한다. |
| `PS-B1` | 구현 | `Client/Scenarios/ps-b1-slow-subscriber-scenario.ts` | slow subscriber delay evidence와 fast subscriber tail event marker를 실제 subscriber 역할 server evidence로 확인한다. |
| `PS-B2` | 구현 | `Client/Scenarios/ps-b2-publisher-restart-scenario.ts` | terminal `Drained`, 기존 publisher row 제거, 같은 rid/endpoint 재등록과 subscriber `ConnectionReady` 뒤 첫 event 전달을 확인한다. |
| `PS-C1` | 구현 | `Client/Scenarios/ps-c1-missing-message-name-scenario.ts` | subscriber dispatch drop evidence와 이후 정상 publish delivery marker를 실제 subscriber 역할 server evidence로 확인한다. |

## 검증 경로 판정

publisher와 모든 subscriber는 실행마다 만든 전용 Redis location store를 함께 사용한다. publisher의
peer row는 framework lifecycle이 등록하며 subscriber는 고정 publisher endpoint를 받지 않고 이 row를
조회해 fanout 연결을 설정한다.

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. 따라서 이 feature map은 별도 client stream connector observer를 요구하지 않는다.

## 검증

- `timeout 420s framework/languages/node/e2e/PubSub/run_e2e.sh`
  - 결과: `pubsub e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260715-074718-2239660`
  - 통과 scenario: `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1`
