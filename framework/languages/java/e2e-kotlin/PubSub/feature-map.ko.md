# Kotlin PubSub E2E feature map

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. runner와 scenario
code는 Kotlin public framework API로 작성해 Kotlin 호출 표면에서 검증한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| PS-A1 | 10.0.0 전환 대상 | 모든 subscriber의 `ConnectionReady` 뒤 측정 구간을 시작하고 공통 연속 sequence를 같은 순서로 받는지 확인해야 한다. 현재 evidence wait만으로는 구독 readiness 경계를 증명하지 못한다. |
| PS-A2 | 구현 | subscriber handler가 publish context topic을 보고 관심 topic만 기록하는지 실제 subscriber 역할 server evidence로 확인한다. |
| PS-A3 | 구현 | late subscriber가 이전 publish를 replay 받지 않고 이후 publish만 받는지 subscriber evidence로 확인한다. |
| PS-A4 | 차단 | 현재 Client support는 subscriber process를 중단하고 다시 시작하므로 application startup이 subscription을 다시 등록한다. 동일 process의 transport 단절·복구와 기존 subscription 자동 재적용을 검증하지 못한다. subscriber 연결만 끊는 fault harness가 필요하다. |
| PS-B1 | 구현 | 느린 subscriber가 있어도 다른 subscriber가 마지막 sequence까지 받는지 bounded subscriber evidence wait로 확인한다. |
| PS-B2 | 10.0.0 전환 대상 | subscriber process와 등록 topic을 유지한 채 같은 endpoint의 publisher를 재시작하고, 새 publisher의 `ConnectionReady` 뒤 새 event를 받는지 확인해야 한다. 현재 row 교체와 수신은 확인하지만 socket readiness evidence가 남아 있다. |
| PS-C1 | 구현 | 미등록 packet publish가 subscriber dispatch error/drop으로 기록되고 정상 publish가 회복되는지 subscriber evidence로 확인한다. |

## 검증 경로 판정

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. 따라서 Kotlin PubSub는 client stream connector observer를 추가하지 않고, 실제 subscriber
역할 server의 bounded evidence wait와 snapshot 단언으로 fanout, non-replay, negative path를 검증한다.

## 포팅 구조 상태

현재 Kotlin PubSub E2E는 `Shared`, `Client`, `Server/Publisher`, `Server/Subscriber` Gradle project로
나뉜다. 10.0.0 목표에서는 registry와 location store 없이 publisher endpoint를 subscriber에 명시해
classic fanout 연결을 구성한다. 현재 source와 runner는 Redis discovery를 제거하고 manual endpoint를
적용해야 한다.
Client는 framework fanout client를 직접 들지 않고 publisher role의 HTTP endpoint를 호출한다. role
실행 설정은 각 role의 CLI option parser가 맡고, PS-A4/PS-B2 lifecycle 제어는 Client support의
process launcher가 맡는다. runner는 초기 role 시작, client 실행, cleanup을 담당한다. 파일별 대응
상태는 `porting-inventory.ko.md`에 기록한다.

## 완료 판정

표에서 `구현`으로 표시한 행은 해당 시나리오의 검증 로직이 존재한다. `PS-A1`·`PS-B2`는
`ConnectionReady`를 포함한 readiness 경계를 확인하기 전까지 전환 대상으로 유지한다. `PS-A4`는 동일 process에서
transport 연결만 복구하는 fault harness가 추가되기 전까지 차단 상태다. 모든 행은 Redis discovery가
없는 manual publisher endpoint 구성으로 실행한 결과를 확보한 뒤에만 10.0.0 완료로 판정한다.
