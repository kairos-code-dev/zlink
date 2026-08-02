# .NET Framework current progress log

## 기록 범위

- 기록 시각: 2026-08-03 07:17:02 KST
- 대상 ledger: [`../dotnet-framework-spec-gap-ledger.ko.md`](../dotnet-framework-spec-gap-ledger.ko.md)
- 기준 `HEAD`: `ee1dbccbb5ba72a9defc4158206b4fd23fa36c62`
- 기준 branch: `agent/framework-contract-runtime-update`
- `origin/agent/framework-contract-runtime-update`는 같은 commit을 가리킨다.
- 목적: 2026-08-02 23:51 이후 현재 working tree에 추가된 .NET E2E 진행을 확인하고, source/build/regression과 actual process evidence를 분리한다.

이 확인에서는 .NET production source, package, 공통 spec과 exact interface를 수정하지 않았다. 대상
ledger와 이 log만 갱신하며, 다른 언어 workstream의 dirty 변경은 보존한다.

## Working tree 조건

ledger와 이 log를 반영한 최종 capture 시점의 전체 working tree는 다른 workstream 변경을 포함해
status 731줄이며, status manifest의 SHA-256은
`4ae0f85ccccf90be77b17a55e9675b51f52cd782ae1d33f425b906d62d27a728`다. .NET E2E와
`Zlink.Framework.SampleRegressionTests` 범위는 47줄이며 SHA-256은
`a725d149856bac8f4381327669da0f876951eba33fe087f456a0676ba5004d22`다.

현재 ledger 문서의 content hash는 `e51acb02074d6916b9fcca88a505ee6972c8ddd9`다. 다른
workstream이 동시에 working tree를 바꿀 수 있으므로 이 fingerprint는 이 기록 시점의 snapshot이다.

다음 경로에는 이 확인 시점의 dirty source가 없다.

- `framework/languages/dotnet/src/`
- `framework/languages/dotnet/Directory.Packages.props`
- `framework/languages/dotnet/scripts/`
- `framework/languages/dotnet/contract/`

기존 [`working-tree-status.txt`](working-tree-status.txt)는 이전 review의 historical manifest이므로
덮어쓰지 않았다.

## 현재 확인된 E2E 진행

| Config | 현재 working tree에서 확인한 변화 | 현재 판정 |
|---|---|---|
| 2 SpotService | `sm-g5a`와 `sm-g5b`를 client dispatch에 연결하고 runner가 각각 선택하도록 바꿨다. | `SM-G5A/B` feature-map은 아직 `미구현`이고 actual process log가 없어 source 진행 상태다. |
| 4 RegistrationCodec | `RC-B6` client selector, typed `JsonGolden` message, role-server endpoint와 handler가 추가됐다. Client·Server build가 warning/error 0으로 통과했다. | feature-map은 아직 `미구현`이고 별도 process roundtrip log가 없다. |
| 7 RuntimeMonitoring | `MON-A4A/B`와 `MON-D1A/B`가 client·runner에 독립 ID로 연결되고 readiness 대기가 추가됐다. Client build가 warning/error 0으로 통과했다. | feature-map은 `source 구현·process 미검증`이며 actual process log가 없다. |
| 8 AutomaticTurnDispatch | `TD-D4`, `TD-D5`, `TD-D6`, `TD-E2A`, `TD-F5A` scenario·dispatch·runner 경로가 추가됐다. Client·Play·Session build가 warning/error 0으로 통과했다. | feature-map은 다섯 ID를 아직 `미구현`으로 기록한다. 관련 targeted regression은 `3/3`과 `1/1`로 통과했지만 actual process log는 없다. |
| 11 ObservabilityOps | `OBS-C9A/B` client dispatch와 runner readiness 경로가 추가됐다. Client·Play·Session build가 warning/error 0으로 통과했다. | feature-map은 두 ID를 `source 구현·process 미검증`으로 기록하며 actual process log가 없다. |
| 12 ChannelEgressRouting | split selector `CH-E2E-04A/B/C`, `CH-E2E-07A/B/C`와 aggregate scenario 목록을 runner에 연결했다. Client·Server build가 warning/error 0으로 통과했다. | 현재 변경으로 이전의 “누락 selector 때문에 all exit 2” source 상태는 바뀌었지만, 현재 `all`을 실행하지 않았다. feature-map의 partial/missing 상태와 process evidence는 열린 상태다. |
| 14 InstanceSpot | `run_e2e.sh`가 추가됐지만 feature-map inventory만 출력하고 exit 2로 종료한다. | role server·client·process fixture가 없으므로 미완료다. |

현재 변경된 runner의 `bash -n`은 exit 0이다. 이 결과는 shell syntax와 source wiring만 확인하며,
role process가 실제로 시작되고 client-visible terminal assertion을 통과했다는 뜻은 아니다.

## 실행한 좁은 검증

| 검증 | 결과 |
|---|---|
| `ExecutionTurn` sample regression filter | `3/3` 통과 |
| `E2ERunner` sample regression filter | `1/1` 통과 |
| `AutomaticTurnDispatchScenariosOwnTheirVerificationFlows` | `1/1` 통과 |
| 현재 변경 대상 runner `bash -n` | exit 0 |
| 변경된 E2E project build | AutomaticTurnDispatch Client/Play/Session, RegistrationCodec Client/Server, ChannelEgressRouting Client/Server, ObservabilityOps Client/Play/Session, RuntimeMonitoring Client, SpotService Client 모두 warning 0/error 0 |

첫 combined filter command는 testhost가 해당 filter 표현을 인식하지 않아 `No test matches`를 반환했다.
이 결과는 test failure가 아니므로 판정에 사용하지 않았고, 각 test name을 직접 지정해 위 결과를 다시
확인했다.

## 현재 판정과 재개 지점

2026-08-02 23:51의 마지막 runtime round는 .NET UnitTests `1431/1431`, 전체 solution
`1890/1890`, bindings test `142/142`, `verify_packaged_contract.sh` exit 0을 기록했다. 현재
production/package 경로에 추가 dirty 변경이 없으므로 해당 runtime baseline은 유지한다. 그러나 그
round 이후 변경된 E2E source를 사용한 actual process evidence는 아직 없다.

따라서 현재 상태는 다음과 같다.

- source·selector·runner 진행: Config 2·4·7·8·11·12에서 확인
- build·좁은 regression: 통과
- actual process E2E: 새 변경 기준 0건
- Config 14: 의도적으로 fail-closed, exit 2
- Phase A: 미완료
- Phase B: 시작 금지

다음 재개 순서는 각 변경 lane의 단일 selector process 실행과 feature-map 갱신이다. 그 뒤 Config 12
`all`을 다시 실행하고 Config 14의 fixture를 구현해야 한다. process log, client-visible result,
role-server evidence와 cleanup을 확인하기 전에는 feature-map을 `actual 통과`로 올리지 않는다.
