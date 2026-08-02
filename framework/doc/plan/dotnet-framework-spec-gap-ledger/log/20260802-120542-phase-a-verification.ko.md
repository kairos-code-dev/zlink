# Phase A 최신 검증 기록

## 대상과 현재 조건

이 기록은 `.NET Framework` Phase A의 source, contract, unit, package와 process E2E gate를
현재 working tree에서 다시 실행한 결과를 보존한다. 기준 branch는
`agent/framework-contract-runtime-update`이고 `HEAD`는
`9efee01aa39ace3db8e0f50c46ba9c12864f2cc2`다. working tree에는 다른 언어 workstream의 변경이
함께 있으므로 이 검증은 `.NET` 경로와 ledger log만 대상으로 했다.

## 실행 결과

| 구분 | 실행 결과 | 판정 |
|---|---|---|
| `.NET` UnitTests build | `dotnet build tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj --no-restore --no-incremental --maxcpucount:1` 성공, error 0, warning 10 | 통과. nullable warning은 남아 있다. |
| `.NET` ContractTests | 전체 `76/76` 통과 | 통과. exact-interface declaration·owner, source/package export와 contract snapshot을 포함한다. |
| Documentation/runner regression | `RegressionTests` filter `21/21` 통과 | exact-interface 문서 owner, matrix reference, Config 1~14 inventory, aggregate runner entry와 fail-closed runner reference를 확인했다. |
| relocation·HWM·Entry Spot targeted UnitTests | `DrainCoordinatorTests`, `ActorHandoffTests`, `StandaloneActorRelocationRuntimeTests`, `MaintenanceRuntimeTests`, `EntrySpotActorDispatchTests`, `InboundDispatchOptionsTests`, `TopologyExactSurface` filter `305/305` 통과 | 통과. |
| `.NET` UnitTests 전체 | `dotnet test ... --no-build --blame-crash --blame-crash-dump-type mini` `1406/1406` 통과, 약 2분 40초 | 통과. testhost crash와 timeout은 발생하지 않았다. |
| NuGet package contract | `framework/languages/dotnet/scripts/verify_packaged_contract.sh` exit 0 | 통과. 9개 package의 assembly manifest, dependency, clean consumer와 HTTP package consumer를 확인했다. |
| `LocationMessaging:RM-A2` process E2E | `./e2e/run_e2e_all.sh --config-timeout-seconds 300 -- LocationMessaging:RM-A2` exit 0, `LocationMessaging PASS`, `total PASS`, 15초 | 통과. log: `framework/languages/dotnet/e2e/LocationMessaging/logs/20260802-120542-77932/` |
| Config 12 aggregate | `ChannelEgressRouting/run_e2e.sh all` exit 2 | fail-closed. `CH-E2E-03`, `CH-E2E-08`, `CH-REG-02`, `CH-REG-05` process evidence가 없다. |
| Config 14 aggregate | `InstanceSpot/run_e2e.sh all` exit 2 | fail-closed. process fixture, role server, client evidence가 없다. |
| 변경 문서·source whitespace | `git diff --check` | 통과. |

Package gate에서 확인한 public API snapshot hash는
`399d5e99932d10574db163537bf6858f49a221331512358f06b2140c083e549a`다. 생성된 temporary package의
hash는 command output에 남겼으며, package는 temporary directory에서 consumer와 함께 검증한 뒤
삭제되었다.

## 현재 판정

STREAM socket configuration과 actual endpoint 연결, host Application HWM의 RouteMesh·Spot·Actor·
STREAM ingress 적용, relocation deadline·commit boundary·Entry Spot admission·observer terminal
보존에 대한 현재 source 변경은 build, targeted test와 전체 unit test를 통과한다. `LocationMessaging:RM-A2`
process 경로와 package consumer도 통과한다.

Phase A는 아직 완료가 아니다. 공통 Config 1~14의 feature-map에는 `미구현`, `부분 구현·실행 대기`,
`source 구현·process 미검증` 상태가 남아 있다. Config 12와 Config 14의 aggregate는 이를 성공으로
세지 않도록 exit 2를 반환한다. Config 1의 단일 selector 통과만 전체 relocation·HWM·Instance Spot
process 의미를 증명하지 않으므로 DN-E2E-IMP-001~017, 중앙 matrix의 실행 가능한 test reference 보강,
독립 최종 audit은 열린 상태로 유지한다.

## Review와 남은 조건

기존 A-G1 Sol Medium review의 결과는 이 최신 실행 결과로 대체하지 않는다. 현재 source 변경에 대한
새 Sol High 이상 독립 audit과 process E2E 전체 증거가 아직 없다. 따라서 이 기록은 구현·package·부분
process gate의 evidence이며, Phase A 완료 선언은 아니다.
