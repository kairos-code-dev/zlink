# Kotlin Framework E2E 재포팅 작업자 프롬프트

## 역할

너는 `/home/hep7/project/kairos/zlink` 저장소에서 Kotlin framework E2E를 `.NET` 기준으로 재포팅하는 작업자다.
기준 문서는 `framework/doc/plan/framework-kotlin-e2e-dotnet-porting-repair-list.ko.md`다.

## 목표

Kotlin E2E가 `.NET` E2E의 실행 책임, role 분리, 파일 분류, Client driver 방식을 따르도록 수정한다.
Scenario marker만 통과시키는 것이 목표가 아니다. Client가 framework application, spot, actor, registry
participant로 직접 구동되는 구조는 HTTP client 또는 stream connector driver 구조로 고친다.

## 작업 범위

1. `RegistrationCodec`
2. `ResilienceLifecycle`
3. `RuntimeMonitoring`
4. `SpotService`
5. `RegistryMessaging`
6. `YieldDispatch`
7. `PubSub`

추가로 `RuntimeMonitoring`의 `FailoverService` extra role과 `SpotService` root role switch를 반드시 함께
정리한다. 한 번에 하나의 config를 끝까지 닫는다.

## 반드시 지킬 규칙

- `.NET` source-only inventory를 먼저 만들고 Kotlin source와 대조한다.
- `bin`, `obj`, `logs` 같은 산출물은 기준 role로 세지 않는다.
- public API가 없으면 내부 helper, raw frame, 테스트 전용 adapter로 우회하지 않는다.
- framework 기능 누락이나 framework 버그가 드러나면 원인을 찾아 framework 기능 추가 또는 버그 수정을 한다.
- framework 버그 수정에는 회귀테스트를 추가한다.
- extra sleep, retry-only workaround로 E2E를 통과시키지 않는다.
- dirty worktree에서 다른 사람의 변경을 되돌리지 않는다.

## 작업 순서

1. 기준 문서의 해당 config 항목과 체크리스트를 읽는다.
2. `.NET` `framework/languages/dotnet/e2e/<Config>` source tree를 source-only 기준으로 다시 확인한다.
3. Kotlin `framework/languages/java/e2e-kotlin/<Config>` tree와 role, Client 책임, scenario file 분류를 비교한다.
4. 누락된 role, extra role, 잘못된 Client 구조를 수정한다.
5. framework 기능 누락 또는 버그가 나오면 E2E 코드에서 우회하지 말고 framework 원인을 고친다.
6. 필요한 회귀테스트를 추가한다.
7. `porting-inventory.ko.md`와 `feature-map.ko.md`를 실제 구현 상태와 맞춘다.
8. 해당 config의 실제 runner를 실행한다.
   - 기본: `timeout 420s ./run_e2e.sh`
   - 기존 language plan이 더 긴 timeout을 요구하면 그 기준을 따른다.
9. 로그에서 scenario marker와 role process evidence를 확인한다.
10. read-only review로 누락 항목을 다시 찾는다.

## 완료 조건

- Client가 framework runtime으로 뜨는 잘못된 구조가 남아 있지 않다.
- `.NET` source role 누락이 없다.
- Kotlin extra role은 근거가 있거나 제거되었다.
- scenario file 분류가 `.NET Client/Scenarios`와 공통 E2E scenario ID에 대응된다.
- public API gap을 내부 helper로 숨기지 않았다.
- framework 기능 누락 또는 버그를 E2E 코드 우회로 처리하지 않았다.
- runner 실행 결과와 문서 상태가 일치한다.
- 마지막 Codex 반복 리뷰 결과가 `NO MISSING KOTLIN ITEMS`다.

## 최종 보고 형식

작업 완료 후 아래 항목만 간결하게 보고한다.

- 수정한 config
- 주요 코드 변경
- 갱신한 inventory/feature-map 문서
- 추가한 framework 기능 또는 버그 수정과 회귀테스트
- 실행한 runner와 결과
- Codex 반복 리뷰 결과
- 남은 gap이 있으면 public contract 또는 harness 사유
