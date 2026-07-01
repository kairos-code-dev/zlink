# Node.js RuntimeMonitoring E2E 포팅 인벤토리

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

기준 구현: `framework/languages/dotnet/e2e/RuntimeMonitoring`

현재 상태: Node.js `RuntimeMonitoring` config는 MON-A1~MON-D1이 구현되어 있다.
이 inventory는 `.NET` 기준 파일과 공통 scenario ID를 빠뜨리지 않고, 각 행을 `done` 또는 public contract 근거가 있는 `gap`으로 고정한다.

## Scenario

| Scenario | .NET 기준 파일 | Node.js 대상 파일 | 상태 | 비고 |
|----------|----------------|-------------------|------|------|
| MON-A1 | `Client/Scenarios/MonA1SocketEventsScenario.cs` | `Client/Scenarios/mon-a1-socket-events-scenario.ts` | done | socket event observation. PASS: `logs/20260702-051108-1129` |
| MON-A2 | `Client/Scenarios/MonA2RegistryEventsScenario.cs` | `Client/Scenarios/mon-a2-registry-events-scenario.ts` | done | registry event observation. PASS: `logs/20260702-051108-1129` |
| MON-A3 | `Client/Scenarios/MonA3SpotEventsScenario.cs` | `Client/Scenarios/mon-a3-spot-events-scenario.ts` | done | spot event observation. PASS: `logs/20260702-051108-1129` |
| MON-A4 | `Client/Scenarios/MonA4AvailabilityTransitionScenario.cs` | `Client/Scenarios/mon-a4-availability-transition-scenario.ts` | done | public runtime drain/restore와 trigger socket `PeerAdmissionChanged` evidence 검증. PASS: `logs/20260702-051108-1129` |
| MON-A5 | `Client/Scenarios/MonA5FixedKindsScenario.cs` | `Client/Scenarios/mon-a5-fixed-kinds-scenario.ts`, `Client/main.ts`, `feature-map.ko.md` | done | malformed raw TCP attempt를 public `HandshakeFailed` socket evidence로 검증. PASS: `logs/20260702-051108-1129` |
| MON-B1 | `Client/Scenarios/MonB1KindFilterScenario.cs` | `Client/Scenarios/mon-b1-kind-filter-scenario.ts` | done | event kind filter. PASS: `logs/20260702-051108-1129` |
| MON-B2 | `Client/Scenarios/MonB2RegistrationValidationScenario.cs` | `Client/Scenarios/mon-b2-registration-validation-scenario.ts` | done | registration validation. PASS: `logs/20260702-051108-1129` |
| MON-C1 | `Client/Scenarios/MonC1DispatchFailureScenario.cs` | `Client/Scenarios/mon-c1-dispatch-failure-scenario.ts` | done | dispatch failure isolation. PASS: `logs/20260702-051108-1129` |
| MON-D1 | `Client/Scenarios/MonD1FailureRecoveryScenario.cs` | `Client/Scenarios/mon-d1-failure-recovery-scenario.ts` | done | failure/recovery continuity. PASS: `logs/20260702-051108-1129` |

## File Mapping

| .NET 기준 영역 | Node.js 대상 영역 | 분류 | 상태 | 비고 |
|----------------|-------------------|------|------|------|
| `.gitignore` | `.gitignore`, `logs/.gitignore` | ignore | done | dist, node_modules, 실행 로그 제외 |
| `feature-map.ko.md` | `feature-map.ko.md` | feature-map | done | scenario 범위와 gap 상태 기록 |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | Registry, Service, FilteredService, ThrowingService, Trigger build/start/readiness/cleanup/client 실행 구현 |
| `Shared/Messages.cs`, `Shared/RuntimeMonitoring.Shared.csproj` | `Shared/messages.ts` | shared | done | profile request/reply와 evidence wait 계약 포팅 |
| `Client/Program.cs`, `Client/RuntimeMonitoring.Client.csproj` | `Client/main.ts`, `Client/package.json`, `Client/tsconfig.json` | client-entry/project | done | implemented scenario 선택과 실행 앱 구현 |
| `Client/Support/*` | `Client/Support/` | support | done | options, assertion, HTTP helper 포팅 |
| `Client/Scenarios/*.cs` | `Client/Scenarios/` | scenario | done | MON-A1~MON-D1 구현 |
| `Server/Registry/*` | `Server/Registry/` | registry-role | done | registry monitoring source, event recorder, health/shutdown/evidence endpoint 구현 |
| `Server/Service/*` | `Server/Service/` | service-role | done | socket/spot monitoring source, event recorder, request handler, entry spot timer 구현 |
| `Server/FilteredService/*` | `Server/FilteredService/`, `Server/Service/` | filtered-service-role | done | 별도 role entrypoint와 package를 두고, 내부 구현은 Service host factory를 재사용한다. runner는 `--socket-filter` role-switch option을 넘기지 않는다. |
| `Server/ThrowingService/*` | `Server/ThrowingService/`, `Server/Service/` | throwing-service-role | done | 별도 role entrypoint와 package를 두고, 내부 구현은 Service host factory를 재사용한다. runner는 `--throw-monitor` role-switch option을 넘기지 않는다. |
| `Server/Trigger/*` | `Server/Trigger/` | trigger-role | done | transient/service-B/throwing-service/malformed trigger, validation/log endpoint 구현 |

## Public Contract 확인 필요

- Node framework가 monitoring event source, kind filter, registration validation, event dispatch failure evidence를
  `.NET`과 같은 public surface로 노출하는지 확인해야 한다.
- `MON-A5`는 `.NET`처럼 malformed raw TCP attempt를 socket `HandshakeFailed` 또는 `Internal` evidence로
  관찰해야 한다. Node는 native `Disconnected` event의 handshake failure reason을 public `HandshakeFailed`
  kind로 매핑해 fixed kind 검증을 완료했다.
- public contract가 없으면 internal helper, raw frame, 테스트 전용 adapter로 우회하지 않고 해당 scenario를
  `gap`으로 남긴다.

## 후속 계약 판정

| Scenario | 판정 | 다음 작업 |
|----------|------|-----------|
| `MON-A5` | 구현 | native disconnect reason이 handshake failure일 때 public `HandshakeFailed` kind로 매핑한다. `logs/20260702-051108-1129` all run에서 marker를 확인했다. |
