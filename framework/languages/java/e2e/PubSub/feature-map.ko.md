# Java PubSub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

마지막 검증:

- 명령: `nice -n 10 timeout 600s ./run_e2e.sh all`
- 결과: passed
- 로그: `framework/languages/java/e2e/PubSub/logs/20260707-221458-3633382/`

10.0.0 목표에서는 classic fanout이 location store를 사용하지 않는다. publisher는
`enablePublisher(endpoint)`, subscriber는 `connectSubscriber(endpoint)`로 연결한다. 현재 source와
runner의 Redis discovery 제거 및 manual endpoint 적용은 S9에서 수행한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 10.0.0 전환 대상 | manual PUB/SUB 연결에서 public fanout client로 publish하고 subscriber `/evidence/wait`로 공통 연속 sequence를 확인한다. |
| PS-A2 | 구현 | subscriber handler가 `ZLinkPublishContext.topic()`으로 관심 topic만 evidence에 기록하는지 `/evidence/wait`와 snapshot 대조로 확인한다. |
| PS-A3 | 구현 | `sub-3`를 pre-late publish 뒤에 시작하고, 구독 전 이벤트가 replay되지 않으며 이후 publish만 받는지 `/evidence/wait`로 확인한다. |
| PS-A4 | 차단 | 현재 구현은 `sub-1` 프로세스를 재시작하므로 application startup이 handler를 다시 등록한다. 공통 계약이 요구하는 동일 process의 transport 단절·복구와 기존 subscription 자동 재적용을 검증하지 않는다. subscriber 하나의 연결만 끊는 process-external network fault harness가 필요하다. |
| PS-B1 | 구현 | `sub-1` handler 지연 중에도 `sub-2`와 `sub-3`가 최신 이벤트를 계속 받는지 `/evidence/wait`로 확인한다. |
| PS-B2 | 10.0.0 전환 대상 | publisher를 같은 endpoint로 재시작하고 기존 subscriber의 `ConnectionReady` 뒤 새 event 수신을 확인한다. 동적 역할 readiness는 3초다. |
| PS-C1 | 구현 | 미등록 packet name publish가 subscriber observer evidence에 `HANDLER_MISSING`/`DROP`으로 남고 이후 정상 publish가 유지되는지 `/evidence/wait`로 확인한다. |

## Evidence wait 검증

공통 E2E README는 Pub/Sub처럼 검증 대상이 client stream session이 아니라 subscriber 역할 server의
fanout delivery인 경우, subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로
쓸 수 있다고 정한다. Java PubSub client는 같은 snapshot GET을 반복하지 않고 `/evidence/wait`로
각 subscriber의 실제 dispatch marker를 기다린 뒤 필요한 snapshot만 대조한다.
