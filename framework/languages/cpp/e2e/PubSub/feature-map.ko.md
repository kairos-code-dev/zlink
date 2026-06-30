# C++ Pub/Sub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 C++ framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `PS-A1` | 부분 구현 | Publisher role server가 fanout 측정 sequence를 발행하고 세 subscriber의 수신 evidence를 확인한다. push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| `PS-A2` | 부분 구현 | subscriber handler가 publish context의 topic을 보고 관심 topic은 accepted evidence로, 비관심 topic은 ignored evidence로 기록하는지 확인한다. push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| `PS-A3` | 부분 구현 | late subscriber가 합류 이후 발행분만 받고 합류 전 발행분은 replay되지 않는지 확인한다. push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| `PS-A4` | 부분 구현 | subscriber 프로세스를 종료했다가 같은 endpoint로 다시 띄우고, 종료 중 발행분은 replay되지 않으며 재구독 이후 발행분만 다시 받는지 확인한다. push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| `PS-B1` | 부분 구현 | 한 subscriber handler에 지연을 주입한 상태에서 다른 subscriber가 같은 발행 sequence를 계속 수신하는지 확인한다. push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| `PS-B2` | 부분 구현 | Publisher role server를 종료했다가 다시 시작한 뒤, 재시작 이후에만 나오는 발행분을 기존 subscriber가 받는지 확인한다. push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| `PS-C1` | 부분 구현 | handler 없는 message name으로 publish하면 subscriber dispatch observer에 `handlerMissing`/`drop` marker가 남고 후속 정상 publish가 오염되지 않는지 확인한다. negative 전파 확인은 아직 HTTP evidence polling에 의존한다. |

## Push 검증 gap

공통 E2E README는 값 변경이나 fanout 결과처럼 push로 확인할 수 있는 변화는 HTTP evidence를 반복
조회하지 말고 client stream connector로 연결해 push 메시지로 검증하도록 요구한다. 현재 C++ PubSub
E2E는 `.NET` 기준 구현과 마찬가지로 subscriber 역할의 `/evidence` endpoint를 HTTP로 조회해 marker를
확인한다.

이 gap은 PubSub 동작 자체의 누락이 아니라 검증 경로의 누락이다. 새 public API나 테스트 전용 adapter를
추가하지 않고, stream push 검증 계약이 정리된 뒤 후속 public contract parity 작업의 입력으로 남긴다.
