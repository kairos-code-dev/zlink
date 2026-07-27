# ObservabilityOps .NET feature map

이 표는 공통 Config 11의 scenario ID가 `.NET` E2E 앱에서 어떤 증거로 검증되는지 기록한다.
이 앱은 샘플 프로젝트를 참조하지 않으며, session·play·workflow 역할과 검증 client를 자체 계약으로 구성한다.

| ID | 상태 | .NET 구현 |
|----|------|-----------|
| OBS-A1 | 구현 | session request가 actor/API, Play와 room Spot을 같은 flow id로 통과하고 시간 순서가 유지되는지 검증한다. |
| OBS-A2 | 구현 | 미등록 packet의 received/error가 같은 flow id를 사용하고 protocol error가 trigger에 반환되는지 검증한다. |
| OBS-A3 | 구현 | connector가 flow를 만들고 tracing off API node가 기록 없이 flow를 Play로 전달하는지 검증한다. |
| OBS-A4 | 구현 | projection fan-out 두 갈래가 같은 flow id를 사용하고 room timer가 timer origin flow를 만드는지 검증한다. |
| OBS-B1 | 구현 | 세 connector의 active/opened/closed 증감과 reconnect attempt counter를 정확한 meter sample로 검증한다. |
| OBS-B2 | 구현 | Entry/user Spot queue 계열, actor transfer count/duration과 pending request sample을 검증한다. |
| OBS-B3 | 구현 | fanout 1:2, lease renew lateness, 닫힌 label과 고카디널리티 label 부재를 검증한다. |
| OBS-B4 | 구현 | metric reader가 설정되지 않은 node에 raw sample이 보관되지 않는지 검증한다. |
| OBS-C1 | 전환 대상 | 제거된 drain API 대신 host `Relocate`의 `Relocating`·`Relocated`, 신규 배치 제외와 기존 연결 유지를 검증해야 한다. |
| OBS-C2 | 전환 대상 | host `Relocate`가 Actor authority와 bound session route를 같은 ObjectGeneration으로 전환하는 현재 계약으로 고쳐야 한다. |
| OBS-C3 | 전환 대상 | User Spot과 member Actor의 aggregate relocation, queue·timer 복원과 단일 authority publication을 검증해야 한다. |
| OBS-C4 | 전환 대상 | `Shutdown`의 closing callback, accepted barrier와 physical session disconnect 통지를 검증해야 한다. |
| OBS-C5 | 전환 대상 | eligible target이 없을 때 source가 `Serving`을 유지하고 `Blocked/TargetUnavailable`로 끝나는지 검증해야 한다. |
| OBS-C6 | 미구현 | 새 application version target을 준비한 뒤 stateful object를 이전하는 rolling update E2E가 없다. |
| OBS-C7 | 미구현 | 동일 version target만 선택하는 planned maintenance E2E가 없다. |
| OBS-C8 | 미구현 | closing callback deadline과 bounded forced teardown E2E가 없다. |
| OBS-C9 | 미구현 | automatic peer readiness 대기와 manual topology precommit 차단 E2E가 없다. |
| OBS-C10 | 미구현 | PlannedMaintenance와 RollingUpdate의 exact version 선택 E2E가 없다. |
| OBS-C11 | 미구현 | concurrent `Relocate` option 공유·충돌과 terminal replay E2E가 없다. |

## 실행 구조

`Server/Session`, `Server/Play`, `Server/Workflow`는 각 역할을 별도 프로세스로 실행한다.
현재 `Client/Scenarios`는 OBS-A1~B4의 기존 관측 시나리오와 이전 drain 기반 OBS-C1~C5만 가진다.
현행 Config 11의 OBS-C1~C11을 모두 구현했다는 증거가 아니다. Runtime M6 gate 뒤 C track을
`Relocate`와 `Shutdown`의 현재 공개 API로 교체하고, 각 ID마다 별도 scenario 파일과 runner
registration을 추가한다.
