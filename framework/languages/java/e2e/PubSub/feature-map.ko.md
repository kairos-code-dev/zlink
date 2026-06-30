# Java PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

마지막 검증:

- 명령: `timeout 420s ./run_e2e.sh`
- 결과: passed
- 로그: `framework/languages/java/e2e/PubSub/logs/20260629-123647-553789/`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 부분 구현 | publisher 역할이 public `ZLinkFanoutClient`로 fanout publish를 수행하고, client가 세 subscriber의 evidence에서 공통 연속 sequence를 확인한다. 검증 경로는 아직 HTTP evidence polling이다. |
| PS-A2 | 부분 구현 | subscriber handler가 `ZLinkPublishContext.topic()`으로 관심 topic만 evidence에 기록하는지 확인한다. 검증 경로는 아직 HTTP evidence polling이다. |
| PS-A3 | 부분 구현 | `sub-3`를 pre-late publish 뒤에 시작하고, 구독 전 이벤트가 replay되지 않으며 이후 publish만 받는지 확인한다. 검증 경로는 아직 HTTP evidence polling이다. |
| PS-A4 | 부분 구현 | `sub-1` 프로세스를 종료한 동안 publish된 이벤트는 재시작 후 evidence에 없고, 재시작 뒤 publish된 이벤트는 다시 받는지 확인한다. 검증 경로는 아직 HTTP evidence polling이다. |
| PS-B1 | 부분 구현 | `sub-1` handler 지연 중에도 `sub-2`와 `sub-3`가 최신 이벤트를 계속 받는지 확인한다. 검증 경로는 아직 HTTP evidence polling이다. |
| PS-B2 | 부분 구현 | publisher 프로세스를 실제로 종료한 뒤 새 publisher process가 보낸 이벤트를 기존 subscriber들이 받는지 확인한다. 검증 경로는 아직 HTTP evidence polling이다. |
| PS-C1 | 부분 구현 | 미등록 packet name publish가 subscriber observer evidence에 `HANDLER_MISSING`/`DROP`으로 남고 이후 정상 publish가 유지되는지 확인한다. 검증 경로는 아직 HTTP evidence polling이다. |

## Push 검증 gap

공통 E2E README는 fanout 결과처럼 push로 확인할 수 있는 변화는 HTTP evidence를 반복 조회하지 말고
client stream connector로 연결해 push 메시지로 검증하도록 요구한다. 현재 Java PubSub E2E는 `.NET`
PubSub와 마찬가지로 subscriber 역할의 `/evidence` endpoint를 HTTP로 조회해 marker를 확인한다.

이 gap은 PubSub 동작 자체의 누락이 아니라 검증 경로의 누락이다. PubSub E2E가 사용할 공통 stream
push 계약과 Java stream connector 문서가 정리되기 전까지 새 public API나 테스트 전용 adapter를
추가하지 않고 후속 public contract parity 작업의 입력으로 남긴다.
