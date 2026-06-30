# Kotlin PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. runner와 scenario
code는 Kotlin public framework API로 작성해 Kotlin 호출 표면에서 검증한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 부분 구현 | fanout publisher가 모든 subscriber에 연속 sequence를 전달하는지 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-A2 | 부분 구현 | subscriber handler가 publish context topic을 보고 관심 topic만 기록하는지 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-A3 | 부분 구현 | late subscriber가 이전 publish를 replay 받지 않고 이후 publish만 받는지 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-A4 | 부분 구현 | subscriber 중단 중 publish된 event는 재시작 후 replay되지 않고 이후 event는 수신되는지 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-B1 | 부분 구현 | 느린 subscriber가 있어도 다른 subscriber가 마지막 sequence까지 받는지 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-B2 | 부분 구현 | publisher 재시작 뒤 subscriber들이 새 event를 받는지 확인하지만, push 수신 검증은 아직 HTTP evidence polling에 의존한다. |
| PS-C1 | 부분 구현 | 미등록 packet publish가 dispatch error/drop으로 기록되고 정상 publish가 회복되는지 확인하지만, negative 전파 확인은 아직 HTTP evidence polling에 의존한다. |

## Push 검증 gap

공통 E2E README는 값 변경이나 fanout 결과처럼 push로 확인할 수 있는 변화는 HTTP evidence를 반복
조회하지 말고 client stream connector로 연결해 push 메시지로 검증하도록 요구한다. 현재 Kotlin PubSub
E2E는 subscriber 역할의 evidence HTTP endpoint를 반복 조회해 marker를 확인한다.

이 gap은 PubSub 동작 자체의 누락이 아니라 검증 경로의 누락이다. Kotlin에서 바로 새 public API나
테스트 전용 adapter를 추가하지 않고, stream push 검증 계약이 정리된 뒤 후속 public contract parity
작업에서 닫는다.

## 포팅 구조 상태

현재 Kotlin PubSub E2E는 `.NET` 기준에 맞춰 `Shared`, `Client`, `Server/Publisher`,
`Server/Registry`, `Server/Subscriber` Gradle project로 나뉜다. Client는 framework fanout client를
직접 들지 않고 publisher role의 HTTP endpoint를 호출한다. role 실행 설정은 각 role의 CLI option
parser가 맡고, 파일별 대응 상태는 `porting-inventory.ko.md`에 기록한다.

## 검증 결과

- `logs/20260629-162342-449016`: `timeout 420s framework/languages/java/e2e-kotlin/PubSub/run_e2e.sh`
  실행 결과 role별 Gradle project와 CLI option 기반 runner가 통과했다.
- 통과 scenario: `PS-A1`, `PS-A2`, `PS-A3`, `PS-A4`, `PS-B1`, `PS-B2`, `PS-C1`.
- 각 client mode에서 `pub-sub kotlin e2e result=passed` marker를 확인했다.
- 이 결과는 현재 role/project 분리 구현의 동작 기준선이다. push 검증 gap은 아직 후속 작업으로 남긴다.
