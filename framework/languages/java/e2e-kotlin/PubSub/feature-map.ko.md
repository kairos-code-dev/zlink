# Kotlin PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. runner와 scenario
code는 Kotlin public framework API로 작성해 Kotlin 호출 표면에서 검증한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 구현 | fanout publisher가 모든 subscriber에 공통 연속 sequence를 전달하는지 실제 subscriber 역할 server의 bounded `/evidence/wait`로 확인한다. |
| PS-A2 | 구현 | subscriber handler가 publish context topic을 보고 관심 topic만 기록하는지 실제 subscriber 역할 server evidence로 확인한다. |
| PS-A3 | 구현 | late subscriber가 이전 publish를 replay 받지 않고 이후 publish만 받는지 subscriber evidence로 확인한다. |
| PS-A4 | 구현 | Client support가 reconnect subscriber process를 중단/재시작하고, 중단 중 publish된 event는 재시작 후 replay되지 않으며 이후 event는 수신되는지 subscriber evidence로 확인한다. |
| PS-B1 | 구현 | 느린 subscriber가 있어도 다른 subscriber가 마지막 sequence까지 받는지 bounded subscriber evidence wait로 확인한다. |
| PS-B2 | 구현 | Client support가 publisher process를 shutdown/restart하고, 재시작 뒤 subscriber들이 새 event를 받는지 bounded subscriber evidence wait로 확인한다. |
| PS-C1 | 구현 | 미등록 packet publish가 subscriber dispatch error/drop으로 기록되고 정상 publish가 회복되는지 subscriber evidence로 확인한다. |

## 검증 경로 판정

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. 따라서 Kotlin PubSub는 client stream connector observer를 추가하지 않고, 실제 subscriber
역할 server의 bounded evidence wait와 snapshot 단언으로 fanout, non-replay, negative path를 검증한다.

## 포팅 구조 상태

현재 Kotlin PubSub E2E는 `.NET` 기준에 맞춰 `Shared`, `Client`, `Server/Publisher`,
`Server/Subscriber` Gradle project로 나뉜다. registry role은 더 이상 띄우지 않고, publisher와
subscriber가 같은 Redis location store endpoint와 실행별 key prefix를 받아 peer row를 공유한다.
Client는 framework fanout client를 직접 들지 않고 publisher role의 HTTP endpoint를 호출한다. role
실행 설정은 각 role의 CLI option parser가 맡고, PS-A4/PS-B2 lifecycle 제어는 Client support의
process launcher가 맡는다. runner는 초기 role 시작, client 실행, cleanup을 담당한다. 파일별 대응
상태는 `porting-inventory.ko.md`에 기록한다.

## 검증 결과

- `logs/20260629-162342-449016`: `timeout 420s framework/languages/java/e2e-kotlin/PubSub/run_e2e.sh`
  실행 결과 role별 Gradle project와 CLI option 기반 runner가 통과했다.
- `logs/20260702-063516-76921`: `timeout 420s ./run_e2e.sh` 실행 결과 PS-A4 reconnect subscriber와
  PS-B2 restarted publisher를 Client support가 시작/종료하는 구조로 통과했다.
- `logs/20260704-030433-92742`: `timeout 420s ./run_e2e.sh` 실행 결과 registry role 없이 Redis
  location store 기반 publisher/subscriber 수렴, reconnect subscriber, restarted publisher 경로가
  모두 통과했다.
- 통과 scenario: `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1`.
- 각 client mode에서 `pub-sub kotlin e2e result=passed` marker를 확인했다.
- 최신 결과는 Redis location store 기반 role/project 분리와 Client-owned lifecycle 검증을 함께
  확인한다.
