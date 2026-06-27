# .NET PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 부분 구현 | fanout basic delivery marker는 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-A2 | 부분 구현 | topic filter marker는 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-A3 | 부분 구현 | late subscriber marker는 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-A4 | 부분 구현 | subscriber 프로세스를 끊었다가 같은 endpoint로 다시 띄우고 복구 후 발행분 marker를 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-B1 | 부분 구현 | 한 subscriber handler에 지연을 넣어도 다른 subscriber가 계속 받는 marker는 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-B2 | 부분 구현 | publisher 프로세스 재기동 후 복구 발행분 marker는 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-C1 | 부분 구현 | publish missing message name marker는 확인하지만, negative 전파 확인은 아직 HTTP evidence polling에 의존한다. |

## Push 검증 gap

공통 E2E README는 값 변경이나 fanout 결과처럼 push로 확인할 수 있는 변화는 HTTP evidence를 반복
조회하지 말고 client stream connector로 연결해 push 메시지로 검증하도록 요구한다. 현재 .NET PubSub
E2E는 subscriber 역할의 `/evidence` endpoint를 HTTP로 조회해 marker를 확인한다.

이 gap은 PubSub 동작 자체의 누락이 아니라 검증 경로의 누락이다. .NET stream connector 구현은
존재하지만 PubSub E2E가 사용할 공통 stream push 계약과 .NET stream connector 문서가 아직
정리되어 있지 않으므로, 새 public API나 테스트 전용 adapter를 추가하지 않고 후속 public contract
parity 작업의 입력으로 남긴다.
